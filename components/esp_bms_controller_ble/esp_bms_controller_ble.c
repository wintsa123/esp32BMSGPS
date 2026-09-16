#include "esp_bms_controller_ble.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

#include "esp_bms_idf_runtime.h"

static const char *TAG = "esp_bms_controller_ble";

#define CONTROLLER_SCAN_DEBUG_REPORT_LIMIT 32U
#define CONTROLLER_SCAN_DURATION_MS 10000U
#define CONTROLLER_CONNECT_TIMEOUT_MS 10000U
#define CONTROLLER_FIRST_FRAME_TIMEOUT_MS 10000U
/* 宿主未同步（NimBLE reset 后可能不再自动同步）时，超过这个时间就重建宿主栈 */
#define CONTROLLER_HOST_SYNC_TIMEOUT_MS 5000U
#define CONTROLLER_READ_PERIOD_MS 200U
#define CONTROLLER_KEEPALIVE_PERIOD_MS 3000U
#define CONTROLLER_OPEN_RETRY_MS 1000U
/* 在线后的操作周期：保活与轮询读包按此节奏交替 */
#define CONTROLLER_ONLINE_PERIOD_MS 1000U
/* 通知缓冲：型号之间的帧长与打包方式不同，按滑动解析需要的最大长度预留 */
#define CONTROLLER_NOTIFY_BUFFER_LEN 64U
#define CONTROLLER_TIRE_RIM_MIN ESP_BMS_CONTROLLER_TIRE_RIM_MIN
#define CONTROLLER_TIRE_RIM_MAX ESP_BMS_CONTROLLER_TIRE_RIM_MAX
#define CONTROLLER_TIRE_ASPECT_MIN ESP_BMS_CONTROLLER_TIRE_ASPECT_MIN
#define CONTROLLER_TIRE_ASPECT_MAX ESP_BMS_CONTROLLER_TIRE_ASPECT_MAX
#define CONTROLLER_TIRE_ASPECT_STEP ESP_BMS_CONTROLLER_TIRE_ASPECT_STEP
#define CONTROLLER_TIRE_WIDTH_MIN ESP_BMS_CONTROLLER_TIRE_WIDTH_MIN
#define CONTROLLER_TIRE_WIDTH_MAX ESP_BMS_CONTROLLER_TIRE_WIDTH_MAX
#define CONTROLLER_TIRE_WIDTH_STEP ESP_BMS_CONTROLLER_TIRE_WIDTH_STEP
#define CONTROLLER_RATIO_CENTI_MIN ESP_BMS_CONTROLLER_RATIO_CENTI_MIN
#define CONTROLLER_RATIO_CENTI_MAX ESP_BMS_CONTROLLER_RATIO_CENTI_MAX

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))

typedef enum {
    CONTROLLER_BLE_PHASE_IDLE = 0,
    CONTROLLER_BLE_PHASE_SCANNING = 1,
    CONTROLLER_BLE_PHASE_CONNECTING = 2,
    CONTROLLER_BLE_PHASE_DISCOVERING_SERVICE = 3,
    CONTROLLER_BLE_PHASE_DISCOVERING_CHARACTERISTIC = 4,
    CONTROLLER_BLE_PHASE_DISCOVERING_CCCD = 5,
    CONTROLLER_BLE_PHASE_SUBSCRIBING = 6,
    CONTROLLER_BLE_PHASE_ONLINE = 7,
    CONTROLLER_BLE_PHASE_BACKOFF = 8,
    /* 未知型号：遍历对端全部服务/特征找可用的 notify+write 组合 */
    CONTROLLER_BLE_PHASE_PROBING = 9,
} controller_ble_phase_t;

typedef enum {
    CONTROLLER_PROFILE_NONE = 0,
    CONTROLLER_PROFILE_NUS = 1,
    CONTROLLER_PROFILE_FFE0 = 2,
    /* 既不是 NUS 也不是 FFE0 的型号：UUID 由探测结果现场填入 */
    CONTROLLER_PROFILE_DISCOVERED = 3,
} controller_profile_t;

typedef struct {
    const char *name;
    const ble_uuid_t *service_uuid;
    const ble_uuid_t *notify_uuid;
    const ble_uuid_t *write_uuid;
    bool read_polling;
} controller_profile_config_t;

typedef struct {
    char mac[18];
    char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
} controller_scan_name_cache_entry_t;

/* 未知型号探测：对端服务/特征清单。数量按常见 BLE 串口模块取余量，超出时
 * 截断并记日志，不阻塞探测。 */
#define CONTROLLER_DISCOVERY_SERVICE_MAX 8U
#define CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX 24U

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t start_handle;
    uint16_t end_handle;
} controller_discovery_service_t;

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t val_handle;
    uint32_t properties;
    uint8_t service_index;
} controller_discovery_characteristic_t;

static controller_discovery_service_t
    s_discovery_services[CONTROLLER_DISCOVERY_SERVICE_MAX];
static uint8_t s_discovery_service_count;
static controller_discovery_characteristic_t
    s_discovery_characteristics[CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX];
static uint8_t s_discovery_characteristic_count;
static uint8_t s_discovery_service_cursor;

/* 探测命中的 UUID：留到订阅与收发阶段使用（ble_uuid_any_t 自带 128 位存储，
 * 指向其成员 u 的指针可以长期持有）。 */
static ble_uuid_any_t s_discovered_service_uuid;
static ble_uuid_any_t s_discovered_notify_uuid;
static ble_uuid_any_t s_discovered_write_uuid;

