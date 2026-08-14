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
#define CONTROLLER_READ_PERIOD_MS 200U
#define CONTROLLER_KEEPALIVE_PERIOD_MS 3000U
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
} controller_ble_phase_t;

typedef enum {
    CONTROLLER_PROFILE_NONE = 0,
    CONTROLLER_PROFILE_NUS = 1,
    CONTROLLER_PROFILE_FFE0 = 2,
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
};

static controller_profile_t s_controller_profile;
static uint8_t s_controller_poll_index;
static uint32_t s_controller_first_frame_elapsed_ms;

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

static void controller_send_command(esp_bms_idf_runtime_t *runtime,
                                    const uint8_t *command,
                                    size_t len)
{
    if (!runtime || !command || len == 0U || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_write_char_val_handle == 0U) {
        return;
    }
    const int rc = ble_gattc_write_no_rsp_flat(runtime->controller_conn_handle,
                                               runtime->controller_write_char_val_handle,
                                               command,
                                               len);
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
                                               request,
                                               sizeof(request));
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
        controller_send_open(runtime);
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
    const uint8_t value[2] = { 1U, 0U };
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
        if (ble_uuid_cmp(&chr->uuid.u, controller_profile_config()->notify_uuid) == 0 &&
            (chr->properties & BLE_GATT_CHR_F_NOTIFY) != 0) {
            runtime->controller_char_val_handle = chr->val_handle;
            ESP_LOGI(TAG,
                     "notify characteristic: profile=%s handle=%u properties=0x%02x",
                     controller_profile_config()->name,
                     chr->val_handle,
                     chr->properties);
        } else if (ble_uuid_cmp(&chr->uuid.u, controller_profile_config()->write_uuid) == 0 &&
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
        if (s_controller_profile == CONTROLLER_PROFILE_NUS) {
            ESP_LOGI(TAG, "service not found: profile=NUS, trying profile=FFE0");
            (void)controller_discover_profile(runtime, CONTROLLER_PROFILE_FFE0);
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
        (void)ble_gap_disc_cancel();
    }
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0 ||
        ble_gap_connect(own_addr_type,
                        &disc->addr,
                        CONTROLLER_CONNECT_TIMEOUT_MS,
                        NULL,
                        controller_gap_event,
                        runtime) != 0) {
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
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            ESP_LOGI(TAG,
                     "GAP connected: conn=%u mac=%s name=%s",
                     event->connect.conn_handle,
                     runtime->controller_bound_mac,
                     runtime->controller_bound_name[0] != '\0' ? runtime->controller_bound_name : "-");
            esp_bms_idf_runtime_request_coded_phy(event->connect.conn_handle, "controller");
            (void)controller_discover_profile(runtime, CONTROLLER_PROFILE_NUS);
        } else {
            ESP_LOGW(TAG, "GAP connect failed: status=%d", event->connect.status);
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
        if (event->notify_rx.conn_handle == runtime->controller_conn_handle &&
            event->notify_rx.attr_handle == runtime->controller_char_val_handle &&
            (runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING ||
             runtime->controller_ble_phase == (uint8_t)CONTROLLER_BLE_PHASE_ONLINE) &&
            RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED)) {
            uint8_t frame[ESP_FARDRIVER_FRAME_LEN];
            const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len == (int)sizeof(frame) &&
                os_mbuf_copydata(event->notify_rx.om, 0, len, frame) == 0) {
                if (esp_fardriver_parse_frame(&runtime->controller_state,
                                              frame,
                                              sizeof(frame))) {
                    if (runtime->controller_ble_phase ==
                            (uint8_t)CONTROLLER_BLE_PHASE_SUBSCRIBING &&
                        esp_fardriver_has_instrument_telemetry(&runtime->controller_state)) {
                        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_ONLINE;
                        s_controller_first_frame_elapsed_ms = 0U;
                        __atomic_fetch_or(&runtime->pending_audio_events,
                                          ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED,
                                          __ATOMIC_RELAXED);
                        ESP_LOGI(TAG,
                                 "controller ready: conn=%u profile=%s stage=first-telemetry",
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
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE)) {
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
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
                                       : CONTROLLER_KEEPALIVE_PERIOD_MS;
        if (runtime->controller_keepalive_elapsed_ms >= period_ms) {
            if (controller_profile_config()->read_polling) {
                controller_send_read_request(runtime);
            } else {
                controller_send_keepalive(runtime);
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