static controller_scan_name_cache_entry_t
    s_controller_scan_name_cache[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
static uint8_t s_controller_scan_name_cache_count;
static uint8_t s_controller_scan_name_cache_next;

static const ble_uuid128_t CONTROLLER_NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t CONTROLLER_NUS_NOTIFY_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
static const ble_uuid128_t CONTROLLER_NUS_WRITE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid16_t CONTROLLER_FFE0_SERVICE_UUID = BLE_UUID16_INIT(0xFFE0U);
static const ble_uuid16_t CONTROLLER_FFE0_NOTIFY_UUID = BLE_UUID16_INIT(0xFFECU);
static const ble_uuid16_t CONTROLLER_FFE0_WRITE_UUID = BLE_UUID16_INIT(0xFFECU);

static const controller_profile_config_t CONTROLLER_PROFILES[] = {
    [CONTROLLER_PROFILE_NONE] = { .name = "none" },
    [CONTROLLER_PROFILE_NUS] = {
        .name = "NUS",
        .service_uuid = &CONTROLLER_NUS_SERVICE_UUID.u,
        .notify_uuid = &CONTROLLER_NUS_NOTIFY_UUID.u,
        .write_uuid = &CONTROLLER_NUS_WRITE_UUID.u,
        .read_polling = true,
    },
    [CONTROLLER_PROFILE_FFE0] = {
        .name = "FFE0",
        .service_uuid = &CONTROLLER_FFE0_SERVICE_UUID.u,
        .notify_uuid = &CONTROLLER_FFE0_NOTIFY_UUID.u,
        .write_uuid = &CONTROLLER_FFE0_WRITE_UUID.u,
        .read_polling = false,
    },
    [CONTROLLER_PROFILE_DISCOVERED] = {
        .name = "discovered",
        .service_uuid = &s_discovered_service_uuid.u,
        .notify_uuid = &s_discovered_notify_uuid.u,
        .write_uuid = &s_discovered_write_uuid.u,
        /* 未知型号按 FFE0 路径处理：先用开启指令拉数据流，首帧前再插入读请求。 */
        .read_polling = false,
    },
};

/* 连接参数档位：远驱各型号 BLE 模块的容忍度不同。
 * 档 0 是真机验证过的参数（30-40ms 间隔 + 最长监督超时）；
 * 档 1 更保守（40-60ms 间隔 + 1.6s 超时），供连不上的型号重试。 */
static const struct ble_gap_conn_params CONTROLLER_CONN_PARAMS[] = {
    {
        .scan_itvl = 0x0010,
        .scan_window = 0x0010,
        .itvl_min = 0x0018,
        .itvl_max = 0x0028,
        .latency = 0,
        .supervision_timeout = 0x0C80,
        .min_ce_len = 0,
        .max_ce_len = 0,
    },
    {
        .scan_itvl = 0x0020,
        .scan_window = 0x0020,
        .itvl_min = 0x0028,
        .itvl_max = 0x003C,
        .latency = 0,
        .supervision_timeout = 0x0640,
        .min_ce_len = 0,
        .max_ce_len = 0,
    },
};

static controller_profile_t s_controller_profile;
static uint8_t s_controller_poll_index;
static uint32_t s_controller_first_frame_elapsed_ms;
static uint8_t s_notify_diag_count;
/* CCCD 订阅值：0x0001=Notify，0x0002=Indicate；0 表示未探测到，按 Notify 处理 */
static uint16_t s_controller_cccd_value;
/* 连接参数档位：型号对 itvl/监督超时的容忍度不同，连接失败后换档重试 */
static uint8_t s_controller_conn_param_index;
/* 首帧前按固定顺序轮流尝试控制帧与读包；在线后保活与读包交替 */
static uint8_t s_controller_stream_step;
static uint8_t s_controller_online_step;
/* 下次连接从哪一档 profile 开始：首帧超时说明该档拉不出数据，向后轮换 */
static controller_profile_t s_controller_profile_start = CONTROLLER_PROFILE_NUS;

static void controller_copy_text(char *out, size_t out_len, const char *text)
{
    if (!out || out_len == 0U) {
        return;
    }
    if (!text) {
        out[0] = '\0';
        return;
    }
    strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

/* 同一个 16 位 UUID 可能被对端声明成等价的 128 位蓝牙 base 形式
 * （0000xxxx-0000-1000-8000-00805f9b34fb）。NimBLE 的 ble_uuid_cmp 只比较
 * 同类型 UUID，这里补一层等价比较，避免只因声明的宽度不同就判定"特征缺失"。 */
static uint16_t controller_uuid16_value(const ble_uuid_t *uuid)
{
    if (!uuid) {
        return 0U;
    }
    switch (uuid->type) {
    case BLE_UUID_TYPE_16:
        return BLE_UUID16(uuid)->value;
    case BLE_UUID_TYPE_32:
        return (uint16_t)BLE_UUID32(uuid)->value;
    case BLE_UUID_TYPE_128: {
        static const uint8_t base_tail[8] = { 0x80U, 0x00U, 0x00U, 0x80U,
                                              0x00U, 0x10U, 0x00U, 0x00U };
        const uint8_t *value = BLE_UUID128(uuid)->value;
        if (memcmp(value + 4U, base_tail, sizeof(base_tail)) != 0 ||
            value[14] != 0x00U || value[15] != 0x00U) {
            return 0U;
        }
        return (uint16_t)((uint16_t)value[12] | ((uint16_t)value[13] << 8U));
    }
    default:
        return 0U;
    }
}

static bool controller_uuid_matches(const ble_uuid_t *candidate, const ble_uuid_t *expected)
{
    if (!candidate || !expected) {
        return false;
    }
    if (ble_uuid_cmp(candidate, expected) == 0) {
        return true;
    }
    const uint16_t candidate16 = controller_uuid16_value(candidate);
    const uint16_t expected16 = controller_uuid16_value(expected);
    return candidate16 != 0U && candidate16 == expected16;
}

static bool controller_tire_matches_policy(uint8_t rim_inch,
                                            uint8_t aspect_percent,
                                            uint16_t width_mm)
{
    return rim_inch >= CONTROLLER_TIRE_RIM_MIN && rim_inch <= CONTROLLER_TIRE_RIM_MAX &&
           aspect_percent >= CONTROLLER_TIRE_ASPECT_MIN &&
           aspect_percent <= CONTROLLER_TIRE_ASPECT_MAX &&
           (aspect_percent - CONTROLLER_TIRE_ASPECT_MIN) % CONTROLLER_TIRE_ASPECT_STEP == 0U &&
           width_mm >= CONTROLLER_TIRE_WIDTH_MIN && width_mm <= CONTROLLER_TIRE_WIDTH_MAX &&
           (width_mm - CONTROLLER_TIRE_WIDTH_MIN) % CONTROLLER_TIRE_WIDTH_STEP == 0U;
}

static bool controller_ratio_matches_policy(uint16_t ratio_centi)
{
    return ratio_centi >= CONTROLLER_RATIO_CENTI_MIN &&
           ratio_centi <= CONTROLLER_RATIO_CENTI_MAX;
}

static void controller_sync_parameters(esp_bms_idf_runtime_t *runtime)
{
    const esp_fardriver_state_t *state = &runtime->controller_state;
    if (!state->controller_speed_params_valid) {
        return;
    }
    if (runtime->controller_observed_tire_rim_inch == state->tire_rim_inch &&
        runtime->controller_observed_tire_aspect_percent == state->tire_aspect_percent &&
        runtime->controller_observed_tire_width_mm == state->tire_width_mm &&
        runtime->controller_observed_gear_ratio_centi == state->gear_ratio_centi) {
        return;
    }
    runtime->controller_observed_tire_rim_inch = state->tire_rim_inch;
    runtime->controller_observed_tire_aspect_percent = state->tire_aspect_percent;
    runtime->controller_observed_tire_width_mm = state->tire_width_mm;
    runtime->controller_observed_gear_ratio_centi = state->gear_ratio_centi;

    if (!controller_tire_matches_policy(state->tire_rim_inch,
                                        state->tire_aspect_percent,
                                        state->tire_width_mm) ||
        !controller_ratio_matches_policy(state->gear_ratio_centi)) {
        ESP_LOGW(TAG,
                 "parameters not synchronized: tire=%u-%u-%u ratio=%u.%02u",
                 state->tire_rim_inch,
                 state->tire_aspect_percent,
                 state->tire_width_mm,
                 state->gear_ratio_centi / 100U,
                 state->gear_ratio_centi % 100U);
        return;
    }

    if (runtime->controller_fallback_tire_rim_inch == state->tire_rim_inch &&
        runtime->controller_fallback_tire_aspect_percent == state->tire_aspect_percent &&
        runtime->controller_fallback_tire_width_mm == state->tire_width_mm &&
        runtime->controller_state.fallback_gear_ratio_centi == state->gear_ratio_centi) {
        return;
    }
    runtime->controller_fallback_tire_rim_inch = state->tire_rim_inch;
    runtime->controller_fallback_tire_aspect_percent = state->tire_aspect_percent;
    runtime->controller_fallback_tire_width_mm = state->tire_width_mm;
    runtime->controller_state.fallback_wheel_circumference_mm =
        state->wheel_circumference_mm;
    runtime->controller_state.fallback_gear_ratio_centi = state->gear_ratio_centi;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SETTINGS_SAVE_REQUESTED, true);
    ESP_LOGI(TAG,
             "parameters synchronized: tire=%u-%u-%u ratio=%u.%02u",
             state->tire_rim_inch,
             state->tire_aspect_percent,
             state->tire_width_mm,
             state->gear_ratio_centi / 100U,
             state->gear_ratio_centi % 100U);
}

static void controller_clear_telemetry(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    const uint16_t wheel = runtime->controller_state.fallback_wheel_circumference_mm;
    const uint16_t ratio = runtime->controller_state.fallback_gear_ratio_centi;
    memset(&runtime->controller_state, 0, sizeof(runtime->controller_state));
    runtime->controller_state.fallback_wheel_circumference_mm = wheel;
    runtime->controller_state.fallback_gear_ratio_centi = ratio;
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static char controller_hex_char(uint8_t value)
{
    return value < 10U ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void controller_addr_to_mac_text(const uint8_t addr[6], char *out, size_t out_len)
{
    if (!addr || !out || out_len < 18U) {
        return;
    }
    (void)snprintf(out,
                   out_len,
                   "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                   controller_hex_char(addr[5] >> 4U), controller_hex_char(addr[5] & 0x0FU),
                   controller_hex_char(addr[4] >> 4U), controller_hex_char(addr[4] & 0x0FU),
                   controller_hex_char(addr[3] >> 4U), controller_hex_char(addr[3] & 0x0FU),
                   controller_hex_char(addr[2] >> 4U), controller_hex_char(addr[2] & 0x0FU),
                   controller_hex_char(addr[1] >> 4U), controller_hex_char(addr[1] & 0x0FU),
                   controller_hex_char(addr[0] >> 4U), controller_hex_char(addr[0] & 0x0FU));
}

static bool controller_name_copy(char *out, size_t out_len, const uint8_t *name, size_t name_len)
{
    if (!out || out_len == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!name || name_len == 0U) {
        return false;
    }
    const size_t limit = out_len - 1U < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? out_len - 1U
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    size_t copied = 0U;
    for (size_t index = 0U; index < name_len;) {
        size_t sequence_len = 1U;
        const uint8_t first = name[index];
        if ((first & 0xE0U) == 0xC0U) {
            sequence_len = 2U;
        } else if ((first & 0xF0U) == 0xE0U) {
            sequence_len = 3U;
        } else if ((first & 0xF8U) == 0xF0U) {
            sequence_len = 4U;
        }
        if (index + sequence_len > name_len || copied + sequence_len > limit) {
            break;
        }
        memcpy(out + copied, name + index, sequence_len);
        copied += sequence_len;
        index += sequence_len;
    }
    out[copied] = '\0';
    return copied > 0U;
}

static const char *controller_cached_name_locked(const char *mac)
{
    if (!mac || mac[0] == '\0') {
        return NULL;
    }
    for (uint8_t index = 0U; index < s_controller_scan_name_cache_count; ++index) {
        if (strcmp(s_controller_scan_name_cache[index].mac, mac) == 0) {
            return s_controller_scan_name_cache[index].name;
        }
    }
    return NULL;
}

static void controller_cache_name_locked(const char *mac, const char *name)
{
    if (!mac || mac[0] == '\0' || !name || name[0] == '\0') {
        return;
    }
    for (uint8_t index = 0U; index < s_controller_scan_name_cache_count; ++index) {
        if (strcmp(s_controller_scan_name_cache[index].mac, mac) == 0) {
            controller_copy_text(s_controller_scan_name_cache[index].name,
                                 sizeof(s_controller_scan_name_cache[index].name),
                                 name);
            return;
        }
    }
    uint8_t slot = s_controller_scan_name_cache_count;
    if (slot < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        s_controller_scan_name_cache_count++;
    } else {
        slot = s_controller_scan_name_cache_next;
        s_controller_scan_name_cache_next =
            (uint8_t)((s_controller_scan_name_cache_next + 1U) % ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES);
    }
    controller_copy_text(s_controller_scan_name_cache[slot].mac,
                         sizeof(s_controller_scan_name_cache[slot].mac),
                         mac);
    controller_copy_text(s_controller_scan_name_cache[slot].name,
                         sizeof(s_controller_scan_name_cache[slot].name),
                         name);
}

static int controller_gap_event(struct ble_gap_event *event, void *arg);
static int controller_service_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg);
static int controller_dsc_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg);
static int controller_discovery_chr_cb(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       const struct ble_gatt_chr *chr,
                                       void *arg);

static const controller_profile_config_t *controller_profile_config(void)
{
    return &CONTROLLER_PROFILES[s_controller_profile];
}

static void controller_fail_connection(esp_bms_idf_runtime_t *runtime,
                                       uint16_t conn_handle,
                                       const char *stage,
                                       int status)
{
    if (!runtime) {
        return;
    }
    ESP_LOGW(TAG,
             "connection failed: profile=%s stage=%s status=%d phase=%u",
             controller_profile_config()->name,
             stage,
             status,
             runtime->controller_ble_phase);
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
    runtime->controller_keepalive_elapsed_ms = 0U;
    s_controller_first_frame_elapsed_ms = 0U;
    s_controller_poll_index = 0U;
    controller_clear_telemetry(runtime);
    const int rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGW(TAG, "terminate failed: stage=%s rc=%d", stage, rc);
    }
}

static bool controller_discover_profile(esp_bms_idf_runtime_t *runtime,
                                        controller_profile_t profile)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        profile <= CONTROLLER_PROFILE_NONE || profile > CONTROLLER_PROFILE_FFE0) {
        return false;
    }
    s_controller_profile = profile;
    runtime->controller_service_start_handle = 0U;
    runtime->controller_service_end_handle = 0U;
    runtime->controller_char_val_handle = 0U;
    runtime->controller_write_char_val_handle = 0U;
    runtime->controller_cccd_handle = 0U;
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_SERVICE;
    ESP_LOGI(TAG,
             "service discovery: conn=%u profile=%s",
             runtime->controller_conn_handle,
             controller_profile_config()->name);
    const int rc = ble_gattc_disc_svc_by_uuid(runtime->controller_conn_handle,
                                              controller_profile_config()->service_uuid,
                                              controller_service_cb,
                                              runtime);
    if (rc != 0) {
        controller_fail_connection(runtime,
                                   runtime->controller_conn_handle,
                                   "service-start",
                                   rc);
        return false;
    }
    return true;
}

/* ---------- 未知型号探测：既不是 NUS 也不是 FFE0 时遍历对端 GATT ---------- */

/* 串口透传模块常见 UUID：命中时优先，值越大优先级越高 */
static uint8_t controller_discovery_uuid_rank(uint16_t value, bool notify_role)
{
    if (notify_role) {
        switch (value) {
        case 0xFFECU:
            return 4U;
        case 0xFFE1U:
        case 0xFFF1U:
            return 3U;
        case 0xFFE4U:
        case 0xFFE9U:
        case 0xFFF2U:
            return 2U;
        default:
            return 1U;
        }
    }
    switch (value) {
    case 0xFFECU:
        return 4U;
    case 0xFFE1U:
    case 0xFFE2U:
    case 0xFFF2U:
        return 3U;
    case 0xFFE9U:
    case 0xFFF1U:
        return 2U;
    default:
        return 1U;
    }
}

static bool controller_discovery_is_notify(
    const controller_discovery_characteristic_t *characteristic)
{
    return (characteristic->properties &
            (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) != 0U;
}

static bool controller_discovery_is_write(
    const controller_discovery_characteristic_t *characteristic)
{
    return (characteristic->properties &
            (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)) != 0U;
}

/* 特征评分：UUID 偏好优先；同分时属性更"标准"的优先（Notify > Indicate、
 * Write > Write Without Response）。 */
static uint16_t controller_discovery_score(
    const controller_discovery_characteristic_t *characteristic,
    bool notify_role)
{
    const uint8_t rank = controller_discovery_uuid_rank(
        controller_uuid16_value(&characteristic->uuid.u), notify_role);
    uint8_t attribute = 0U;
    if (notify_role) {
        if ((characteristic->properties & BLE_GATT_CHR_F_NOTIFY) != 0U) {
            attribute = 2U;
        } else if ((characteristic->properties & BLE_GATT_CHR_F_INDICATE) != 0U) {
            attribute = 1U;
        }
    } else if ((characteristic->properties & BLE_GATT_CHR_F_WRITE) != 0U) {
        attribute = 2U;
    } else if ((characteristic->properties & BLE_GATT_CHR_F_WRITE_NO_RSP) != 0U) {
        attribute = 1U;
    }
    return (uint16_t)((uint16_t)rank * 4U + attribute);
}

/* 选择策略：优先在同一服务里成对取 notify 与 write（串口透传模块的两个特征总
 * 在同一个服务内）；没有成对服务时，再退化为全设备内评分最高的组合。 */
static bool controller_discovery_select(controller_discovery_characteristic_t *notify_out,
                                        controller_discovery_characteristic_t *write_out)
{
    if (!notify_out || !write_out) {
        return false;
    }
    for (uint8_t service = 0U; service < s_discovery_service_count; ++service) {
        const controller_discovery_characteristic_t *notify_characteristic = NULL;
        const controller_discovery_characteristic_t *write_characteristic = NULL;
        uint16_t notify_score = 0U;
        uint16_t write_score = 0U;
        for (uint8_t index = 0U; index < s_discovery_characteristic_count; ++index) {
            const controller_discovery_characteristic_t *candidate =
                &s_discovery_characteristics[index];
            if (candidate->service_index != service) {
                continue;
            }
            if (controller_discovery_is_notify(candidate)) {
                const uint16_t score = controller_discovery_score(candidate, true);
                if (score > notify_score) {
                    notify_score = score;
                    notify_characteristic = candidate;
                }
            }
            if (controller_discovery_is_write(candidate)) {
                const uint16_t score = controller_discovery_score(candidate, false);
                if (score > write_score) {
                    write_score = score;
                    write_characteristic = candidate;
                }
            }
        }
        if (notify_characteristic && write_characteristic) {
            *notify_out = *notify_characteristic;
            *write_out = *write_characteristic;
            return true;
        }
    }
    uint16_t notify_score = 0U;
    uint16_t write_score = 0U;
    for (uint8_t index = 0U; index < s_discovery_characteristic_count; ++index) {
        const controller_discovery_characteristic_t *candidate =
            &s_discovery_characteristics[index];
        if (controller_discovery_is_notify(candidate)) {
            const uint16_t score = controller_discovery_score(candidate, true);
            if (score > notify_score) {
                notify_score = score;
                *notify_out = *candidate;
            }
        }
        if (controller_discovery_is_write(candidate)) {
            const uint16_t score = controller_discovery_score(candidate, false);
            if (score > write_score) {
                write_score = score;
                *write_out = *candidate;
            }
        }
    }
    return notify_score > 0U && write_score > 0U;
}

static void controller_discovery_finish(esp_bms_idf_runtime_t *runtime);
static void controller_discovery_request_next_service(esp_bms_idf_runtime_t *runtime);

static void controller_discovery_finish(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    controller_discovery_characteristic_t notify_characteristic = { 0 };
    controller_discovery_characteristic_t write_characteristic = { 0 };
    if (s_discovery_service_count == 0U ||
        !controller_discovery_select(&notify_characteristic, &write_characteristic)) {
        ESP_LOGW(TAG,
                 "unknown model: no usable notify/write pair (services=%u characteristics=%u)",
                 (unsigned)s_discovery_service_count,
                 (unsigned)s_discovery_characteristic_count);
        controller_fail_connection(runtime,
                                   runtime->controller_conn_handle,
                                   "discovery-select",
                                   s_discovery_service_count == 0U ? BLE_HS_ENOENT : BLE_HS_EINVAL);
        return;
    }
    const controller_discovery_service_t *service =
        &s_discovery_services[notify_characteristic.service_index];
    ble_uuid_copy(&s_discovered_service_uuid, &service->uuid.u);
    ble_uuid_copy(&s_discovered_notify_uuid, &notify_characteristic.uuid.u);
    ble_uuid_copy(&s_discovered_write_uuid, &write_characteristic.uuid.u);
    s_controller_profile = CONTROLLER_PROFILE_DISCOVERED;
    runtime->controller_service_start_handle = service->start_handle;
    runtime->controller_service_end_handle = service->end_handle;
    runtime->controller_char_val_handle = notify_characteristic.val_handle;
    runtime->controller_write_char_val_handle = write_characteristic.val_handle;
    runtime->controller_cccd_handle = 0U;
    s_controller_cccd_value = (notify_characteristic.properties & BLE_GATT_CHR_F_NOTIFY) != 0U
                                  ? 0x0001U
                                  : 0x0002U;
    char service_text[BLE_UUID_STR_LEN] = { 0 };
    char notify_text[BLE_UUID_STR_LEN] = { 0 };
    char write_text[BLE_UUID_STR_LEN] = { 0 };
    ESP_LOGI(TAG,
             "unknown model ready: service=%s handles=%u-%u notify=%s handle=%u write=%s handle=%u cccd=0x%04x",
             ble_uuid_to_str(&service->uuid.u, service_text),
             service->start_handle,
             service->end_handle,
             ble_uuid_to_str(&notify_characteristic.uuid.u, notify_text),
             notify_characteristic.val_handle,
             ble_uuid_to_str(&write_characteristic.uuid.u, write_text),
             write_characteristic.val_handle,
             s_controller_cccd_value);
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_CCCD;
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
    const int rc = ble_gattc_disc_all_dscs(runtime->controller_conn_handle,
                                           notify_characteristic.val_handle,
                                           service->end_handle,
                                           controller_dsc_cb,
                                           runtime);
    if (rc != 0) {
        controller_fail_connection(runtime,
                                   runtime->controller_conn_handle,
                                   "discovery-cccd-start",
                                   rc);
    }
}

static void controller_discovery_request_next_service(esp_bms_idf_runtime_t *runtime)
{
    while (s_discovery_service_cursor < s_discovery_service_count) {
        const controller_discovery_service_t *service =
            &s_discovery_services[s_discovery_service_cursor];
        const int rc = ble_gattc_disc_all_chrs(runtime->controller_conn_handle,
                                               service->start_handle,
                                               service->end_handle,
                                               controller_discovery_chr_cb,
                                               runtime);
        if (rc == 0) {
            return;
        }
        ESP_LOGW(TAG,
                 "discovery: characteristic walk rejected for service %u rc=%d",
                 (unsigned)s_discovery_service_cursor,
                 rc);
        s_discovery_service_cursor++;
    }
    controller_discovery_finish(runtime);
}

static int controller_discovery_chr_cb(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       const struct ble_gatt_chr *chr,
                                       void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        if (s_discovery_characteristic_count >= CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX) {
            ESP_LOGW(TAG,
                     "discovery: characteristic list full (%u), skipping",
                     (unsigned)CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX);
            return 0;
        }
        controller_discovery_characteristic_t *entry =
            &s_discovery_characteristics[s_discovery_characteristic_count++];
        ble_uuid_copy(&entry->uuid, &chr->uuid.u);
        entry->val_handle = chr->val_handle;
        entry->properties = chr->properties;
        entry->service_index = s_discovery_service_cursor;
        char uuid_text[BLE_UUID_STR_LEN] = { 0 };
        ESP_LOGI(TAG,
                 "discovery characteristic: service=%u uuid=%s handle=%u properties=0x%02x",
                 (unsigned)entry->service_index,
                 ble_uuid_to_str(&entry->uuid.u, uuid_text),
                 entry->val_handle,
                 (unsigned)entry->properties);
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        s_discovery_service_cursor++;
        controller_discovery_request_next_service(runtime);
        return 0;
    }
    controller_fail_connection(runtime,
                               conn_handle,
                               "discovery-characteristic",
                               error ? error->status : -1);
    return 0;
}

static int controller_discovery_svc_cb(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       const struct ble_gatt_svc *service,
                                       void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        char uuid_text[BLE_UUID_STR_LEN] = { 0 };
        ESP_LOGI(TAG,
                 "discovery service: uuid=%s handles=%u-%u",
                 ble_uuid_to_str(&service->uuid.u, uuid_text),
                 service->start_handle,
                 service->end_handle);
        /* 0x1800/0x1801 是 GAP/GATT 必备服务，不可能承载控制器数据流 */
        const uint16_t short_uuid = controller_uuid16_value(&service->uuid.u);
        if (short_uuid == 0x1800U || short_uuid == 0x1801U) {
            return 0;
        }
        if (s_discovery_service_count >= CONTROLLER_DISCOVERY_SERVICE_MAX) {
            ESP_LOGW(TAG,
                     "discovery: service list full (%u), skipping",
                     (unsigned)CONTROLLER_DISCOVERY_SERVICE_MAX);
            return 0;
        }
        controller_discovery_service_t *entry =
            &s_discovery_services[s_discovery_service_count++];
        ble_uuid_copy(&entry->uuid, &service->uuid.u);
        entry->start_handle = service->start_handle;
        entry->end_handle = service->end_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        s_discovery_service_cursor = 0U;
        controller_discovery_request_next_service(runtime);
        return 0;
    }
    controller_fail_connection(runtime,
                               conn_handle,
                               "discovery-service",
                               error ? error->status : -1);
    return 0;
}

static bool controller_start_discovery_probe(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU) {
        return false;
    }
    s_discovery_service_count = 0U;
    s_discovery_characteristic_count = 0U;
    s_discovery_service_cursor = 0U;
    s_controller_profile = CONTROLLER_PROFILE_NONE;
    s_controller_cccd_value = 0U;
    runtime->controller_service_start_handle = 0U;
    runtime->controller_service_end_handle = 0U;
    runtime->controller_char_val_handle = 0U;
    runtime->controller_write_char_val_handle = 0U;
    runtime->controller_cccd_handle = 0U;
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_PROBING;
    ESP_LOGI(TAG,
             "unknown model probe: conn=%u stage=all-services",
             runtime->controller_conn_handle);
    const int rc = ble_gattc_disc_all_svcs(runtime->controller_conn_handle,
                                           controller_discovery_svc_cb,
                                           runtime);
    if (rc != 0) {
        controller_fail_connection(runtime,
                                   runtime->controller_conn_handle,
                                   "discovery-service-start",
                                   rc);
        return false;
    }
    return true;
}

/* 每次连接开始时按记录档位进入发现流程：NUS / FFE0 走已知 UUID，
 * 其余走全服务探测。 */
static void controller_begin_profile_walk(esp_bms_idf_runtime_t *runtime)
{
    if (s_controller_profile_start == CONTROLLER_PROFILE_FFE0) {
        (void)controller_discover_profile(runtime, CONTROLLER_PROFILE_FFE0);
        return;
    }
    if (s_controller_profile_start == CONTROLLER_PROFILE_DISCOVERED) {
        (void)controller_start_discovery_probe(runtime);
        return;
    }
    (void)controller_discover_profile(runtime, CONTROLLER_PROFILE_NUS);
}

/* 当前 profile 找不到可用服务/特征时依次换用：NUS -> FFE0 -> 全服务探测。
 * 型号之间 GATT 差异很大，走完这条链才能覆盖"服务还在但特征不同"的情况。 */
static bool controller_advance_profile(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }
    if (s_controller_profile == CONTROLLER_PROFILE_NUS) {
        ESP_LOGI(TAG, "profile NUS unusable, trying profile FFE0");
        return controller_discover_profile(runtime, CONTROLLER_PROFILE_FFE0);
    }
    if (s_controller_profile == CONTROLLER_PROFILE_FFE0) {
        ESP_LOGI(TAG, "profile FFE0 unusable, probing all services");
        return controller_start_discovery_probe(runtime);
    }
    return false;
}

static void controller_send_command(esp_bms_idf_runtime_t *runtime,
                                    const uint8_t *command,
                                    size_t len)
{
    if (!runtime || !command || len == 0U || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_write_char_val_handle == 0U) {
        return;
    }
    /* 实测 FFEC 属性 0x14 = Notify | Write Without Response，与 PC 路径一致。 */
    const int rc = ble_gattc_write_no_rsp_flat(runtime->controller_conn_handle,
                                               runtime->controller_write_char_val_handle,
                                               command, len);
    ESP_LOGI(TAG, "command tx: profile=%s conn=%u handle=%u mode=no-response len=%u rc=%d",
             controller_profile_config()->name, runtime->controller_conn_handle,
             runtime->controller_write_char_val_handle, (unsigned)len, rc);
    if (rc != 0) {
        ESP_LOGW(TAG, "command send failed: rc=%d", rc);
    }
}

static void controller_send_read_request(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_write_char_val_handle == 0U) {
        return;
    }
    uint8_t address = 0U;
    uint8_t request[ESP_FARDRIVER_READ_REQUEST_LEN];
    const size_t count = esp_fardriver_poll_address_count();
    if (count == 0U || !esp_fardriver_poll_address(s_controller_poll_index, &address) ||
        !esp_fardriver_build_read_request(address, request)) {
        s_controller_poll_index = 0U;
        return;
    }
    const int rc = ble_gattc_write_no_rsp_flat(runtime->controller_conn_handle,
                                               runtime->controller_write_char_val_handle,
                                               request, sizeof(request));
    runtime->controller_keepalive_elapsed_ms = 0U;
    if (rc != 0) {
        ESP_LOGW(TAG, "read request failed: address=0x%02X rc=%d", address, rc);
        return;
    }
    s_controller_poll_index = (uint8_t)((s_controller_poll_index + 1U) % count);
}

static void controller_send_open(esp_bms_idf_runtime_t *runtime)
{
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
    if (esp_fardriver_build_open_command(command)) {
        controller_send_command(runtime, command, sizeof(command));
    }
}

static void controller_send_stream_command(esp_bms_idf_runtime_t *runtime)
{
    /* 官方 App 开场第一条：AA 13 EC 07 01 5F（与 PC 抓包的 0xF1 变体只差末字节） */
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
    if (esp_fardriver_build_control_command(0x13U, 0x07U, 0x01U, 0x5FU, command)) {
        controller_send_command(runtime, command, sizeof(command));
    }
}

static void controller_send_aux_command(esp_bms_idf_runtime_t *runtime)
{
    /* 官方 App 开场第二条：AA 17 E8 F1 13 51 */
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
    if (esp_fardriver_build_control_command(0x17U, 0xF1U, 0x13U, 0x51U, command)) {
        controller_send_command(runtime, command, sizeof(command));
    }
}

static void controller_send_keepalive(esp_bms_idf_runtime_t *runtime)
{
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN];
    if (esp_fardriver_build_keepalive_command(command)) {
        controller_send_command(runtime, command, sizeof(command));
    }
}

static int controller_write_cb(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr,
                               void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle ||
        runtime->controller_ble_phase != (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING) {
        return 0;
    }
    const int status = error ? error->status : -1;
    if (status != 0) {
        controller_fail_connection(runtime, conn_handle, "subscribe", status);
        return 0;
    }
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, true);
    runtime->controller_keepalive_elapsed_ms = 0U;
    s_controller_first_frame_elapsed_ms = 0U;
    ESP_LOGI(TAG,
             "subscription ready: conn=%u profile=%s notify=%u write=%u cccd=%u stage=wait-frame",
             conn_handle,
             controller_profile_config()->name,
             runtime->controller_char_val_handle,
             runtime->controller_write_char_val_handle,
             runtime->controller_cccd_handle);
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
    if (controller_profile_config()->read_polling) {
        controller_send_read_request(runtime);
    } else {
        /* 等待订阅及对端连接参数更新完成后，由 tick 开启数据流。 */
        ESP_LOGI(TAG, "stream start deferred: delay_ms=%u", CONTROLLER_OPEN_RETRY_MS);
    }
    return 0;
}

static void controller_set_subscription(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_cccd_handle == 0U ||
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED)) {
        return;
    }
    /* CCCD 值必须与特征属性一致：Notify=0x0001，Indicate=0x0002 */
    const uint16_t cccd_value =
        s_controller_cccd_value != 0U ? s_controller_cccd_value : 0x0001U;
    const uint8_t value[2] = { (uint8_t)(cccd_value & 0xFFU), (uint8_t)(cccd_value >> 8U) };
    const int rc = ble_gattc_write_flat(runtime->controller_conn_handle,
                                        runtime->controller_cccd_handle,
                                        value,
                                        sizeof(value),
                                        controller_write_cb,
                                        runtime);
    if (rc != 0) {
        controller_fail_connection(runtime,
                                   runtime->controller_conn_handle,
                                   "subscribe-start",
                                   rc);
    }
}

static int controller_dsc_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle ||
        chr_val_handle != runtime->controller_char_val_handle) {
        return 0;
    }
    if (error && error->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            runtime->controller_cccd_handle = dsc->handle;
            ESP_LOGI(TAG,
                     "CCCD found: conn=%u profile=%s handle=%u",
                     conn_handle,
                     controller_profile_config()->name,
                     dsc->handle);
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_cccd_handle != 0U) {
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING;
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        controller_set_subscription(runtime);
        return 0;
    }
    controller_fail_connection(runtime,
                               conn_handle,
                               "cccd",
                               error ? error->status : -1);
    return 0;
}

static int controller_chr_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr,
                             void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        const controller_profile_config_t *profile = controller_profile_config();
        if (controller_uuid_matches(&chr->uuid.u, profile->notify_uuid) &&
            (chr->properties & (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) != 0) {
            runtime->controller_char_val_handle = chr->val_handle;
            /* 只声明 Indicate 的模块必须写 CCCD=0x0002，否则订阅后收不到任何数据 */
            s_controller_cccd_value =
                (chr->properties & BLE_GATT_CHR_F_NOTIFY) != 0 ? 0x0001U : 0x0002U;
            ESP_LOGI(TAG,
                     "notify characteristic: profile=%s handle=%u properties=0x%02x cccd=0x%04x",
                     profile->name,
                     chr->val_handle,
                     chr->properties,
                     s_controller_cccd_value);
        }
        /* FFE0 的 notify 与 write 共用同一个特征（0xFFEC），两个句柄都要记录 */
        if (controller_uuid_matches(&chr->uuid.u, profile->write_uuid) &&
            (chr->properties & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)) != 0) {
            runtime->controller_write_char_val_handle = chr->val_handle;
            ESP_LOGI(TAG,
                     "write characteristic: profile=%s handle=%u properties=0x%02x",
                     controller_profile_config()->name,
                     chr->val_handle,
                     chr->properties);
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->controller_char_val_handle != 0U &&
            runtime->controller_write_char_val_handle != 0U) {
            runtime->controller_cccd_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_CCCD;
            const int rc = ble_gattc_disc_all_dscs(conn_handle,
                                                   runtime->controller_char_val_handle,
                                                   runtime->controller_service_end_handle,
                                                   controller_dsc_cb,
                                                   runtime);
            if (rc == 0) {
                return 0;
            }
            controller_fail_connection(runtime, conn_handle, "cccd-start", rc);
            return 0;
        }
        /* 服务在但特征不同（UUID/属性不符）：继续换 profile，不要直接放弃 */
        if (controller_advance_profile(runtime)) {
            return 0;
        }
    }
    controller_fail_connection(runtime,
                               conn_handle,
                               "characteristic",
                               error ? error->status : -1);
    return 0;
}

static int controller_service_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        runtime->controller_service_start_handle = service->start_handle;
        runtime->controller_service_end_handle = service->end_handle;
        ESP_LOGI(TAG,
                 "service found: conn=%u profile=%s handles=%u-%u",
                 conn_handle,
                 controller_profile_config()->name,
                 service->start_handle,
                 service->end_handle);
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->controller_service_start_handle != 0U) {
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
            runtime->controller_char_val_handle = 0U;
            runtime->controller_write_char_val_handle = 0U;
            const int rc = ble_gattc_disc_all_chrs(conn_handle,
                                                   runtime->controller_service_start_handle,
                                                   runtime->controller_service_end_handle,
                                                   controller_chr_cb,
                                                   runtime);
            if (rc == 0) {
                return 0;
            }
            controller_fail_connection(runtime, conn_handle, "characteristic-start", rc);
            return 0;
        }
        if (controller_advance_profile(runtime)) {
            return 0;
        }
    }
    controller_fail_connection(runtime,
                               conn_handle,
                               "service",
                               error ? error->status : -1);
    return 0;
}

static esp_err_t controller_connect(esp_bms_idf_runtime_t *runtime,
                                    const struct ble_gap_disc_desc *disc)
{
    if (!runtime || !disc || runtime->controller_conn_handle != 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active()) {
        const int cancel_rc = ble_gap_disc_cancel();
        if (cancel_rc != 0) {
            ESP_LOGW(TAG, "scan cancel before connect failed: rc=%d", cancel_rc);
            return ESP_FAIL;
        }
    }
    /* 主动取消扫描不保证触发 DISC_COMPLETE，必须同步本地标记。 */
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    uint8_t own_addr_type = 0U;
    /* 远驱控制器的 BLE 模块对激进参数兼容性差：改用 30-50ms 间隔 + 长监督超时，
     * 避免订阅后因空包丢失而链路超时（reason=520 / first-frame-timeout）。
     * 部分型号连这套参数都拒绝，连接失败后由 GAP 回调换下一档重试。 */
    const struct ble_gap_conn_params *conn_params =
        &CONTROLLER_CONN_PARAMS[s_controller_conn_param_index];
    ESP_LOGI(TAG,
             "connect request: addr_type=%u stage=%u itvl=%u-%u timeout=%u",
             (unsigned)disc->addr.type,
             (unsigned)s_controller_conn_param_index,
             conn_params->itvl_min,
             conn_params->itvl_max,
             conn_params->supervision_timeout);
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc == 0) {
        rc = ble_gap_connect(own_addr_type,
                        &disc->addr,
                        CONTROLLER_CONNECT_TIMEOUT_MS,
                        conn_params,
                        controller_gap_event,
                        runtime);
    }
    if (rc != 0) {
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
        ESP_LOGW(TAG, "connect start failed: rc=%d addr_type=%u", rc, disc->addr.type);
        return ESP_FAIL;
    }
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_CONNECTING;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    return ESP_OK;
}

static esp_err_t controller_start_scan(esp_bms_idf_runtime_t *runtime);

static int controller_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            if (!runtime->controller_connection_enabled) {
                ESP_LOGI(TAG,
                         "connection rejected: conn=%u stage=disabled",
                         event->connect.conn_handle);
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            runtime->controller_conn_handle = event->connect.conn_handle;
            runtime->controller_keepalive_elapsed_ms = 0U;
            s_controller_first_frame_elapsed_ms = 0U;
            s_controller_poll_index = 0U;
            s_controller_profile = CONTROLLER_PROFILE_NONE;
            s_controller_cccd_value = 0U;
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            ESP_LOGI(TAG,
                     "GAP connected: conn=%u mac=%s name=%s",
                     event->connect.conn_handle,
                     runtime->controller_bound_mac,
                     runtime->controller_bound_name[0] != '\0' ? runtime->controller_bound_name : "-");
            /* 远驱（YuanQu）这类老式 BLE 4.0 控制器只支持 1M PHY：连接后请求 Coded PHY
             * 会让对端收到它不认识的 LL 控制 PDU，并以 BLE_ERR_UNSUPP_REM_FEATURE 拒绝
             * （日志里的 "controller Coded PHY request failed: conn=1 rc=538"），此后
             * 控制器不再推送任何数据帧，订阅必然走到 first-frame-timeout。
             * Windows 侧 BLE 栈从不发起该请求，这里对齐已验证可用的 PC 路径，
             * 控制器链路保持默认 1M PHY。 */
            const int mtu_rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
            ESP_LOGI(TAG, "controller MTU exchange submitted: rc=%d", mtu_rc);
            s_notify_diag_count = 0U;
            controller_begin_profile_walk(runtime);
        } else {
            ESP_LOGW(TAG,
                     "GAP connect failed: status=%d stage=%u",
                     event->connect.status,
                     (unsigned)s_controller_conn_param_index);
            /* 该档连接参数被对端拒绝：换下一档再试，避免固定参数把型号挡在门外 */
            s_controller_conn_param_index =
                (uint8_t)((s_controller_conn_param_index + 1U) %
                          (uint8_t)(sizeof(CONTROLLER_CONN_PARAMS) /
                                    sizeof(CONTROLLER_CONN_PARAMS[0])));
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            runtime->controller_keepalive_elapsed_ms = 0U;
            s_controller_first_frame_elapsed_ms = 0U;
            s_controller_poll_index = 0U;
            s_controller_profile = CONTROLLER_PROFILE_NONE;
            controller_clear_telemetry(runtime);
        }
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG,
                 "mtu update: conn=%u value=%u",
                 event->mtu.conn_handle,
                 event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG,
                 "conn update: conn=%u status=%d",
                 event->conn_update.conn_handle,
                 event->conn_update.status);
        return 0;
#if CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        if (event->phy_updated.conn_handle == runtime->controller_conn_handle &&
            event->phy_updated.status != 0) {
            ESP_LOGW(TAG, "Coded PHY unavailable: conn=%u status=%d",
                     event->phy_updated.conn_handle, event->phy_updated.status);
        }
        return 0;
#endif
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->controller_conn_handle) {
            ESP_LOGW(TAG,
                     "disconnected: conn=%u reason=%d profile=%s phase=%u subscribed=%u",
                     event->disconnect.conn.conn_handle,
                     event->disconnect.reason,
                     controller_profile_config()->name,
                     runtime->controller_ble_phase,
                     RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED) ? 1U : 0U);
            runtime->controller_conn_handle = 0xFFFFU;
            runtime->controller_service_start_handle = 0U;
            runtime->controller_service_end_handle = 0U;
            runtime->controller_cccd_handle = 0U;
            runtime->controller_char_val_handle = 0U;
            runtime->controller_write_char_val_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            runtime->controller_keepalive_elapsed_ms = 0U;
            s_controller_first_frame_elapsed_ms = 0U;
            s_controller_poll_index = 0U;
            s_controller_profile = CONTROLLER_PROFILE_NONE;
            controller_clear_telemetry(runtime);
            if (runtime->controller_connection_enabled &&
                RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
                const esp_err_t ret = controller_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "deferred rebind scan failed: %s", esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (s_notify_diag_count < 8U) {
            s_notify_diag_count++;
            ESP_LOGI(TAG,
                     "notify rx #%u: conn=%u handle=%u len=%d phase=%u subscribed=%u",
                     (unsigned)s_notify_diag_count,
                     event->notify_rx.conn_handle,
                     event->notify_rx.attr_handle,
                     OS_MBUF_PKTLEN(event->notify_rx.om),
                     runtime->controller_ble_phase,
                     RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED) ? 1U : 0U);
        }
        if (event->notify_rx.conn_handle == runtime->controller_conn_handle &&
            event->notify_rx.attr_handle == runtime->controller_char_val_handle &&
            (runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING ||
             runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_ONLINE) &&
            RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED)) {
            /* 型号之间的帧长与打包方式不同：按整段数据滑动解析，而不是只认 16 字节 */
            uint8_t frame[CONTROLLER_NOTIFY_BUFFER_LEN];
            const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len >= (int)ESP_FARDRIVER_FRAME_LEN && len <= (int)sizeof(frame) &&
                os_mbuf_copydata(event->notify_rx.om, 0, len, frame) == 0) {
                if (esp_fardriver_parse_frame(&runtime->controller_state,
                                              frame,
                                              (size_t)len)) {
                    if (s_notify_diag_count <= 8U) {
                        /* 型号之间校验方式不同：这里直接标出收到的是哪一套 */
                        ESP_LOGI(TAG,
                                 "frame accepted: len=%d index=%u checksum=%s",
                                 len,
                                 (unsigned)(frame[1] & 0x7FU),
                                 (frame[1] & 0x80U) != 0U ? "crc16" : "sum16");
                    }
                    if (runtime->controller_ble_phase ==
                            (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING &&
                        esp_fardriver_link_online(&runtime->controller_state)) {
                        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_ONLINE;
                        s_controller_first_frame_elapsed_ms = 0U;
                        __atomic_fetch_or(&runtime->pending_audio_events,
                                          ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED,
                                          __ATOMIC_RELAXED);
                        ESP_LOGI(TAG,
                                 "controller ready: conn=%u profile=%s stage=first-frame",
                                 event->notify_rx.conn_handle,
                                 controller_profile_config()->name);
                    }
                    controller_sync_parameters(runtime);
                    esp_bms_idf_runtime_project_controller_snapshot(runtime);
                }
            }
        }
        return 0;
    default:
        return 0;
    }
}

static void controller_store_candidate(esp_bms_idf_runtime_t *runtime,
                                       const char *mac,
                                       const char *name,
                                       int8_t rssi)
{
    if (!runtime || !mac || mac[0] == '\0') {
        return;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (name && name[0] != '\0') {
        controller_cache_name_locked(mac, name);
    } else {
        name = controller_cached_name_locked(mac);
    }
    bool changed = false;
    if (name && name[0] != '\0' && strcmp(mac, runtime->controller_bound_mac) == 0 &&
        strcmp(name, runtime->controller_bound_name) != 0) {
        controller_copy_text(runtime->controller_bound_name,
                             sizeof(runtime->controller_bound_name),
                             name);
        changed = true;
    }
    for (uint8_t index = 0U; index < runtime->controller_scan_candidate_count; ++index) {
        if (strcmp(runtime->controller_scan_candidates[index].mac, mac) == 0) {
            runtime->controller_scan_candidates[index].rssi = rssi;
            if (name && name[0] != '\0' &&
                (!runtime->controller_scan_candidates[index].has_name ||
                 strcmp(runtime->controller_scan_candidates[index].name, name) != 0)) {
                controller_copy_text(runtime->controller_scan_candidates[index].name,
                                     sizeof(runtime->controller_scan_candidates[index].name),
                                     name);
                runtime->controller_scan_candidates[index].has_name = true;
                changed = true;
            }
            if (runtime->bms_scan_lock) {
                xSemaphoreGive(runtime->bms_scan_lock);
            }
            if (changed) {
                esp_bms_idf_runtime_project_controller_snapshot(runtime);
            }
            return;
        }
    }
    if (runtime->controller_scan_candidate_count >= ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        return;
    }
    esp_bms_idf_bms_scan_candidate_t *candidate =
        &runtime->controller_scan_candidates[runtime->controller_scan_candidate_count++];
    controller_copy_text(candidate->mac, sizeof(candidate->mac), mac);
    controller_copy_text(candidate->name, sizeof(candidate->name), name);
    candidate->has_name = name && name[0] != '\0';
    candidate->rssi = rssi;
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static int controller_scan_gap_event(struct ble_gap_event *event, void *arg)
{
    static uint8_t debug_report_count;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_hs_adv_field *name_field = NULL;
        const struct ble_hs_adv_field *short_name_field = NULL;
        const int complete_name_rc = ble_hs_adv_find_field(BLE_HS_ADV_TYPE_COMP_NAME,
                                                            event->disc.data,
                                                            event->disc.length_data,
                                                            &name_field);
        const int short_name_rc = ble_hs_adv_find_field(BLE_HS_ADV_TYPE_INCOMP_NAME,
                                                         event->disc.data,
                                                         event->disc.length_data,
                                                         &short_name_field);
        if (complete_name_rc != 0) {
            name_field = short_name_field;
        }
        char mac[sizeof(runtime->controller_bound_mac)] = { 0 };
        char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U] = { 0 };
        controller_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = name_field && name_field->length > 1U &&
                              controller_name_copy(name,
                                                   sizeof(name),
                                                   name_field->value,
                                                   (size_t)name_field->length - 1U);
        const int8_t rssi = event->disc.rssi == 127 ? INT8_MIN : event->disc.rssi;
        if (debug_report_count < CONTROLLER_SCAN_DEBUG_REPORT_LIMIT) {
            ESP_LOGI(TAG,
                     "[controller-scan-debug] report=%u mac=%s event_type=%u addr_type=%u rssi=%d len=%u name_rc=%d/%d name=%s raw:",
                     (unsigned)debug_report_count + 1U,
                     mac,
                     (unsigned)event->disc.event_type,
                     (unsigned)event->disc.addr.type,
                     (int)rssi,
                     (unsigned)event->disc.length_data,
                     complete_name_rc,
                     short_name_rc,
                     has_name ? name : "-");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,
                                     event->disc.data,
                                     event->disc.length_data,
                                     ESP_LOG_INFO);
            debug_report_count++;
        }
        if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE)) {
            controller_store_candidate(runtime, mac, has_name ? name : NULL, rssi);
            if (runtime->controller_connection_enabled &&
                runtime->controller_bound_mac[0] != '\0' &&
                strcmp(mac, runtime->controller_bound_mac) == 0) {
                (void)controller_connect(runtime, &event->disc);
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG,
                 "[controller-scan-debug] complete logged=%u limit=%u",
                 (unsigned)debug_report_count,
                 (unsigned)CONTROLLER_SCAN_DEBUG_REPORT_LIMIT);
        debug_report_count = 0U;
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
            const esp_err_t ret = esp_bms_idf_runtime_resume_bms_scan(runtime);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "BMS scan handoff failed: %s", esp_err_to_name(ret));
            }
        }
        return 0;
    default:
        return 0;
    }
}

static esp_err_t controller_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    /* NimBLE owns one discovery callback: the most recent request wins. */
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    ESP_RETURN_ON_ERROR(esp_bms_idf_runtime_ensure_ble_host(runtime), TAG, "NimBLE init failed");
    if (!RUNTIME_FLAG(runtime, BLE_HOST_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
        return ESP_OK;
    }
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE) && ble_gap_disc_active()) {
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    if (ble_gap_disc_active()) {
        /* NimBLE has one global discovery callback; hand ownership to controller. */
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        (void)ble_gap_disc_cancel();
        ESP_LOGI(TAG, "BLE scan handoff requested: BMS -> controller");
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    runtime->controller_scan_candidate_count = 0U;
    memset(runtime->controller_scan_candidates, 0, sizeof(runtime->controller_scan_candidates));
    runtime->controller_scan_revision++;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, true);
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    const struct ble_gap_disc_params params = {
        .filter_duplicates = 0,
        .passive = 0,
        .filter_policy = 0,
        .limited = 0,
    };
    if (ble_gap_disc(own_addr_type,
                     CONTROLLER_SCAN_DURATION_MS,
                     &params,
                     controller_scan_gap_event,
                     runtime) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
    return ESP_OK;
}

static esp_err_t controller_start_if_enabled(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_connection_enabled ||
        runtime->controller_bound_mac[0] == '\0' ||
        runtime->controller_conn_handle != 0xFFFFU) {
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    return controller_start_scan(runtime);
}

static void controller_stop(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    runtime->controller_keepalive_elapsed_ms = 0U;
    s_controller_first_frame_elapsed_ms = 0U;
    s_controller_poll_index = 0U;
    if (runtime->controller_conn_handle != 0xFFFFU) {
        ESP_LOGI(TAG,
                 "connection stop: conn=%u profile=%s phase=%u",
                 runtime->controller_conn_handle,
                 controller_profile_config()->name,
                 runtime->controller_ble_phase);
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
        controller_clear_telemetry(runtime);
        (void)ble_gap_terminate(runtime->controller_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static void controller_on_ble_reset(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    runtime->controller_conn_handle = 0xFFFFU;
    runtime->controller_service_start_handle = 0U;
    runtime->controller_service_end_handle = 0U;
    runtime->controller_char_val_handle = 0U;
    runtime->controller_write_char_val_handle = 0U;
    runtime->controller_cccd_handle = 0U;
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
    runtime->controller_keepalive_elapsed_ms = 0U;
    s_controller_first_frame_elapsed_ms = 0U;
    s_controller_poll_index = 0U;
    s_controller_profile = CONTROLLER_PROFILE_NONE;
    RUNTIME_SET_FLAG(runtime,
                     CONTROLLER_SCAN_REQUESTED,
                     runtime->controller_connection_enabled && runtime->controller_bound_mac[0] != '\0');
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    controller_clear_telemetry(runtime);
}

static bool controller_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }
    bool changed = false;
    /* 宿主未同步时扫描会一直排队（NimBLE reset 后可能不再自动同步），
     * 超时后重建宿主栈，避免界面永久停在“连接中”。 */
    static uint32_t s_host_sync_wait_ms;
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED) &&
        !RUNTIME_FLAG(runtime, BLE_HOST_SYNCED)) {
        s_host_sync_wait_ms += elapsed_ms;
        if (s_host_sync_wait_ms >= CONTROLLER_HOST_SYNC_TIMEOUT_MS) {
            s_host_sync_wait_ms = 0U;
            ESP_LOGW(TAG, "BLE host not synced while a scan is pending; rebuilding host");
            const esp_err_t recover_ret = esp_bms_idf_runtime_recover_ble_host(runtime);
            if (recover_ret != ESP_OK) {
                ESP_LOGW(TAG, "BLE host rebuild failed: %s", esp_err_to_name(recover_ret));
            }
        }
    } else {
        s_host_sync_wait_ms = 0U;
    }
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED) && !ble_gap_disc_active()) {
        (void)controller_start_scan(runtime);
        changed = true;
    }
    const bool waiting_for_frame =
        runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING &&
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED);
    const bool online_and_active =
        (runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER ||
         runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD) &&
        runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_ONLINE &&
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED);
    if (waiting_for_frame) {
        if (elapsed_ms >= CONTROLLER_FIRST_FRAME_TIMEOUT_MS -
                              s_controller_first_frame_elapsed_ms) {
            /* 该档 profile 订阅成功却收不到帧：下次连接从下一档开始，
             * 避免同一个型号永远重试同一条假设。NUS -> FFE0 -> 全服务探测 -> NUS */
            s_controller_profile_start =
                s_controller_profile == CONTROLLER_PROFILE_NUS
                    ? CONTROLLER_PROFILE_FFE0
                    : (s_controller_profile == CONTROLLER_PROFILE_FFE0
                           ? CONTROLLER_PROFILE_DISCOVERED
                           : CONTROLLER_PROFILE_NUS);
            ESP_LOGW(TAG,
                     "first frame timeout: profile=%s next_stage=%u",
                     controller_profile_config()->name,
                     (unsigned)s_controller_profile_start);
            controller_fail_connection(runtime,
                                       runtime->controller_conn_handle,
                                       "first-frame-timeout",
                                       ESP_ERR_TIMEOUT);
            s_controller_first_frame_elapsed_ms = 0U;
            return true;
        }
        s_controller_first_frame_elapsed_ms += elapsed_ms;
    }
    if (waiting_for_frame || online_and_active) {
        runtime->controller_keepalive_elapsed_ms += elapsed_ms;
        const uint32_t period_ms = controller_profile_config()->read_polling
                                       ? CONTROLLER_READ_PERIOD_MS
                                       : (waiting_for_frame ? CONTROLLER_OPEN_RETRY_MS
                                                            : CONTROLLER_ONLINE_PERIOD_MS);
        if (runtime->controller_keepalive_elapsed_ms >= period_ms) {
            if (controller_profile_config()->read_polling) {
                controller_send_read_request(runtime);
            } else if (waiting_for_frame) {
                /* 首帧前轮流尝试：老开启指令 → App 的开启变体 → App 的辅助指令 → 轮询读包。
                 * 控制器固件只在其中一种上开始出数据，必须逐个试到。 */
                switch (s_controller_stream_step % 4U) {
                case 0U:
                    controller_send_open(runtime);
                    break;
                case 1U:
                    controller_send_stream_command(runtime);
                    break;
                case 2U:
                    controller_send_aux_command(runtime);
                    break;
                default:
                    controller_send_read_request(runtime);
                    break;
                }
                s_controller_stream_step++;
            } else if ((s_controller_online_step % 3U) == 0U) {
                /* 在线后保活，其余拍发轮询读包：只回读包不主动推流的型号也能刷新数据 */
                controller_send_keepalive(runtime);
                s_controller_online_step++;
            } else {
                controller_send_read_request(runtime);
                s_controller_online_step++;
            }
            runtime->controller_keepalive_elapsed_ms = 0U;
        }
    }
    return changed;
}

static const esp_bms_idf_runtime_controller_ble_driver_t s_controller_ble_driver = {
    .start_if_enabled = controller_start_if_enabled,
    .start_scan = controller_start_scan,
    .stop = controller_stop,
    .tick = controller_tick,
    .on_ble_reset = controller_on_ble_reset,
};

esp_err_t esp_bms_controller_ble_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_bms_idf_runtime_register_controller_ble_driver(runtime, &s_controller_ble_driver);
    return ESP_OK;
}

esp_err_t esp_bms_controller_ble_start(esp_bms_idf_runtime_t *runtime)
{
    return controller_start_if_enabled(runtime);
}
