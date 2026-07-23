#include "esp_bms_idf_runtime.h"

#include "esp_bms_lvgl_bridge.h"
#include "esp_bms_profile_hardware.h"
#if ESP_BMS_FEATURE_OTA
#include "esp_bms_ota.h"
#endif

#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "bms_idf_runtime";

#define BATTERY_GPIO ESP_BMS_PROFILE_BATTERY_ADC
#define BATTERY_SAMPLE_PERIOD_MS 2000U
#define BATTERY_ADC_MAX 4095U
#define BATTERY_REFERENCE_MV 3300U
#define BATTERY_DIVIDER_TOP_OHMS 100000U
#define BATTERY_DIVIDER_BOTTOM_OHMS 100000U
#define BMS_TELEMETRY_FRESHNESS_US INT64_C(2000000)
#define SETUP_AP_SSID_PREFIX "fuckingBms_"
#define SETUP_AP_SSID_SUFFIX_LEN 6U
#define SETUP_AP_PASSWORD_LEN 8U
#define SETUP_AP_NVS_NAMESPACE "esp_bms"
#define SETUP_AP_NVS_SSID_KEY "setup_ssid"
#define SETUP_AP_NVS_PASSWORD_KEY "setup_pw"
#define ANT_BMS_SERVICE_UUID_16 0xFFE0U
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xFFE1U
#define FARDRIVER_SERVICE_UUID_16 0xFFE0U
#define FARDRIVER_CHARACTERISTIC_UUID_16 0xFFECU
#define BMS_SCAN_DURATION_MS 10000
#define BMS_SCAN_HOST_TASK_STACK 4096U
#define BMS_SCAN_HOST_TASK_PRIORITY 5U
#define LOCAL_BLUETOOTH_NAME "ESP32 BMS GPS"
#define LOCAL_BLUETOOTH_ADV_INTERVAL_MS 500U
#define BMS_CONNECT_TIMEOUT_MS 10000
#define BMS_STATUS_POLL_PERIOD_MS 500U
#define BMS_HEARTBEAT_TIMEOUT_MS 5000U
#define BMS_RECONNECT_BACKOFF_MS 3000U
#define BMS_NVS_BOUND_MAC_KEY "bms_mac"
#define BMS_NVS_BOUND_NAME_KEY "bms_name"
#define DISPLAY_NVS_BRIGHTNESS_KEY "disp_bright"
#define DISPLAY_NVS_VOLUME_KEY "disp_vol"
#define DISPLAY_NVS_ROTATION_KEY "disp_rot"
#define DISPLAY_NVS_SPEED_UNIT_KEY "speed_unit"
#define DISPLAY_NVS_SPEED_SOURCE_KEY "speed_src"
#define DISPLAY_NVS_SPEED_STYLE_KEY "speed_style"
#define DISPLAY_NVS_BOOT_ANIMATION_KEY "boot_anim"
#define DISPLAY_NVS_LANGUAGE_KEY "lang"
#define DISPLAY_NVS_BMS_TYPE_KEY "bms_type"
#define DISPLAY_NVS_PRESET_RANGE_KEY "preset_rng"
#define CONTROLLER_NVS_CONNECTION_KEY "ctl_conn"
#define CONTROLLER_NVS_PAGE_KEY "ctl_page"
#define CONTROLLER_NVS_WHEEL_KEY "ctl_wheel"
#define CONTROLLER_NVS_RATIO_KEY "ctl_ratio"
#define CONTROLLER_NVS_RIM_KEY "ctl_rim"
#define CONTROLLER_NVS_ASPECT_KEY "ctl_aspect"
#define CONTROLLER_NVS_WIDTH_KEY "ctl_width"
#define CONTROLLER_NVS_BOUND_MAC_KEY "ctl_mac"
#define CONTROLLER_NVS_BOUND_NAME_KEY "ctl_name"
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
#define CONTROLLER_RATIO_CENTI_DEFAULT ESP_BMS_CONTROLLER_RATIO_CENTI_DEFAULT
#define HTTP_BODY_MAX_LEN 384U
#define HTTP_JSON_MAX_LEN 1024U
#define CAST_PROTOCOL_VERSION 1U
#define CAST_BLOCK_MAX_SIDE 16U
#define CAST_BLOCK_MAX_BYTES (CAST_BLOCK_MAX_SIDE * CAST_BLOCK_MAX_SIDE * 2U)
#define CAST_MESSAGE_MAX_BYTES (8U + CAST_BLOCK_MAX_BYTES)
#define CAST_HEARTBEAT_TIMEOUT_MS 5000U
#define CAST_TYPE_FRAME_BEGIN 1U
#define CAST_TYPE_RGB565_BLOCK 2U
#define CAST_TYPE_FRAME_END 3U
#define CAST_TYPE_HEARTBEAT 4U
#define CAST_TYPE_ACK 0x81U
#define CAST_FRAME_BEGIN_BYTES 7U
#define CAST_FRAME_END_BYTES 5U
#define CAST_BLOCK_HEADER_BYTES 7U

static uint16_t runtime_cast_width(const esp_bms_idf_runtime_t *runtime)
{
    return runtime->display_rotation == ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE ||
                   runtime->display_rotation == ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE
               ? 320U
               : 240U;
}

static uint16_t runtime_cast_height(const esp_bms_idf_runtime_t *runtime)
{
    return runtime_cast_width(runtime) == 320U ? 240U : 320U;
}

void esp_bms_idf_runtime_stop_cast(esp_bms_idf_runtime_t *runtime, const char *reason)
{
    if (__atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED)) {
        ESP_LOGI(TAG, "[cast] stopped: %s", reason);
    }
    __atomic_store_n(&runtime->cast_active, false, __ATOMIC_RELAXED);
    runtime->cast_frame_active = false;
    runtime->cast_socket_fd = -1;
    runtime->cast_sequence = 0U;
    runtime->cast_heartbeat_elapsed_ms = 0U;
}

static void runtime_log_heap_state(const char *stage)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "[heap] %s default_free=%u default_min=%u internal8_free=%u internal8_min=%u psram_free=%u psram_largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_free_size(psram_caps),
             (unsigned)heap_caps_get_largest_free_block(psram_caps));
}

void ble_store_config_init(void);

/* Controller compatibility still uses this numeric phase representation. */
typedef enum {
    BMS_BLE_PHASE_IDLE = 0,
    BMS_BLE_PHASE_SCANNING = 1,
    BMS_BLE_PHASE_CONNECTING = 2,
    BMS_BLE_PHASE_DISCOVERING_SERVICE = 3,
    BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC = 4,
    BMS_BLE_PHASE_DISCOVERING_CCCD = 5,
    BMS_BLE_PHASE_SUBSCRIBING = 6,
    BMS_BLE_PHASE_ONLINE = 7,
    BMS_BLE_PHASE_BACKOFF = 8,
} bms_ble_phase_t;

typedef struct {
    char mac[18];
    char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
} bms_scan_name_cache_entry_t;

static bms_scan_name_cache_entry_t s_bms_scan_name_cache[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
static uint8_t s_bms_scan_name_cache_count;
static uint8_t s_bms_scan_name_cache_next;

static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_init_ble_host(esp_bms_idf_runtime_t *runtime);
static void runtime_copy_snapshot_text(char *out, size_t out_len, const char *text);
static esp_err_t runtime_save_bms_binding(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_save_setup_ap_credentials(const esp_bms_idf_runtime_t *runtime);
static void runtime_ensure_setup_ap_credentials(esp_bms_idf_runtime_t *runtime);
static char runtime_hex_char(uint8_t value);
static void runtime_update_snapshot_speed(esp_bms_idf_runtime_t *runtime);

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))
#define RUNTIME_SNAPSHOT(runtime) ((runtime) ? &(runtime)->snapshot : NULL)
#define RUNTIME_SNAPSHOT_FLAG(runtime, name) \
    esp_bms_dashboard_snapshot_flag_get(RUNTIME_SNAPSHOT(runtime), ESP_BMS_DASHBOARD_FLAG_##name)
#define RUNTIME_SET_SNAPSHOT_FLAG(runtime, name, enabled) \
    esp_bms_dashboard_snapshot_flag_set(RUNTIME_SNAPSHOT(runtime), ESP_BMS_DASHBOARD_FLAG_##name, (enabled))
#define ACTION_EVENT_FLAG(event, name) \
    esp_bms_lvgl_action_event_flag_get((event), ESP_BMS_LVGL_ACTION_EVENT_FLAG_##name)

static esp_bms_idf_runtime_t *s_ble_host_runtime;

static bool runtime_controller_tire_matches_policy(uint8_t rim_inch,
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

static bool runtime_controller_ratio_matches_policy(uint16_t ratio_centi)
{
    return ratio_centi >= CONTROLLER_RATIO_CENTI_MIN &&
           ratio_centi <= CONTROLLER_RATIO_CENTI_MAX;
}

static void runtime_project_controller_snapshot(esp_bms_idf_runtime_t *runtime)
{
    esp_bms_dashboard_snapshot_t *snapshot = &runtime->snapshot;
    const esp_fardriver_state_t *state = &runtime->controller_state;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_CONNECTION_ENABLED, runtime->controller_connection_enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_PAGE_ENABLED, runtime->controller_page_enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_ONLINE, runtime->controller_conn_handle != 0xFFFFU);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_SPEED_VALID, state->speed_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_RPM_VALID, state->rpm_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_GEAR_VALID, state->gear_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_POWER_VALID, state->power_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_TEMP_VALID, state->controller_temp_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MOTOR_TEMP_VALID, state->motor_temp_valid);
    snapshot->controller_speed_deci_units = state->speed_deci_kmh;
    if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH && state->speed_valid) {
        snapshot->controller_speed_deci_units = (uint16_t)(((uint32_t)state->speed_deci_kmh * 621371U) / 1000000U);
    }
    snapshot->controller_rpm = state->rpm;
    snapshot->controller_gear = state->gear;
    snapshot->controller_power_w = state->power_w;
    snapshot->controller_temp_c = state->controller_temp_c;
    snapshot->motor_temp_c = state->motor_temp_c;
    snapshot->controller_fallback_tire_rim_inch = runtime->controller_fallback_tire_rim_inch;
    snapshot->controller_fallback_tire_aspect_percent =
        runtime->controller_fallback_tire_aspect_percent;
    snapshot->controller_fallback_tire_width_mm = runtime->controller_fallback_tire_width_mm;
    snapshot->controller_fallback_wheel_circumference_mm =
        state->fallback_wheel_circumference_mm;
    snapshot->controller_fallback_gear_ratio_centi = state->fallback_gear_ratio_centi;
    snapshot->controller_tire_rim_inch = 0U;
    snapshot->controller_tire_aspect_percent = 0U;
    snapshot->controller_tire_width_mm = 0U;
    snapshot->controller_wheel_circumference_mm = state->fallback_wheel_circumference_mm;
    snapshot->controller_gear_ratio_centi = state->fallback_gear_ratio_centi;
    snapshot->controller_scan_active = RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE) ? 1U : 0U;
    snapshot->controller_scan_revision = runtime->controller_scan_revision;
    snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_UNSET;
    if (state->controller_speed_params_valid) {
        snapshot->controller_tire_rim_inch = state->tire_rim_inch;
        snapshot->controller_tire_aspect_percent = state->tire_aspect_percent;
        snapshot->controller_tire_width_mm = state->tire_width_mm;
        snapshot->controller_wheel_circumference_mm = state->wheel_circumference_mm;
        snapshot->controller_gear_ratio_centi = state->gear_ratio_centi;
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_CONTROLLER;
    } else if (runtime_controller_tire_matches_policy(runtime->controller_fallback_tire_rim_inch,
                                                       runtime->controller_fallback_tire_aspect_percent,
                                                       runtime->controller_fallback_tire_width_mm)) {
        snapshot->controller_tire_rim_inch = runtime->controller_fallback_tire_rim_inch;
        snapshot->controller_tire_aspect_percent =
            runtime->controller_fallback_tire_aspect_percent;
        snapshot->controller_tire_width_mm = runtime->controller_fallback_tire_width_mm;
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LOCAL;
    } else if (state->fallback_wheel_circumference_mm > 0U) {
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LEGACY_WHEEL;
    }
    snapshot->controller_scan_candidate_count = runtime->controller_scan_candidate_count;
    memcpy(snapshot->controller_scan_candidates,
           runtime->controller_scan_candidates,
           sizeof(snapshot->controller_scan_candidates));
    runtime_copy_snapshot_text(snapshot->controller_bound_name,
                               sizeof(snapshot->controller_bound_name),
                               runtime->controller_bound_name);
    runtime_update_snapshot_speed(runtime);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY, true);
}

void esp_bms_idf_runtime_project_controller_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (runtime) {
        runtime_project_controller_snapshot(runtime);
    }
}

static void runtime_set_error(esp_bms_idf_runtime_t *runtime, const char *text)
{
    strncpy(runtime->snapshot.bms_error_text, text, sizeof(runtime->snapshot.bms_error_text) - 1);
    runtime->snapshot.bms_error_text[sizeof(runtime->snapshot.bms_error_text) - 1] = '\0';
}

static void runtime_copy_snapshot_text(char *out, size_t out_len, const char *text)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!text) {
        out[0] = '\0';
        return;
    }
    strncpy(out, text, out_len - 1);
    out[out_len - 1] = '\0';
}

static void runtime_set_bms_info(esp_bms_idf_runtime_t *runtime, const char *text)
{
    runtime_copy_snapshot_text(runtime->snapshot.bms_info_text,
                               sizeof(runtime->snapshot.bms_info_text),
                               text);
    runtime_set_error(runtime, text);
    RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, true);
}

static bool runtime_project_bluetooth_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }

    const bool enabled = true;
    const bool discoverable = RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) ||
                              RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING);
    const bool changed = RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED) != enabled ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING) != discoverable ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_CONNECTED) !=
                             RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) ||
                         strcmp(runtime->snapshot.bluetooth_name, runtime->bluetooth_name) != 0;

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED, enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING, discoverable);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_CONNECTED, RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED));
    runtime_copy_snapshot_text(runtime->snapshot.bluetooth_name,
                               sizeof(runtime->snapshot.bluetooth_name),
                               runtime->bluetooth_name);
    if (changed) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY, true);
    }
    return changed;
}

static void runtime_update_setup_ap_snapshot(esp_bms_idf_runtime_t *runtime)
{
    runtime_copy_snapshot_text(runtime->snapshot.setup_ap_ssid,
                               sizeof(runtime->snapshot.setup_ap_ssid),
                               runtime->setup_ap_ssid);
    runtime_copy_snapshot_text(runtime->snapshot.setup_ap_password,
                               sizeof(runtime->snapshot.setup_ap_password),
                               runtime->setup_ap_password);

    if (runtime->setup_ap_ssid[0] == '\0' || runtime->setup_ap_password[0] == '\0') {
        runtime->snapshot.setup_ap_qr_payload[0] = '\0';
        return;
    }

    const int written = snprintf(runtime->snapshot.setup_ap_qr_payload,
                                 sizeof(runtime->snapshot.setup_ap_qr_payload),
                                 "WIFI:S:%s;T:WPA;P:%s;;",
                                 runtime->setup_ap_ssid,
                                 runtime->setup_ap_password);
    if (written < 0 || (size_t)written >= sizeof(runtime->snapshot.setup_ap_qr_payload)) {
        runtime->snapshot.setup_ap_qr_payload[0] = '\0';
    }
}

static void runtime_generate_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    const unsigned long suffix = (unsigned long)(esp_random() & 0xFFFFFFU);
    (void)snprintf(runtime->setup_ap_ssid, sizeof(runtime->setup_ap_ssid),
                   SETUP_AP_SSID_PREFIX "%06lx", suffix);

    for (size_t index = 0; index < SETUP_AP_PASSWORD_LEN; index++) {
        runtime->setup_ap_password[index] = (char)('0' + (esp_random() % 10U));
    }
    runtime->setup_ap_password[SETUP_AP_PASSWORD_LEN] = '\0';
    runtime_update_setup_ap_snapshot(runtime);
}

static bool runtime_setup_ap_ssid_matches_policy(const char *ssid)
{
    const size_t prefix_len = strlen(SETUP_AP_SSID_PREFIX);
    const size_t ssid_len = strlen(ssid);
    if (ssid_len != prefix_len + SETUP_AP_SSID_SUFFIX_LEN ||
        memcmp(ssid, SETUP_AP_SSID_PREFIX, prefix_len) != 0) {
        return false;
    }

    for (size_t index = prefix_len; index < ssid_len; index++) {
        const char value = ssid[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool runtime_setup_ap_password_matches_policy(const char *password)
{
    if (strlen(password) != SETUP_AP_PASSWORD_LEN) {
        return false;
    }
    for (size_t index = 0; index < SETUP_AP_PASSWORD_LEN; index++) {
        if (password[index] < '0' || password[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool runtime_bms_name_copy(char *out, size_t out_len, const uint8_t *name, size_t name_len)
{
    if (!out || out_len == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!name || name_len == 0U) {
        return false;
    }

    size_t copied = 0;
    const size_t limit = name_len < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? name_len
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    for (size_t index = 0; index < limit && copied + 1U < out_len; index++) {
        const unsigned char value = name[index];
        if (value < 0x20U || value > 0x7EU || value == '"' || value == '\\') {
            break;
        }
        out[copied++] = (char)value;
    }
    out[copied] = '\0';
    return copied > 0U;
}

bool esp_bms_idf_runtime_bms_scan_project_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    uint8_t count = runtime->bms_scan_candidate_count;
    if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
    }

    const bool changed =
        runtime->snapshot.bms_scan_candidate_count != count ||
        memcmp(runtime->snapshot.bms_scan_candidates,
               runtime->bms_scan_candidates,
               sizeof(runtime->snapshot.bms_scan_candidates)) != 0;
    runtime->snapshot.bms_scan_candidate_count = count;
    memset(runtime->snapshot.bms_scan_candidates, 0, sizeof(runtime->snapshot.bms_scan_candidates));
    memcpy(runtime->snapshot.bms_scan_candidates,
           runtime->bms_scan_candidates,
           sizeof(runtime->snapshot.bms_scan_candidates[0]) * count);

    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    return changed;
}

void esp_bms_idf_runtime_bms_scan_clear_candidates(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    memset(runtime->bms_scan_candidates, 0, sizeof(runtime->bms_scan_candidates));
    runtime->bms_scan_candidate_count = 0;
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    (void)esp_bms_idf_runtime_bms_scan_project_snapshot(runtime);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
}

static size_t runtime_bms_scan_find_candidate(const esp_bms_idf_runtime_t *runtime, const char *mac)
{
    for (size_t index = 0; index < runtime->bms_scan_candidate_count; index++) {
        if (strcmp(runtime->bms_scan_candidates[index].mac, mac) == 0) {
            return index;
        }
    }
    return ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
}

static const char *runtime_bms_scan_cached_name_locked(const char *mac)
{
    for (uint8_t index = 0; index < s_bms_scan_name_cache_count; index++) {
        if (strcmp(s_bms_scan_name_cache[index].mac, mac) == 0) {
            return s_bms_scan_name_cache[index].name;
        }
    }
    return NULL;
}

static void runtime_bms_scan_cache_name_locked(const char *mac, const char *name)
{
    uint8_t index = 0;
    for (; index < s_bms_scan_name_cache_count; index++) {
        if (strcmp(s_bms_scan_name_cache[index].mac, mac) == 0) {
            break;
        }
    }
    if (index == s_bms_scan_name_cache_count) {
        if (s_bms_scan_name_cache_count < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            s_bms_scan_name_cache_count++;
        } else {
            index = s_bms_scan_name_cache_next++ % ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
    }
    runtime_copy_snapshot_text(s_bms_scan_name_cache[index].mac,
                               sizeof(s_bms_scan_name_cache[index].mac),
                               mac);
    runtime_copy_snapshot_text(s_bms_scan_name_cache[index].name,
                               sizeof(s_bms_scan_name_cache[index].name),
                               name);
}

void esp_bms_idf_runtime_bms_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
                                                   const char *mac,
                                                   const char *name,
                                                   int8_t rssi)
{
    if (!mac || mac[0] == '\0') {
        return;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (name && name[0] != '\0') {
        runtime_bms_scan_cache_name_locked(mac, name);
    } else {
        name = runtime_bms_scan_cached_name_locked(mac);
    }

    const bool bound_name_changed = name && name[0] != '\0' &&
                                    strcmp(mac, runtime->bms_bound_mac) == 0 &&
                                    strlen(name) > strlen(runtime->bms_bound_name);
    if (bound_name_changed) {
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   name);
        runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                                   sizeof(runtime->snapshot.bms_bound_name),
                                   runtime->bms_bound_name);
    }

    size_t index = runtime_bms_scan_find_candidate(runtime, mac);
    if (index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        const bool candidate_changed =
            bound_name_changed ||
            (name && name[0] != '\0' &&
             (!runtime->bms_scan_candidates[index].has_name ||
              strcmp(runtime->bms_scan_candidates[index].name, name) != 0));
        runtime->bms_scan_candidates[index].rssi = rssi;
        if (name && name[0] != '\0') {
            runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].name,
                                       sizeof(runtime->bms_scan_candidates[index].name),
                                       name);
            runtime->bms_scan_candidates[index].has_name = true;
        }
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        if (candidate_changed) {
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
        }
        return;
    }

    if (runtime->bms_scan_candidate_count < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        index = runtime->bms_scan_candidate_count++;
    } else {
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        return;
    }

    runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].mac,
                               sizeof(runtime->bms_scan_candidates[index].mac),
                               mac);
    runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].name,
                               sizeof(runtime->bms_scan_candidates[index].name),
                               name ? name : "");
    runtime->bms_scan_candidates[index].rssi = rssi;
    runtime->bms_scan_candidates[index].has_name = name && name[0] != '\0';
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
    ESP_LOGI(TAG,
             "[bms] scan candidate stored: count=%u mac=%s name=%s rssi=%d",
             (unsigned)runtime->bms_scan_candidate_count,
             mac,
             name && name[0] != '\0' ? name : "-",
             (int)rssi);
}

static void runtime_clear_bms_telemetry(esp_bms_idf_runtime_t *runtime)
{
    runtime->bms_telemetry_last_us = 0;
    runtime->trip_efficiency.anchor_valid = false;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID, false);
    runtime->snapshot.pack_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CURRENT_VALID, false);
    runtime->snapshot.current_deci_amps = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SOC_VALID, false);
    runtime->snapshot.soc_percent = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID, false);
    runtime->snapshot.min_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID, false);
    runtime->snapshot.average_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID, false);
    runtime->snapshot.max_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID, false);
    runtime->snapshot.delta_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID, false);
    runtime->snapshot.total_capacity_mah = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID, false);
    runtime->snapshot.capacity_remaining_mah = 0;
    runtime->snapshot.bms_protection_count = 0;
    memset(runtime->snapshot.bms_protection_codes, 0, sizeof(runtime->snapshot.bms_protection_codes));
    runtime->snapshot.bms_warning_count = 0;
    memset(runtime->snapshot.bms_warning_codes, 0, sizeof(runtime->snapshot.bms_warning_codes));
    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot, index, false);
    }
    memset(runtime->snapshot.bms_temperature_celsius, 0, sizeof(runtime->snapshot.bms_temperature_celsius));
    runtime_copy_snapshot_text(runtime->snapshot.bms_info_text,
                               sizeof(runtime->snapshot.bms_info_text),
                               "BMS OFF");
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + value - 'A';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + value - 'a';
    }
    return -1;
}

static char runtime_hex_char(uint8_t value)
{
    return value < 10U ? (char)('0' + value) : (char)('A' + (value - 10U));
}

static bool runtime_normalize_mac_text(const char *input, char *output, size_t output_len)
{
    if (!input || !output || output_len < 18U || strlen(input) != 17U) {
        return false;
    }

    size_t cursor = 0;
    for (size_t index = 0; index < 6U; index++) {
        const size_t base = index * 3U;
        const int high = hex_value(input[base]);
        const int low = hex_value(input[base + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        if (index < 5U && input[base + 2U] != ':') {
            return false;
        }
        if (index > 0) {
            output[cursor++] = ':';
        }
        output[cursor++] = runtime_hex_char((uint8_t)high);
        output[cursor++] = runtime_hex_char((uint8_t)low);
    }
    output[cursor] = '\0';
    return true;
}

static uint32_t runtime_battery_mv_from_raw(uint16_t raw)
{
    const uint64_t pin_mv = (uint64_t)raw * BATTERY_REFERENCE_MV / BATTERY_ADC_MAX;
    const uint64_t divider_total = BATTERY_DIVIDER_TOP_OHMS + BATTERY_DIVIDER_BOTTOM_OHMS;
    const uint64_t battery_mv = pin_mv * divider_total / BATTERY_DIVIDER_BOTTOM_OHMS;
    return battery_mv > UINT32_MAX ? UINT32_MAX : (uint32_t)battery_mv;
}

static uint16_t runtime_speed_deci_units(esp_bms_speed_unit_t unit, uint32_t speed_knots_milli)
{
    uint64_t speed = 0;
    if (unit == ESP_BMS_SPEED_UNIT_MPH) {
        speed = (uint64_t)speed_knots_milli * 11507795U / 1000000000U;
    } else {
        speed = (uint64_t)speed_knots_milli * 1852U / 100000U;
    }
    return speed > UINT16_MAX ? UINT16_MAX : (uint16_t)speed;
}

static void runtime_update_snapshot_speed(esp_bms_idf_runtime_t *runtime)
{
    esp_bms_dashboard_snapshot_t *snapshot = &runtime->snapshot;
    const bool controller_online = runtime->controller_connection_enabled &&
                                   runtime->controller_conn_handle != 0xFFFFU;
    const bool gps_available =
        snapshot->gps_module_state == (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE;
    snapshot->active_speed_source = esp_bms_speed_source_resolve(snapshot->speed_source,
                                                                 gps_available,
                                                                 controller_online);

    const bool speed_valid = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                                 ? controller_online && runtime->controller_state.speed_valid
                                 : gps_available &&
                                       RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SPEED_VALID, speed_valid);
    if (!speed_valid) {
        snapshot->speed_deci_units = 0U;
    } else if (snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER) {
        snapshot->speed_deci_units = runtime->controller_state.speed_deci_kmh;
        if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH) {
            snapshot->speed_deci_units =
                (uint16_t)(((uint32_t)runtime->controller_state.speed_deci_kmh * 621371U) /
                           1000000U);
        }
    } else {
        snapshot->speed_deci_units = runtime_speed_deci_units(snapshot->speed_unit,
                                                               runtime->gps_speed_knots_milli);
    }

    int32_t metric_consumption_deci_wh_per_km = 0;
    snapshot->average_consumption_valid =
        esp_bms_trip_efficiency_consumption(&runtime->trip_efficiency,
                                            false,
                                            &metric_consumption_deci_wh_per_km);
    if (!snapshot->average_consumption_valid) {
        snapshot->average_consumption_deci_wh_per_distance = 0;
    } else if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH) {
        (void)esp_bms_trip_efficiency_consumption(
            &runtime->trip_efficiency,
            true,
            &snapshot->average_consumption_deci_wh_per_distance);
    } else {
        snapshot->average_consumption_deci_wh_per_distance =
            metric_consumption_deci_wh_per_km;
    }

    snapshot->remaining_range_valid = esp_bms_remaining_range_km(
        snapshot->preset_range_km,
        RUNTIME_SNAPSHOT_FLAG(runtime, SOC_VALID),
        snapshot->soc_percent,
        snapshot->average_consumption_valid &&
            RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID) &&
            RUNTIME_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID),
        snapshot->pack_voltage_mv,
        snapshot->capacity_remaining_mah,
        metric_consumption_deci_wh_per_km,
        &snapshot->remaining_range_km);
    if (!snapshot->remaining_range_valid) {
        snapshot->remaining_range_km = 0U;
    }
}

#if ESP_BMS_FEATURE_GPS
bool esp_bms_idf_runtime_set_gps_module_state(esp_bms_idf_runtime_t *runtime,
                                              esp_bms_gps_module_state_t state,
                                              const char *reason)
{
    if (!runtime ||
        (uint32_t)state > (uint32_t)ESP_BMS_GPS_MODULE_UNAVAILABLE ||
        runtime->snapshot.gps_module_state == (uint8_t)state) {
        return false;
    }

    runtime->snapshot.gps_module_state = (uint8_t)state;
    if (state == ESP_BMS_GPS_MODULE_UNAVAILABLE) {
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, false);
        runtime->gps_speed_knots_milli = 0U;
        runtime->snapshot.gps_local_time_valid = false;
        runtime->snapshot.gps_local_date_valid = false;
    }
    runtime_update_snapshot_speed(runtime);

    if (state == ESP_BMS_GPS_MODULE_UNAVAILABLE) {
        ESP_LOGW(TAG, "[gps] module unavailable: reason=%s", reason ? reason : "unknown");
    } else if (state == ESP_BMS_GPS_MODULE_AVAILABLE) {
        ESP_LOGI(TAG, "[gps] module available: evidence=%s", reason ? reason : "protocol");
    } else {
        ESP_LOGI(TAG, "[gps] module probe started");
    }
    return true;
}

bool esp_bms_idf_runtime_publish_gps_sample(esp_bms_idf_runtime_t *runtime,
                                            bool fix_valid,
                                            uint32_t speed_knots_milli)
{
    if (!runtime) {
        return false;
    }

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, fix_valid);
    runtime->gps_speed_knots_milli = speed_knots_milli;
    runtime->snapshot.gps_sentences_seen++;

    const int64_t now_us = esp_timer_get_time();
    const int64_t bms_age_us = now_us - runtime->bms_telemetry_last_us;
    const bool bms_sample_valid = runtime->bms_telemetry_last_us > 0 && bms_age_us >= 0 &&
                                  bms_age_us <= BMS_TELEMETRY_FRESHNESS_US &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, BMS_ONLINE) &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID) &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, CURRENT_VALID);
    esp_bms_trip_efficiency_sample(&runtime->trip_efficiency,
                                   now_us,
                                   fix_valid,
                                   speed_knots_milli,
                                   bms_sample_valid,
                                   runtime->snapshot.pack_voltage_mv,
                                   runtime->snapshot.current_deci_amps);
    runtime_update_snapshot_speed(runtime);
    return true;
}

void esp_bms_idf_runtime_publish_gps_datetime(esp_bms_idf_runtime_t *runtime,
                                              uint16_t year,
                                              uint8_t month,
                                              uint8_t day,
                                              uint8_t hour,
                                              uint8_t minute,
                                              bool valid)
{
    if (!runtime) {
        return;
    }
    runtime->snapshot.gps_local_year = year;
    runtime->snapshot.gps_local_month = month;
    runtime->snapshot.gps_local_day = day;
    runtime->snapshot.gps_local_hour = hour;
    runtime->snapshot.gps_local_minute = minute;
    runtime->snapshot.gps_local_date_valid = valid;
    runtime->snapshot.gps_local_time_valid = valid;
}

bool esp_bms_idf_runtime_timeout_gps(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID)) {
        return false;
    }

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, false);
    runtime->gps_speed_knots_milli = 0U;
    runtime->snapshot.gps_local_time_valid = false;
    runtime->snapshot.gps_local_date_valid = false;
    esp_bms_trip_efficiency_sample(&runtime->trip_efficiency,
                                   esp_timer_get_time(),
                                   false,
                                   0U,
                                   false,
                                   0U,
                                   0);
    runtime_update_snapshot_speed(runtime);
    return true;
}
#endif

static esp_bms_idf_display_rotation_t runtime_next_rotation(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
    default:
        return ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT;
    }
}

static const char *runtime_rotation_text(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return "ROT POR";
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
        return "ROT LAN";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return "ROT IPOR";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
    default:
        return "ROT ILAN";
    }
}

static const char *runtime_rotation_config_text(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return "portrait";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return "inverted_portrait";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return "inverted_landscape";
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
    default:
        return "landscape";
    }
}

static bool runtime_parse_rotation_config_text(const char *text,
                                               esp_bms_idf_display_rotation_t *out_rotation)
{
    if (strcmp(text, "portrait") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT;
        return true;
    }
    if (strcmp(text, "landscape") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
        return true;
    }
    if (strcmp(text, "inverted_portrait") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT;
        return true;
    }
    if (strcmp(text, "inverted_landscape") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
        return true;
    }
    return false;
}

static const char *runtime_speed_unit_config_text(esp_bms_speed_unit_t unit)
{
    return unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h";
}

static bool runtime_parse_speed_unit_config_text(const char *text, esp_bms_speed_unit_t *out_unit)
{
    if (strcmp(text, "km/h") == 0) {
        *out_unit = ESP_BMS_SPEED_UNIT_KMH;
        return true;
    }
    if (strcmp(text, "mph") == 0) {
        *out_unit = ESP_BMS_SPEED_UNIT_MPH;
        return true;
    }
    return false;
}

static const char *runtime_speed_source_config_text(esp_bms_speed_source_t source)
{
    return source == ESP_BMS_SPEED_SOURCE_CONTROLLER ? "controller" : "gps";
}

static bool runtime_parse_speed_source_config_text(const char *text,
                                                   esp_bms_speed_source_t *out_source)
{
    if (strcmp(text, "gps") == 0) {
        *out_source = ESP_BMS_SPEED_SOURCE_GPS;
        return true;
    }
    if (strcmp(text, "controller") == 0) {
        *out_source = ESP_BMS_SPEED_SOURCE_CONTROLLER;
        return true;
    }
    return false;
}

static const char *runtime_bms_type_config_text(esp_bms_idf_bms_type_t type)
{
    switch (type) {
    case ESP_BMS_IDF_BMS_TYPE_JK:
        return "jk";
    case ESP_BMS_IDF_BMS_TYPE_JBD:
        return "jbd";
    case ESP_BMS_IDF_BMS_TYPE_DALY:
        return "daly";
    case ESP_BMS_IDF_BMS_TYPE_ANT:
    default:
        return "ant";
    }
}

static bool runtime_parse_bms_type_config_text(const char *text, esp_bms_idf_bms_type_t *out_type)
{
    if (strcmp(text, "ant") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_ANT;
        return true;
    }
    if (strcmp(text, "jk") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_JK;
        return true;
    }
    if (strcmp(text, "jbd") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_JBD;
        return true;
    }
    if (strcmp(text, "daly") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_DALY;
        return true;
    }
    return false;
}

static const char *runtime_bms_type_status_text(esp_bms_idf_bms_type_t type)
{
    switch (type) {
    case ESP_BMS_IDF_BMS_TYPE_JK:
        return "BMS JK";
    case ESP_BMS_IDF_BMS_TYPE_JBD:
        return "BMS JBD";
    case ESP_BMS_IDF_BMS_TYPE_DALY:
        return "BMS DALY";
    case ESP_BMS_IDF_BMS_TYPE_ANT:
    default:
        return "BMS ANT";
    }
}

static const char *runtime_wifi_config_text(esp_bms_wifi_state_t wifi)
{
    switch (wifi) {
    case ESP_BMS_WIFI_OFFLINE:
        return "offline";
    case ESP_BMS_WIFI_SETUP_AP:
    default:
        return "setup";
    }
}

static bool runtime_brightness_matches_policy(uint8_t brightness_percent)
{
    return brightness_percent >= 10U && brightness_percent <= 100U;
}

static bool runtime_set_brightness_percent(esp_bms_idf_runtime_t *runtime, uint8_t brightness_percent)
{
    if (!runtime || !runtime_brightness_matches_policy(brightness_percent)) {
        return false;
    }
    runtime->brightness_percent = brightness_percent;
    runtime->snapshot.brightness_percent = brightness_percent;
    return true;
}

static bool runtime_volume_matches_policy(uint8_t volume_percent)
{
    return volume_percent <= 100U;
}

static bool runtime_set_volume_percent(esp_bms_idf_runtime_t *runtime, uint8_t volume_percent)
{
    if (!runtime || !runtime_volume_matches_policy(volume_percent)) {
        return false;
    }
    runtime->volume_percent = volume_percent;
    runtime->snapshot.volume_percent = volume_percent;
    return true;
}

static bool runtime_rotation_matches_policy(uint8_t rotation)
{
    return rotation <= (uint8_t)ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
}

static bool runtime_speed_unit_matches_policy(uint8_t speed_unit)
{
    return speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_KMH ||
           speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_MPH;
}

static bool runtime_speed_source_matches_policy(uint8_t speed_source)
{
    return speed_source == (uint8_t)ESP_BMS_SPEED_SOURCE_GPS ||
           speed_source == (uint8_t)ESP_BMS_SPEED_SOURCE_CONTROLLER;
}

static bool runtime_speed_dashboard_style_matches_policy(int32_t style)
{
    return esp_bms_lvgl_ui_speed_dashboard_style_available(
        (esp_bms_speed_dashboard_style_t)style);
}

static bool runtime_boot_animation_style_matches_policy(int32_t style)
{
    return style >= (int32_t)ESP_BMS_BOOT_ANIMATION_CHARGE &&
           style <= (int32_t)ESP_BMS_BOOT_ANIMATION_GAUGE_SWEEP;
}

static bool runtime_language_matches_policy(uint8_t language)
{
    return language <= 1U;
}

static bool runtime_bms_type_matches_policy(uint8_t bms_type)
{
    return bms_type <= (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY;
}

static bool runtime_select_bms_type(esp_bms_idf_runtime_t *runtime, esp_bms_idf_bms_type_t bms_type)
{
    if (!runtime || !runtime_bms_type_matches_policy((uint8_t)bms_type)) {
        return false;
    }
    if (runtime->bms_type == (uint8_t)bms_type) {
        ESP_LOGI(TAG, "[bms] type unchanged: %s", runtime_bms_type_config_text(bms_type));
        return false;
    }

    runtime->bms_type = (uint8_t)bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime_set_error(runtime, runtime_bms_type_status_text(bms_type));
    ESP_LOGI(TAG, "[bms] type selected: %s", runtime_bms_type_config_text(bms_type));
    return true;
}

static bool runtime_json_write_u32_or_null(char *out, size_t out_len, bool valid, uint32_t value)
{
    const int written = valid ? snprintf(out, out_len, "%lu", (unsigned long)value)
                              : snprintf(out, out_len, "null");
    return written >= 0 && (size_t)written < out_len;
}

static bool runtime_json_write_i32_or_null(char *out, size_t out_len, bool valid, int32_t value)
{
    const int written = valid ? snprintf(out, out_len, "%ld", (long)value)
                              : snprintf(out, out_len, "null");
    return written >= 0 && (size_t)written < out_len;
}

static bool runtime_json_find_field(const char *body, const char *field, const char **out_value)
{
    char pattern[32] = { 0 };
    const int written = snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return false;
    }

    const char *cursor = strstr(body, pattern);
    if (!cursor) {
        return false;
    }
    cursor += strlen(pattern);
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != ':') {
        return false;
    }
    cursor++;
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    *out_value = cursor;
    return true;
}

static bool runtime_json_get_u8(const char *body, const char *field, uint8_t *out_value, bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) {
        return true;
    }

    uint32_t value = 0;
    bool seen_digit = false;
    while (*cursor >= '0' && *cursor <= '9') {
        seen_digit = true;
        value = (value * 10U) + (uint32_t)(*cursor - '0');
        if (value > UINT8_MAX) {
            return false;
        }
        cursor++;
    }
    return seen_digit ? ((*out_value = (uint8_t)value), true) : false;
}

static bool runtime_json_get_string(const char *body,
                                    const char *field,
                                    char *out_value,
                                    size_t out_len,
                                    bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) {
        return true;
    }
    if (*cursor != '"' || out_len == 0) {
        return false;
    }
    cursor++;

    size_t len = 0;
    while (*cursor && *cursor != '"') {
        if (*cursor == '\\' || (unsigned char)*cursor < 0x20U || len + 1U >= out_len) {
            return false;
        }
        out_value[len++] = *cursor++;
    }
    if (*cursor != '"') {
        return false;
    }
    out_value[len] = '\0';
    return true;
}

static void runtime_reset_state(esp_bms_idf_runtime_t *runtime)
{
    memset(&runtime->snapshot, 0, sizeof(runtime->snapshot));
    runtime->tick_count = 0;
    runtime->elapsed_ms = 0;
    runtime->battery_sample_elapsed_ms = 0;
    runtime->battery_samples_seen = 0;
    runtime->battery_read_failures = 0;
    runtime->gps_speed_knots_milli = 0;
    runtime->bms_telemetry_last_us = 0;
    runtime->bms_status_poll_elapsed_ms = 0;
    runtime->bms_frame_len = 0;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    runtime->bms_char_val_handle = 0;
    runtime->bms_cccd_handle = 0;
    runtime->bms_own_addr_type = 0;
    runtime->bluetooth_own_addr_type = 0;
    runtime->bluetooth_conn_handle = 0xFFFFU;
    runtime->controller_conn_handle = 0xFFFFU;
    runtime->controller_service_start_handle = 0;
    runtime->controller_service_end_handle = 0;
    runtime->controller_char_val_handle = 0;
    runtime->controller_cccd_handle = 0;
    runtime->controller_ble_phase = BMS_BLE_PHASE_IDLE;
    runtime->controller_keepalive_elapsed_ms = 0;
    runtime->controller_scan_revision = 0U;
    runtime->controller_connection_enabled = false;
    runtime->controller_page_enabled = false;
    runtime->controller_fallback_tire_rim_inch = 0U;
    runtime->controller_fallback_tire_aspect_percent = 0U;
    runtime->controller_fallback_tire_width_mm = 0U;
    runtime->controller_observed_tire_rim_inch = 0U;
    runtime->controller_observed_tire_aspect_percent = 0U;
    runtime->controller_observed_tire_width_mm = 0U;
    runtime->controller_observed_gear_ratio_centi = 0U;
    runtime->active_data_source = ESP_BMS_LVGL_DATA_SOURCE_BMS;
    esp_bms_trip_efficiency_reset(&runtime->trip_efficiency);
    memset(&runtime->controller_state, 0, sizeof(runtime->controller_state));
    runtime->controller_state.fallback_gear_ratio_centi = CONTROLLER_RATIO_CENTI_DEFAULT;
    runtime->bms_ble_phase = BMS_BLE_PHASE_IDLE;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, false);
    runtime_copy_snapshot_text(runtime->bluetooth_name,
                               sizeof(runtime->bluetooth_name),
                               LOCAL_BLUETOOTH_NAME);

    runtime->snapshot.speed_unit = ESP_BMS_SPEED_UNIT_KMH;
    runtime->snapshot.speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    runtime->snapshot.active_speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    runtime->snapshot.speed_dashboard_style = esp_bms_lvgl_ui_default_speed_dashboard_style();
#if ESP_BMS_FEATURE_GPS
    runtime->snapshot.gps_module_state = (uint8_t)ESP_BMS_GPS_MODULE_PROBING;
#else
    runtime->snapshot.gps_module_state = (uint8_t)ESP_BMS_GPS_MODULE_UNAVAILABLE;
#endif
    runtime->snapshot.boot_animation_style = (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    runtime->snapshot.preset_range_km = ESP_BMS_PRESET_RANGE_DEFAULT_KM;
    runtime->snapshot.uptime_seconds = 0U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
    runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
    runtime->bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    runtime->snapshot.bms_type = runtime->bms_type;
    (void)runtime_set_brightness_percent(runtime, 85U);
    (void)runtime_set_volume_percent(runtime, 65U);
    runtime->display_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, true);
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SETTINGS_SAVE_REQUESTED, false);
    (void)runtime_project_bluetooth_snapshot(runtime);
    esp_bms_idf_runtime_bms_scan_clear_candidates(runtime);
    runtime_clear_bms_telemetry(runtime);
    runtime_update_setup_ap_snapshot(runtime);
    runtime_set_bms_info(runtime, "BMS OFF");
    runtime_update_snapshot_speed(runtime);
    runtime_project_controller_snapshot(runtime);
}

static void runtime_init_battery_adc(esp_bms_idf_runtime_t *runtime)
{
    if (BATTERY_GPIO == GPIO_NUM_NC) {
        ESP_LOGI(TAG, "battery ADC is not configured for this profile");
        return;
    }
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_6;
    esp_err_t ret = adc_oneshot_io_to_channel(BATTERY_GPIO, &unit, &channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC GPIO%d is not usable: %s", BATTERY_GPIO, esp_err_to_name(ret));
        return;
    }
    if (unit != ADC_UNIT_1) {
        ESP_LOGW(TAG, "battery ADC GPIO%d resolved to ADC unit %d, expected ADC1", BATTERY_GPIO, unit + 1);
        return;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_config, &runtime->battery_adc);
    if (ret != ESP_OK) {
        runtime->battery_adc = NULL;
        ESP_LOGW(TAG, "battery ADC unit init failed: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(runtime->battery_adc, channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC channel config failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_del_unit(runtime->battery_adc));
        runtime->battery_adc = NULL;
        return;
    }

    runtime->battery_adc_channel = channel;
    RUNTIME_SET_FLAG(runtime, BATTERY_ADC_READY, true);
    ESP_LOGI(TAG, "battery ADC ready: gpio=%d unit=ADC1 channel=%d", BATTERY_GPIO, channel);
}

static esp_err_t runtime_init_nvs(esp_bms_idf_runtime_t *runtime)
{
    if (runtime && RUNTIME_FLAG(runtime, NVS_READY)) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK && runtime) {
        RUNTIME_SET_FLAG(runtime, NVS_READY, true);
    }
    return ret;
}

static esp_err_t runtime_nvs_get_optional_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    const esp_err_t ret = nvs_get_u8(handle, key, value);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t runtime_nvs_get_optional_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    const esp_err_t ret = nvs_get_u16(handle, key, value);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t runtime_nvs_get_optional_string(nvs_handle_t handle,
                                                 const char *key,
                                                 char *value,
                                                 size_t value_len)
{
    size_t len = value_len;
    const esp_err_t ret = nvs_get_str(handle, key, value, &len);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

esp_err_t esp_bms_idf_runtime_load_display_settings(esp_bms_idf_runtime_t *runtime, bool *loaded)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_FALSE(loaded, ESP_ERR_INVALID_ARG, TAG, "loaded output is required");
    *loaded = false;

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t brightness_percent = 0;
    uint8_t volume_percent = 0;
    uint8_t rotation = 0;
    uint8_t speed_unit = 0;
    uint8_t speed_source = (uint8_t)ESP_BMS_SPEED_SOURCE_GPS;
    uint8_t speed_dashboard_style = 0;
    uint8_t boot_animation_style = (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    uint8_t language = 0;
    uint8_t bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    uint8_t controller_connection_enabled = 0;
    uint8_t legacy_controller_page_enabled = 0;
    uint8_t controller_tire_rim_inch = 0;
    uint8_t controller_tire_aspect_percent = 0;
    uint16_t controller_tire_width_mm = 0;
    uint16_t controller_wheel_mm = 0;
    uint16_t controller_ratio_centi = 0;
    uint16_t preset_range_km = ESP_BMS_PRESET_RANGE_DEFAULT_KM;
    bool speed_source_migration_needed = false;
    bool preset_range_migration_needed = false;
    bool dashboard_style_migration_needed = false;

    ret = nvs_get_u8(handle, DISPLAY_NVS_BRIGHTNESS_KEY, &brightness_percent);
    if (ret == ESP_OK) {
        const esp_err_t volume_ret = nvs_get_u8(handle, DISPLAY_NVS_VOLUME_KEY, &volume_percent);
        if (volume_ret == ESP_ERR_NVS_NOT_FOUND) {
            volume_percent = runtime->volume_percent;
        } else {
            ret = volume_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_ROTATION_KEY, &rotation);
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_SPEED_UNIT_KEY, &speed_unit);
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, &language);
    }
    if (ret == ESP_OK) {
        const esp_err_t bms_type_ret = nvs_get_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, &bms_type);
        if (bms_type_ret != ESP_ERR_NVS_NOT_FOUND) {
            ret = bms_type_ret;
        }
    }
    if (ret == ESP_OK) {
        const esp_err_t preset_range_ret = nvs_get_u16(handle,
                                                       DISPLAY_NVS_PRESET_RANGE_KEY,
                                                       &preset_range_km);
        if (preset_range_ret == ESP_ERR_NVS_NOT_FOUND) {
            preset_range_migration_needed = true;
        } else {
            ret = preset_range_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_CONNECTION_KEY,
                                          &controller_connection_enabled);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_PAGE_KEY,
                                          &legacy_controller_page_enabled);
    }
    if (ret == ESP_OK) {
        const esp_err_t source_ret = nvs_get_u8(handle,
                                                DISPLAY_NVS_SPEED_SOURCE_KEY,
                                                &speed_source);
        if (source_ret == ESP_ERR_NVS_NOT_FOUND) {
            speed_source = legacy_controller_page_enabled != 0U
                               ? (uint8_t)ESP_BMS_SPEED_SOURCE_CONTROLLER
                               : (uint8_t)ESP_BMS_SPEED_SOURCE_GPS;
            speed_source_migration_needed = true;
        } else {
            ret = source_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle,
                                          DISPLAY_NVS_SPEED_STYLE_KEY,
                                          &speed_dashboard_style);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle,
                                          DISPLAY_NVS_BOOT_ANIMATION_KEY,
                                          &boot_animation_style);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_WHEEL_KEY,
                                           &controller_wheel_mm);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_RATIO_KEY,
                                           &controller_ratio_centi);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_RIM_KEY,
                                          &controller_tire_rim_inch);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_ASPECT_KEY,
                                          &controller_tire_aspect_percent);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_WIDTH_KEY,
                                           &controller_tire_width_mm);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_string(handle, CONTROLLER_NVS_BOUND_MAC_KEY,
                                              runtime->controller_bound_mac,
                                              sizeof(runtime->controller_bound_mac));
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_string(handle, CONTROLLER_NVS_BOUND_NAME_KEY,
                                              runtime->controller_bound_name,
                                              sizeof(runtime->controller_bound_name));
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_speed_dashboard_style_matches_policy(speed_dashboard_style)) {
        ESP_LOGW(TAG,
                 "[display] saved dashboard style %u is unavailable; using the configured default",
                 speed_dashboard_style);
        speed_dashboard_style = (uint8_t)esp_bms_lvgl_ui_default_speed_dashboard_style();
        dashboard_style_migration_needed = true;
    }

    if (!runtime_brightness_matches_policy(brightness_percent) ||
        !runtime_volume_matches_policy(volume_percent) ||
        !runtime_rotation_matches_policy(rotation) ||
        !runtime_speed_unit_matches_policy(speed_unit) ||
        !runtime_speed_source_matches_policy(speed_source) ||
        !runtime_language_matches_policy(language) ||
        !runtime_bms_type_matches_policy(bms_type) ||
        preset_range_km > ESP_BMS_REMAINING_RANGE_MAX_KM ||
        controller_connection_enabled > 1U || legacy_controller_page_enabled > 1U ||
        !runtime_boot_animation_style_matches_policy(boot_animation_style)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (controller_wheel_mm != 0U &&
        (controller_wheel_mm < 500U || controller_wheel_mm > 4000U)) {
        ESP_LOGW(TAG, "[controller] ignored invalid legacy circumference: %u",
                 controller_wheel_mm);
        controller_wheel_mm = 0U;
    }
    if (!runtime_controller_ratio_matches_policy(controller_ratio_centi)) {
        if (controller_ratio_centi != 0U) {
            ESP_LOGW(TAG, "[controller] ignored invalid saved ratio: %u", controller_ratio_centi);
        }
        controller_ratio_centi = CONTROLLER_RATIO_CENTI_DEFAULT;
    }
    const bool tire_fields_present = controller_tire_rim_inch != 0U ||
                                     controller_tire_aspect_percent != 0U ||
                                     controller_tire_width_mm != 0U;
    if (runtime_controller_tire_matches_policy(controller_tire_rim_inch,
                                               controller_tire_aspect_percent,
                                               controller_tire_width_mm)) {
        uint16_t calculated_wheel_mm = 0U;
        if (esp_fardriver_tire_circumference_mm(controller_tire_rim_inch,
                                                controller_tire_aspect_percent,
                                                controller_tire_width_mm,
                                                &calculated_wheel_mm)) {
            controller_wheel_mm = calculated_wheel_mm;
        }
    } else {
        if (tire_fields_present) {
            ESP_LOGW(TAG,
                     "[controller] ignored incomplete/invalid saved tire: %u-%u-%u",
                     controller_tire_rim_inch,
                     controller_tire_aspect_percent,
                     controller_tire_width_mm);
        }
        controller_tire_rim_inch = 0U;
        controller_tire_aspect_percent = 0U;
        controller_tire_width_mm = 0U;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = (esp_bms_idf_display_rotation_t)rotation;
    runtime->snapshot.speed_unit = (esp_bms_speed_unit_t)speed_unit;
    runtime->snapshot.speed_source = (esp_bms_speed_source_t)speed_source;
    runtime->snapshot.speed_dashboard_style =
        (esp_bms_speed_dashboard_style_t)speed_dashboard_style;
    runtime->snapshot.boot_animation_style = boot_animation_style;
    runtime->snapshot.preset_range_km = preset_range_km;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, language != 0U);
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime->controller_page_enabled =
        runtime->snapshot.speed_dashboard_style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
    runtime->controller_connection_enabled = controller_connection_enabled != 0U;
    runtime->controller_fallback_tire_rim_inch = controller_tire_rim_inch;
    runtime->controller_fallback_tire_aspect_percent = controller_tire_aspect_percent;
    runtime->controller_fallback_tire_width_mm = controller_tire_width_mm;
    runtime->controller_state.fallback_wheel_circumference_mm = controller_wheel_mm;
    runtime->controller_state.fallback_gear_ratio_centi = controller_ratio_centi;
    runtime_project_controller_snapshot(runtime);
    runtime_update_snapshot_speed(runtime);
    *loaded = true;
    if (speed_source_migration_needed || preset_range_migration_needed ||
        dashboard_style_migration_needed) {
        const esp_err_t migration_ret = esp_bms_idf_runtime_save_display_settings(runtime);
        if (migration_ret != ESP_OK) {
            ESP_LOGW(TAG, "[settings] migration save failed: %s",
                     esp_err_to_name(migration_ret));
        }
    }
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_FALSE(runtime_brightness_matches_policy(runtime->brightness_percent),
                        ESP_ERR_INVALID_STATE, TAG, "invalid brightness");
    ESP_RETURN_ON_FALSE(runtime_volume_matches_policy(runtime->volume_percent),
                        ESP_ERR_INVALID_STATE, TAG, "invalid volume");
    ESP_RETURN_ON_FALSE(runtime_rotation_matches_policy((uint8_t)runtime->display_rotation),
                        ESP_ERR_INVALID_STATE, TAG, "invalid display rotation");
    ESP_RETURN_ON_FALSE(runtime_speed_unit_matches_policy((uint8_t)runtime->snapshot.speed_unit),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed unit");
    ESP_RETURN_ON_FALSE(runtime_speed_source_matches_policy(
                            (uint8_t)runtime->snapshot.speed_source),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed source");
    ESP_RETURN_ON_FALSE(runtime_speed_dashboard_style_matches_policy(
                            runtime->snapshot.speed_dashboard_style),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed dashboard style");
    ESP_RETURN_ON_FALSE(runtime_boot_animation_style_matches_policy(
                            runtime->snapshot.boot_animation_style),
                        ESP_ERR_INVALID_STATE, TAG, "invalid boot animation style");
    ESP_RETURN_ON_FALSE(runtime_bms_type_matches_policy(runtime->bms_type),
                        ESP_ERR_INVALID_STATE, TAG, "invalid BMS type");
    ESP_RETURN_ON_FALSE(runtime->snapshot.preset_range_km <= ESP_BMS_REMAINING_RANGE_MAX_KM,
                        ESP_ERR_INVALID_STATE, TAG, "invalid preset range");
    ESP_RETURN_ON_FALSE(runtime_controller_ratio_matches_policy(
                            runtime->controller_state.fallback_gear_ratio_centi),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller ratio");
    const bool tire_unset = runtime->controller_fallback_tire_rim_inch == 0U &&
                            runtime->controller_fallback_tire_aspect_percent == 0U &&
                            runtime->controller_fallback_tire_width_mm == 0U;
    ESP_RETURN_ON_FALSE(tire_unset ||
                            runtime_controller_tire_matches_policy(
                                runtime->controller_fallback_tire_rim_inch,
                                runtime->controller_fallback_tire_aspect_percent,
                                runtime->controller_fallback_tire_width_mm),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller tire");
    ESP_RETURN_ON_FALSE(runtime->controller_state.fallback_wheel_circumference_mm == 0U ||
                            (runtime->controller_state.fallback_wheel_circumference_mm >= 500U &&
                             runtime->controller_state.fallback_wheel_circumference_mm <= 4000U),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller circumference");

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(handle, DISPLAY_NVS_BRIGHTNESS_KEY, runtime->brightness_percent);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_VOLUME_KEY, runtime->volume_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_ROTATION_KEY, (uint8_t)runtime->display_rotation);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_SPEED_UNIT_KEY, (uint8_t)runtime->snapshot.speed_unit);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_SPEED_SOURCE_KEY,
                         (uint8_t)runtime->snapshot.speed_source);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_SPEED_STYLE_KEY,
                         (uint8_t)runtime->snapshot.speed_dashboard_style);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_BOOT_ANIMATION_KEY,
                         runtime->snapshot.boot_animation_style);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, runtime->bms_type);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle,
                          DISPLAY_NVS_PRESET_RANGE_KEY,
                          runtime->snapshot.preset_range_km);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_CONNECTION_KEY,
                         runtime->controller_connection_enabled ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_PAGE_KEY,
                         runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_WHEEL_KEY,
                          runtime->controller_state.fallback_wheel_circumference_mm);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_RATIO_KEY,
                          runtime->controller_state.fallback_gear_ratio_centi);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_RIM_KEY,
                         runtime->controller_fallback_tire_rim_inch);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_ASPECT_KEY,
                         runtime->controller_fallback_tire_aspect_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_WIDTH_KEY,
                          runtime->controller_fallback_tire_width_mm);
    }
    if (ret == ESP_OK && runtime->controller_bound_mac[0] != '\0') {
        ret = nvs_set_str(handle, CONTROLLER_NVS_BOUND_MAC_KEY, runtime->controller_bound_mac);
    }
    if (ret == ESP_OK && runtime->controller_bound_name[0] != '\0') {
        ret = nvs_set_str(handle, CONTROLLER_NVS_BOUND_NAME_KEY, runtime->controller_bound_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool runtime_set_pending_http_config(esp_bms_idf_runtime_t *runtime,
                                            uint8_t brightness_percent,
                                            uint8_t volume_percent,
                                            esp_bms_idf_display_rotation_t rotation,
                                            esp_bms_speed_unit_t speed_unit,
                                            esp_bms_speed_source_t speed_source,
                                            bool language_zh,
                                            esp_bms_idf_bms_type_t bms_type)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    runtime->http_pending_brightness_percent = brightness_percent;
    runtime->http_pending_volume_percent = volume_percent;
    runtime->http_pending_display_rotation = rotation;
    runtime->http_pending_speed_unit = speed_unit;
    runtime->http_pending_speed_source = speed_source;
    RUNTIME_SET_FLAG(runtime, HTTP_PENDING_LANGUAGE_ZH, language_zh);
    runtime->http_pending_bms_type = (uint8_t)bms_type;
    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_PENDING, true);

    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

bool esp_bms_idf_runtime_apply_pending_http_config(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }

    const bool pending = RUNTIME_FLAG(runtime, HTTP_CONFIG_PENDING);
    const uint8_t brightness_percent = runtime->http_pending_brightness_percent;
    const uint8_t volume_percent = runtime->http_pending_volume_percent;
    const esp_bms_idf_display_rotation_t rotation = runtime->http_pending_display_rotation;
    const esp_bms_speed_unit_t speed_unit = runtime->http_pending_speed_unit;
    const esp_bms_speed_source_t speed_source = runtime->http_pending_speed_source;
    const bool language_zh = RUNTIME_FLAG(runtime, HTTP_PENDING_LANGUAGE_ZH);
    const uint8_t bms_type = runtime->http_pending_bms_type;
    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_PENDING, false);
    xSemaphoreGive(runtime->http_pending_lock);

    if (!pending) {
        return false;
    }

    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);

    const bool changed = runtime->brightness_percent != brightness_percent ||
                         runtime->volume_percent != volume_percent ||
                         runtime->display_rotation != rotation ||
                         runtime->snapshot.speed_unit != speed_unit ||
                         runtime->snapshot.speed_source != speed_source ||
                         RUNTIME_FLAG(runtime, LANGUAGE_ZH) != language_zh ||
                         runtime->bms_type != bms_type;
    if (!changed) {
        ESP_LOGI(TAG, "[http] config consumed without value changes");
        return true;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = rotation;
    runtime->snapshot.speed_unit = speed_unit;
    runtime->snapshot.speed_source = speed_source;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, language_zh);
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime_project_controller_snapshot(runtime);
    runtime_set_error(runtime, "HTTP CFG");

    const esp_err_t save_ret = esp_bms_idf_runtime_save_display_settings(runtime);
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "[http] display setting save failed: %s", esp_err_to_name(save_ret));
    }
    return true;
}

static esp_err_t runtime_http_set_common_headers(httpd_req_t *req)
{
    esp_err_t ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req,
                                 "Access-Control-Allow-Headers",
                                 "Content-Type, X-Firmware-Code");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    }
    return ret;
}

static esp_err_t runtime_http_send_text(httpd_req_t *req, const char *status, const char *text)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, status), TAG, "set HTTP status failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t runtime_http_send_no_content(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "204 No Content"), TAG, "set HTTP status failed");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t runtime_http_send_json(httpd_req_t *req, const char *json)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool runtime_json_write_bms_codes(char *out,
                                         size_t out_len,
                                         const char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],
                                         uint8_t count)
{
    if (!out || out_len == 0U) {
        return false;
    }

    size_t offset = 0;
    int written = snprintf(out, out_len, "[");
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    offset = (size_t)written;

    const uint8_t safe_count = count > ESP_BMS_BMS_CODE_MAX_COUNT ? ESP_BMS_BMS_CODE_MAX_COUNT : count;
    for (uint8_t index = 0; index < safe_count; index++) {
        written = snprintf(out + offset,
                           out_len - offset,
                           "%s\"%s\"",
                           index == 0U ? "" : ",",
                           codes[index]);
        if (written < 0 || (size_t)written >= out_len - offset) {
            return false;
        }
        offset += (size_t)written;
    }

    written = snprintf(out + offset, out_len - offset, "]");
    return written >= 0 && (size_t)written < out_len - offset;
}

static bool runtime_json_write_bms_temperatures(char *out,
                                                size_t out_len,
                                                const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!out || out_len == 0U || !snapshot) {
        return false;
    }

    size_t offset = 0;
    int written = snprintf(out, out_len, "[");
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    offset = (size_t)written;

    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; index++) {
        if (esp_bms_dashboard_snapshot_temperature_valid(snapshot, index)) {
            written = snprintf(out + offset,
                               out_len - offset,
                               "%s%d",
                               index == 0U ? "" : ",",
                               (int)snapshot->bms_temperature_celsius[index]);
        } else {
            written = snprintf(out + offset, out_len - offset, "%snull", index == 0U ? "" : ",");
        }
        if (written < 0 || (size_t)written >= out_len - offset) {
            return false;
        }
        offset += (size_t)written;
    }

    written = snprintf(out + offset, out_len - offset, "]");
    return written >= 0 && (size_t)written < out_len - offset;
}

static esp_err_t runtime_http_status_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char pack_voltage[16] = { 0 };
    char current[16] = { 0 };
    char soc[16] = { 0 };
    char local_battery[16] = { 0 };
    char protections[96] = { 0 };
    char warnings[96] = { 0 };
    char temperatures[80] = { 0 };
    if (!runtime_json_write_u32_or_null(pack_voltage,
                                        sizeof(pack_voltage),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID),
                                        runtime->snapshot.pack_voltage_mv) ||
        !runtime_json_write_i32_or_null(current,
                                        sizeof(current),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, CURRENT_VALID),
                                        runtime->snapshot.current_deci_amps) ||
        !runtime_json_write_u32_or_null(soc,
                                        sizeof(soc),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, SOC_VALID),
                                        runtime->snapshot.soc_percent) ||
        !runtime_json_write_u32_or_null(local_battery,
                                        sizeof(local_battery),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID),
                                        runtime->snapshot.local_battery_mv) ||
        !runtime_json_write_bms_codes(protections,
                                      sizeof(protections),
                                      runtime->snapshot.bms_protection_codes,
                                      runtime->snapshot.bms_protection_count) ||
        !runtime_json_write_bms_codes(warnings,
                                      sizeof(warnings),
                                      runtime->snapshot.bms_warning_codes,
                                      runtime->snapshot.bms_warning_count) ||
        !runtime_json_write_bms_temperatures(temperatures, sizeof(temperatures), &runtime->snapshot)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json format error");
    }

    char speed[16] = "--";
    if (RUNTIME_SNAPSHOT_FLAG(runtime, SPEED_VALID)) {
        const unsigned value = runtime->snapshot.speed_deci_units;
        (void)snprintf(speed, sizeof(speed), "%u.%u", value / 10U, value % 10U);
    }

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"version\":\"0.1.0\",\"speed\":\"%s\",\"speed_unit\":\"%s\","
                                 "\"gps_fix\":%s,\"bms\":\"%s\",\"pack_voltage_mv\":%s,"
                                 "\"current_deci_amps\":%s,\"soc_percent\":%s,"
                                 "\"local_battery_mv\":%s,\"bms_info\":\"%s\","
                                 "\"bms_protections\":%s,\"bms_warnings\":%s,"
                                 "\"bms_temperatures_c\":%s,\"wifi\":\"%s\","
                                 "\"setup_ap_enabled\":%s}",
                                 speed,
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID) ? "true" : "false",
                                 RUNTIME_SNAPSHOT_FLAG(runtime, BMS_ONLINE) ? "online" : "offline",
                                 pack_voltage,
                                 current,
                                 soc,
                                 local_battery,
                                 runtime->snapshot.bms_info_text[0] != '\0' ? runtime->snapshot.bms_info_text : "BMS OFF",
                                 protections,
                                 warnings,
                                 temperatures,
                                 runtime_wifi_config_text(runtime->snapshot.wifi),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED) ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
}

static esp_err_t runtime_http_bms_candidates_handler(httpd_req_t *req,
                                                     esp_bms_idf_runtime_t *runtime)
{
    esp_bms_idf_bms_scan_candidate_t candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES] = { 0 };
    uint8_t count = 0;

    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = runtime->bms_scan_candidate_count;
        if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
        memcpy(candidates, runtime->bms_scan_candidates, sizeof(candidates));
        xSemaphoreGive(runtime->bms_scan_lock);
    } else if (!runtime->bms_scan_lock) {
        count = runtime->bms_scan_candidate_count;
        if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
        memcpy(candidates, runtime->bms_scan_candidates, sizeof(candidates));
    }

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    size_t offset = 0;
    int written = snprintf(json,
                           sizeof(json),
                           "{\"scan_active\":%s,\"candidates\":[",
                           RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    offset = (size_t)written;

    for (uint8_t index = 0; index < count; index++) {
        const esp_bms_idf_bms_scan_candidate_t *candidate = &candidates[index];
        written = snprintf(json + offset,
                           sizeof(json) - offset,
                           "%s{\"mac\":\"%s\",\"rssi\":%d",
                           index == 0U ? "" : ",",
                           candidate->mac,
                           (int)candidate->rssi);
        if (written < 0 || (size_t)written >= sizeof(json) - offset) {
            return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
        }
        offset += (size_t)written;

        if (candidate->has_name && candidate->name[0] != '\0') {
            written = snprintf(json + offset,
                               sizeof(json) - offset,
                               ",\"name\":\"%s\"",
                               candidate->name);
            if (written < 0 || (size_t)written >= sizeof(json) - offset) {
                return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
            }
            offset += (size_t)written;
        }

        written = snprintf(json + offset, sizeof(json) - offset, "}");
        if (written < 0 || (size_t)written >= sizeof(json) - offset) {
            return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
        }
        offset += (size_t)written;
    }

    written = snprintf(json + offset, sizeof(json) - offset, "]}");
    if (written < 0 || (size_t)written >= sizeof(json) - offset) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
}

static void runtime_config_bms_mac_json(const esp_bms_idf_runtime_t *runtime, char *out, size_t out_len)
{
    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };

    if (runtime->http_pending_lock &&
        xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(mac, sizeof(mac), runtime->bms_bound_mac);
        if (RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING)) {
            runtime_copy_snapshot_text(mac, sizeof(mac), runtime->http_pending_bms_bound_mac);
        }
        xSemaphoreGive(runtime->http_pending_lock);
    } else {
        runtime_copy_snapshot_text(mac, sizeof(mac), runtime->bms_bound_mac);
    }

    if (mac[0] == '\0') {
        runtime_copy_snapshot_text(out, out_len, "null");
        return;
    }
    (void)snprintf(out, out_len, "\"%s\"", mac);
}

static esp_err_t runtime_http_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char bms_mac[24] = { 0 };
    runtime_config_bms_mac_json(runtime, bms_mac, sizeof(bms_mac));

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"brightness\":%u,\"volume\":%u,\"display_rotation\":\"%s\","
                                 "\"speed_unit\":\"%s\",\"speed_source\":\"%s\","
                                 "\"active_speed_source\":\"%s\",\"controller_online\":%s,"
                                 "\"language\":\"%s\","
                                 "\"setup_ap_ssid\":\"%s\",\"setup_ap_password_saved\":%s,"
                                 "\"setup_ap_state\":\"%s\",\"bms_mac\":%s,\"bms_type\":\"%s\"}",
                                 runtime->brightness_percent,
                                 runtime->volume_percent,
                                 runtime_rotation_config_text(runtime->display_rotation),
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 runtime_speed_source_config_text(runtime->snapshot.speed_source),
                                 runtime_speed_source_config_text(runtime->snapshot.active_speed_source),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, CONTROLLER_ONLINE) ? "true" : "false",
                                 RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? "zh" : "en",
                                 runtime->setup_ap_ssid,
                                 runtime->setup_ap_password[0] == '\0' ? "false" : "true",
                                 RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED) ? "enabled" : "disabled",
                                 bms_mac,
                                 runtime_bms_type_config_text((esp_bms_idf_bms_type_t)runtime->bms_type));
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
}

static esp_err_t runtime_http_read_body(httpd_req_t *req, char *body, size_t body_len)
{
    if (req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    size_t remaining = req->content_len;
    while (remaining > 0U) {
        const int read = httpd_req_recv(req, body + offset, remaining);
        if (read <= 0) {
            return ESP_FAIL;
        }
        offset += (size_t)read;
        remaining -= (size_t)read;
    }
    body[offset] = '\0';
    return ESP_OK;
}

static bool runtime_set_pending_http_ap_password(esp_bms_idf_runtime_t *runtime, const char *password)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(runtime->http_pending_setup_ap_password,
                               sizeof(runtime->http_pending_setup_ap_password),
                               password);
    RUNTIME_SET_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_set_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime, const char *mac)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(runtime->http_pending_bms_bound_mac,
                               sizeof(runtime->http_pending_bms_bound_mac),
                               mac);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_BIND_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_set_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static esp_err_t runtime_http_post_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    uint8_t brightness_percent = runtime->brightness_percent;
    uint8_t volume_percent = runtime->volume_percent;
    esp_bms_idf_display_rotation_t rotation = runtime->display_rotation;
    esp_bms_speed_unit_t speed_unit = runtime->snapshot.speed_unit;
    esp_bms_speed_source_t speed_source = runtime->snapshot.speed_source;
    bool language_zh = RUNTIME_FLAG(runtime, LANGUAGE_ZH);
    esp_bms_idf_bms_type_t bms_type = (esp_bms_idf_bms_type_t)runtime->bms_type;

    bool found = false;
    uint8_t parsed_u8 = 0;
    if (!runtime_json_get_u8(body, "brightness", &parsed_u8, &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid brightness");
    }
    if (found) {
        if (!runtime_brightness_matches_policy(parsed_u8)) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid brightness");
        }
        brightness_percent = parsed_u8;
    }

    if (!runtime_json_get_u8(body, "volume", &parsed_u8, &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid volume");
    }
    if (found) {
        if (!runtime_volume_matches_policy(parsed_u8)) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid volume");
        }
        volume_percent = parsed_u8;
    }

    char parsed_text[32] = { 0 };
    if (!runtime_json_get_string(body, "display_rotation", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid rotation");
    }
    if (found && !runtime_parse_rotation_config_text(parsed_text, &rotation)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid rotation");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "speed_unit", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed unit");
    }
    if (found && !runtime_parse_speed_unit_config_text(parsed_text, &speed_unit)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed unit");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "speed_source", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed source");
    }
    if (found && !runtime_parse_speed_source_config_text(parsed_text, &speed_source)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed source");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "language", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid language");
    }
    if (found) {
        if (strcmp(parsed_text, "zh") == 0) {
            language_zh = true;
        } else if (strcmp(parsed_text, "en") == 0) {
            language_zh = false;
        } else {
            return runtime_http_send_text(req, "400 Bad Request", "invalid language");
        }
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "bms_type", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid BMS type");
    }
    if (found && !runtime_parse_bms_type_config_text(parsed_text, &bms_type)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid BMS type");
    }

    if (!runtime_set_pending_http_config(runtime, brightness_percent, volume_percent,
                                         rotation, speed_unit, speed_source,
                                         language_zh, bms_type)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "config queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_ap_password_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    char parsed_password[sizeof(runtime->setup_ap_password)] = { 0 };
    bool found = false;
    if (!runtime_json_get_string(body, "password", parsed_password, sizeof(parsed_password), &found) ||
        !found ||
        !runtime_setup_ap_password_matches_policy(parsed_password)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid password");
    }

    if (!runtime_set_pending_http_ap_password(runtime, parsed_password)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "password queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_bms_bind_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    char parsed_mac[18] = { 0 };
    bool found = false;
    if (!runtime_json_get_string(body, "mac", parsed_mac, sizeof(parsed_mac), &found) || !found) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid mac");
    }

    char normalized_mac[18] = { 0 };
    if (!runtime_normalize_mac_text(parsed_mac, normalized_mac, sizeof(normalized_mac))) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid mac");
    }

    if (!runtime_set_pending_http_bms_bind(runtime, normalized_mac)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "bms bind queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_bms_scan_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    if (req->content_len > 0U) {
        char body[HTTP_BODY_MAX_LEN] = { 0 };
        if (runtime_http_read_body(req, body, sizeof(body)) != ESP_OK) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid body");
        }
    }

    ESP_LOGI(TAG, "[http] BMS scan requested");
    if (!runtime_set_pending_http_bms_scan(runtime)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "bms scan queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_cast_info_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char json[192] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"protocol_version\":%u,\"width\":%u,\"height\":%u,"
                                 "\"rotation\":%u,\"max_block_side\":%u,\"active\":%s}",
                                 CAST_PROTOCOL_VERSION,
                                 runtime_cast_width(runtime),
                                 runtime_cast_height(runtime),
                                 runtime->display_rotation,
                                 CAST_BLOCK_MAX_SIDE,
                                 __atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED) ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "cast info format error");
    }
    return runtime_http_send_json(req, json);
}

static uint16_t runtime_cast_u16(const uint8_t *value)
{
    return (uint16_t)((uint16_t)value[0] << 8U) | value[1];
}

static uint32_t runtime_cast_u32(const uint8_t *value)
{
    return ((uint32_t)value[0] << 24U) | ((uint32_t)value[1] << 16U) |
           ((uint32_t)value[2] << 8U) | value[3];
}

static esp_err_t runtime_cast_send_ack(httpd_req_t *req, uint32_t sequence)
{
    uint8_t ack[] = { CAST_TYPE_ACK, 0U, 0U, 0U, 0U };
    ack[1] = (uint8_t)(sequence >> 24U);
    ack[2] = (uint8_t)(sequence >> 16U);
    ack[3] = (uint8_t)(sequence >> 8U);
    ack[4] = (uint8_t)sequence;
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = ack,
        .len = sizeof(ack),
    };
    return httpd_ws_send_frame(req, &frame);
}

esp_err_t esp_bms_idf_runtime_http_cast_ws_handler(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)req->user_ctx;
    if (!runtime) {
        return ESP_FAIL;
    }
    if (req->method == HTTP_GET) {
        if (__atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED)) {
            ESP_LOGW(TAG, "[cast] reject second client fd=%d", httpd_req_to_sockfd(req));
            return ESP_FAIL;
        }
        __atomic_store_n(&runtime->cast_active, true, __ATOMIC_RELAXED);
        runtime->cast_frame_active = false;
        runtime->cast_socket_fd = httpd_req_to_sockfd(req);
        runtime->cast_sequence = 0U;
        runtime->cast_heartbeat_elapsed_ms = 0U;
        ESP_LOGI(TAG, "[cast] client connected fd=%d", runtime->cast_socket_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, 0), TAG, "read cast frame header failed");
    if (frame.type != HTTPD_WS_TYPE_BINARY || frame.len == 0U || frame.len > CAST_MESSAGE_MAX_BYTES) {
        esp_bms_idf_runtime_stop_cast(runtime, "invalid frame");
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t message[CAST_MESSAGE_MAX_BYTES] = { 0 };
    frame.payload = message;
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, frame.len), TAG, "read cast frame failed");
    runtime->cast_heartbeat_elapsed_ms = 0U;

    if (message[0] == CAST_TYPE_HEARTBEAT && frame.len == 1U) {
        return ESP_OK;
    }
    if (message[0] == CAST_TYPE_FRAME_BEGIN && frame.len == CAST_FRAME_BEGIN_BYTES &&
        message[1] == CAST_PROTOCOL_VERSION && message[6] <= 3U && !runtime->cast_frame_active) {
        runtime->cast_sequence = runtime_cast_u32(&message[2]);
        runtime->cast_frame_active = true;
        return ESP_OK;
    }
    if (message[0] == CAST_TYPE_FRAME_END && frame.len == CAST_FRAME_END_BYTES &&
        runtime->cast_frame_active && runtime_cast_u32(&message[1]) == runtime->cast_sequence) {
        runtime->cast_frame_active = false;
        return runtime_cast_send_ack(req, runtime->cast_sequence);
    }
    if (message[0] != CAST_TYPE_RGB565_BLOCK || !runtime->cast_frame_active ||
        frame.len < CAST_BLOCK_HEADER_BYTES) {
        esp_bms_idf_runtime_stop_cast(runtime, "unknown message");
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t x = runtime_cast_u16(&message[1]);
    const uint16_t y = runtime_cast_u16(&message[3]);
    const uint8_t width = message[5];
    const uint8_t height = message[6];
    const size_t pixel_bytes = (size_t)width * height * sizeof(uint16_t);
    if (width == 0U || height == 0U || width > CAST_BLOCK_MAX_SIDE || height > CAST_BLOCK_MAX_SIDE ||
        x >= runtime_cast_width(runtime) || y >= runtime_cast_height(runtime) ||
        width > runtime_cast_width(runtime) - x || height > runtime_cast_height(runtime) - y ||
        frame.len != CAST_BLOCK_HEADER_BYTES + pixel_bytes) {
        esp_bms_idf_runtime_stop_cast(runtime, "block out of bounds");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(esp_bms_lvgl_bridge_lock(-1), TAG, "lock display for cast failed");
    const esp_err_t ret = esp_bms_lvgl_bridge_write_rgb565(x, y, width, height,
                                                           &message[CAST_BLOCK_HEADER_BYTES], pixel_bytes);
    esp_bms_lvgl_bridge_unlock();
    return ret;
}

esp_err_t esp_bms_idf_runtime_http_api_handler(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)req->user_ctx;
    if (!runtime) {
        return runtime_http_send_text(req, "500 Internal Server Error", "runtime missing");
    }

    if (req->method == HTTP_OPTIONS) {
        return runtime_http_send_no_content(req);
    }
    ESP_LOGI(TAG,
             "[http] api request: method=%d uri=%s clients=%u",
             req->method,
             req->uri,
             (unsigned)runtime->setup_ap_clients);
    runtime_log_heap_state("http_api");

    if (req->method == HTTP_GET && strcmp(req->uri, "/api/status") == 0) {
        return runtime_http_status_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/cast/info") == 0) {
        return runtime_http_cast_info_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_config_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/bms/candidates") == 0) {
        return runtime_http_bms_candidates_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_post_config_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ap-password") == 0) {
        return runtime_http_post_ap_password_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/bms/scan") == 0) {
        return runtime_http_post_bms_scan_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/bms/bind") == 0) {
        return runtime_http_post_bms_bind_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ota") == 0) {
#if ESP_BMS_FEATURE_OTA
        return esp_bms_ota_handle_http_request(req);
#else
        return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
#endif
    }
    if (runtime->optional_http_handler) {
        const esp_err_t optional_result =
            runtime->optional_http_handler(req, runtime->optional_http_context);
        if (optional_result != ESP_ERR_NOT_FOUND) {
            return optional_result;
        }
    }
    ESP_LOGI(TAG, "[http] route not implemented: method=%d uri=%s", req->method, req->uri);
    return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
}

static esp_err_t runtime_load_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char ssid[sizeof(runtime->setup_ap_ssid)] = { 0 };
    char password[sizeof(runtime->setup_ap_password)] = { 0 };
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    ret = nvs_get_str(handle, SETUP_AP_NVS_SSID_KEY, ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, SETUP_AP_NVS_PASSWORD_KEY, password, &password_len);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_setup_ap_ssid_matches_policy(ssid) ||
        !runtime_setup_ap_password_matches_policy(password)) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime_copy_snapshot_text(runtime->setup_ap_ssid, sizeof(runtime->setup_ap_ssid), ssid);
    runtime_copy_snapshot_text(runtime->setup_ap_password, sizeof(runtime->setup_ap_password), password);
    runtime_update_setup_ap_snapshot(runtime);
    return ESP_OK;
}

static esp_err_t runtime_save_setup_ap_credentials(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_setup_ap_ssid_matches_policy(runtime->setup_ap_ssid) ||
        !runtime_setup_ap_password_matches_policy(runtime->setup_ap_password)) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, SETUP_AP_NVS_SSID_KEY, runtime->setup_ap_ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, SETUP_AP_NVS_PASSWORD_KEY, runtime->setup_ap_password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t esp_bms_idf_runtime_load_bms_binding(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t ret = runtime_init_nvs(runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle = 0;
    ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    char name[sizeof(runtime->bms_bound_name)] = { 0 };
    size_t mac_len = sizeof(mac);
    ret = nvs_get_str(handle, BMS_NVS_BOUND_MAC_KEY, mac, &mac_len);
    size_t name_len = sizeof(name);
    const esp_err_t name_ret = nvs_get_str(handle, BMS_NVS_BOUND_NAME_KEY, name, &name_len);
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_normalize_mac_text(mac, normalized_mac, sizeof(normalized_mac))) {
        runtime->bms_bound_mac[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    runtime_copy_snapshot_text(runtime->bms_bound_mac, sizeof(runtime->bms_bound_mac), normalized_mac);
    if (name_ret == ESP_OK) {
        (void)runtime_bms_name_copy(runtime->bms_bound_name,
                                    sizeof(runtime->bms_bound_name),
                                    (const uint8_t *)name,
                                    strlen(name));
    } else {
        runtime->bms_bound_name[0] = '\0';
    }
    runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                               sizeof(runtime->snapshot.bms_bound_name),
                               runtime->bms_bound_name);
    return ESP_OK;
}

static esp_err_t runtime_save_bms_binding(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->bms_bound_mac[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_normalize_mac_text(runtime->bms_bound_mac, normalized_mac, sizeof(normalized_mac))) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = runtime_init_nvs(runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle = 0;
    ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, BMS_NVS_BOUND_MAC_KEY, normalized_mac);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, BMS_NVS_BOUND_NAME_KEY, runtime->bms_bound_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool runtime_get_pending_http_ap_password(esp_bms_idf_runtime_t *runtime,
                                                 char *password,
                                                 size_t password_len)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING);
    if (pending) {
        runtime_copy_snapshot_text(password, password_len, runtime->http_pending_setup_ap_password);
    }
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static void runtime_clear_pending_http_ap_password(esp_bms_idf_runtime_t *runtime, const char *password)
{
    if (!runtime->http_pending_lock) {
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (RUNTIME_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING) &&
        strcmp(runtime->http_pending_setup_ap_password, password) == 0) {
        runtime->http_pending_setup_ap_password[0] = '\0';
        RUNTIME_SET_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING, false);
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_apply_pending_http_ap_password(esp_bms_idf_runtime_t *runtime)
{
    char password[sizeof(runtime->setup_ap_password)] = { 0 };
    if (!runtime_get_pending_http_ap_password(runtime, password, sizeof(password))) {
        return false;
    }

    char previous_password[sizeof(runtime->setup_ap_password)] = { 0 };
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_password, sizeof(previous_password), runtime->setup_ap_password);
    if (strcmp(password, runtime->setup_ap_password) == 0) {
        xSemaphoreGive(runtime->http_pending_lock);
        runtime_clear_pending_http_ap_password(runtime, password);
        RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);
        return false;
    }
    runtime_copy_snapshot_text(runtime->setup_ap_password, sizeof(runtime->setup_ap_password), password);
    xSemaphoreGive(runtime->http_pending_lock);
    runtime_update_setup_ap_snapshot(runtime);

    esp_err_t ret = ESP_OK;
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        ret = runtime->network_driver && runtime->network_driver->refresh_setup_ap_config
                  ? runtime->network_driver->refresh_setup_ap_config(runtime)
                  : ESP_ERR_NOT_SUPPORTED;
    }
    if (ret == ESP_OK) {
        ret = runtime_save_setup_ap_credentials(runtime);
    }
    if (ret == ESP_OK) {
        runtime_clear_pending_http_ap_password(runtime, password);
        RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);
        runtime_set_error(runtime, "AP PW SET");
        ESP_LOGI(TAG, "[wifi] setup AP password updated: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
        return true;
    }

    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(runtime->setup_ap_password,
                                   sizeof(runtime->setup_ap_password),
                                   previous_password);
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_update_setup_ap_snapshot(runtime);
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED) && runtime->network_driver &&
        runtime->network_driver->refresh_setup_ap_config) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(runtime->network_driver->refresh_setup_ap_config(runtime));
    }
    runtime_set_error(runtime, "AP PW FAIL");
    ESP_LOGW(TAG, "[wifi] setup AP password update failed: %s", esp_err_to_name(ret));
    return true;
}

static bool runtime_get_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_BMS_SCAN_PENDING);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, false);
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static bool runtime_apply_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_get_pending_http_bms_scan(runtime)) {
        return false;
    }

    ESP_LOGI(TAG, "[bms] consume pending BLE scan request");
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    const esp_err_t ret = runtime->bms_ble_driver && runtime->bms_ble_driver->start_for_bind
                              ? runtime->bms_ble_driver->start_for_bind(runtime)
                              : ESP_ERR_NOT_SUPPORTED;
    if (ret == ESP_OK) {
        runtime_log_heap_state("bms_scan_started");
        return true;
    }

    runtime_set_bms_info(runtime, "BLE FAIL");
    ESP_LOGW(TAG, "[bms] BLE bind scan start failed: %s", esp_err_to_name(ret));
    runtime_log_heap_state("bms_scan_failed");
    return true;
}

static bool runtime_get_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime,
                                              char *mac,
                                              size_t mac_len)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING);
    if (pending) {
        runtime_copy_snapshot_text(mac, mac_len, runtime->http_pending_bms_bound_mac);
    }
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static void runtime_clear_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime, const char *mac)
{
    if (!runtime->http_pending_lock) {
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING) &&
        strcmp(runtime->http_pending_bms_bound_mac, mac) == 0) {
        runtime->http_pending_bms_bound_mac[0] = '\0';
        RUNTIME_SET_FLAG(runtime, HTTP_BMS_BIND_PENDING, false);
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_apply_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime)
{
    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_get_pending_http_bms_bind(runtime, mac, sizeof(mac))) {
        return false;
    }

    char previous_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    char previous_name[sizeof(runtime->bms_bound_name)] = { 0 };
    char selected_name[sizeof(runtime->bms_bound_name)] = { 0 };
    if (!runtime->bms_scan_lock ||
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        const size_t candidate_index = runtime_bms_scan_find_candidate(runtime, mac);
        if (candidate_index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES &&
            runtime->bms_scan_candidates[candidate_index].has_name) {
            runtime_copy_snapshot_text(selected_name,
                                       sizeof(selected_name),
                                       runtime->bms_scan_candidates[candidate_index].name);
        }
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_mac, sizeof(previous_mac), runtime->bms_bound_mac);
    runtime_copy_snapshot_text(previous_name, sizeof(previous_name), runtime->bms_bound_name);
    if (strcmp(mac, runtime->bms_bound_mac) != 0) {
        runtime_copy_snapshot_text(runtime->bms_bound_mac, sizeof(runtime->bms_bound_mac), mac);
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   selected_name);
    } else if (selected_name[0] != '\0') {
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   selected_name);
    }
    runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                               sizeof(runtime->snapshot.bms_bound_name),
                               runtime->bms_bound_name);
    xSemaphoreGive(runtime->http_pending_lock);

    esp_err_t ret = runtime_save_bms_binding(runtime);
    if (ret == ESP_OK) {
        runtime_clear_pending_http_bms_bind(runtime, mac);
        RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, true);
        ESP_LOGI(TAG, "[bms] bound MAC saved: mac=%s", mac);
        const esp_err_t scan_ret = runtime->bms_ble_driver &&
                                           runtime->bms_ble_driver->start_for_bind
                                       ? runtime->bms_ble_driver->start_for_bind(runtime)
                                       : ESP_ERR_NOT_SUPPORTED;
        if (scan_ret != ESP_OK) {
            runtime_set_bms_info(runtime, "BLE FAIL");
            ESP_LOGW(TAG, "[bms] scan after bind start failed: %s", esp_err_to_name(scan_ret));
        }
        return true;
    }

    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(runtime->bms_bound_mac,
                                   sizeof(runtime->bms_bound_mac),
                                   previous_mac);
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   previous_name);
        runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                                   sizeof(runtime->snapshot.bms_bound_name),
                                   previous_name);
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_set_bms_info(runtime, "BMS SAVE");
    ESP_LOGW(TAG, "[bms] bound MAC save failed: %s", esp_err_to_name(ret));
    return true;
}

static void runtime_ensure_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t nvs_ret = runtime_init_nvs(runtime);
    if (nvs_ret == ESP_OK) {
        const esp_err_t load_ret = runtime_load_setup_ap_credentials(runtime);
        if (load_ret == ESP_OK) {
            ESP_LOGI(TAG, "[wifi] setup AP credentials loaded: ssid='%s' ap_pw_len=%u",
                     runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
            return;
        }
        if (load_ret == ESP_ERR_NVS_NOT_FOUND || load_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "[wifi] setup AP credentials missing or stale; regenerating");
        } else {
            ESP_LOGW(TAG, "[wifi] setup AP credential load failed: %s", esp_err_to_name(load_ret));
        }
    } else {
        ESP_LOGW(TAG, "[wifi] NVS init for setup AP credentials failed: %s", esp_err_to_name(nvs_ret));
    }

    runtime_generate_setup_ap_credentials(runtime);
    if (nvs_ret != ESP_OK) {
        ESP_LOGW(TAG, "[wifi] using volatile setup AP credentials: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
        return;
    }

    const esp_err_t save_ret = runtime_save_setup_ap_credentials(runtime);
    if (save_ret == ESP_OK) {
        ESP_LOGI(TAG, "[wifi] setup AP credentials regenerated and saved: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
    } else {
        ESP_LOGW(TAG, "[wifi] setup AP credential save failed: %s", esp_err_to_name(save_ret));
    }
}

static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            runtime->bluetooth_conn_handle = event->connect.conn_handle;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT PAIR");
            const int security_rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (security_rc != 0 && security_rc != BLE_HS_EALREADY) {
                runtime_set_error(runtime, "BT PAIR FAIL");
                ESP_LOGW(TAG, "[bt] pairing start failed: conn=%u rc=%d",
                         event->connect.conn_handle, security_rc);
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            } else {
                ESP_LOGI(TAG, "[bt] local Bluetooth connected; pairing started: conn=%u",
                         event->connect.conn_handle);
            }
        } else {
            runtime->bluetooth_conn_handle = 0xFFFFU;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
            (void)runtime_project_bluetooth_snapshot(runtime);
            ESP_LOGW(TAG, "[bt] local Bluetooth connection failed: status=%d", event->connect.status);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bluetooth_conn_handle) {
            const bool start_bms_scan = RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED);
            const bool resume_advertising =
                RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) && !start_bms_scan;
            runtime->bluetooth_conn_handle = 0xFFFFU;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, resume_advertising);
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT OFF");
            ESP_LOGI(TAG, "[bt] local Bluetooth disconnected: reason=%d", event->disconnect.reason);
            if (start_bms_scan) {
                const esp_err_t ret = runtime->bms_ble_driver &&
                                              runtime->bms_ble_driver->resume_scan
                                          ? runtime->bms_ble_driver->resume_scan(runtime)
                                          : ESP_ERR_NOT_SUPPORTED;
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] scan after local Bluetooth disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle == runtime->bluetooth_conn_handle) {
            struct ble_gap_conn_desc desc = { 0 };
            const int find_rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            const bool paired = event->enc_change.status == 0 && find_rc == 0 &&
                                desc.sec_state.encrypted;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, paired);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, !paired);
            (void)runtime_project_bluetooth_snapshot(runtime);
            if (paired) {
                runtime_set_error(runtime, "BT CONN");
                ESP_LOGI(TAG,
                         "[bt] pairing complete: conn=%u encrypted=%u bonded=%u",
                         event->enc_change.conn_handle,
                         desc.sec_state.encrypted,
                         desc.sec_state.bonded);
            } else {
                runtime_set_error(runtime, "BT PAIR FAIL");
                ESP_LOGW(TAG, "[bt] pairing failed: conn=%u status=%d find_rc=%d",
                         event->enc_change.conn_handle,
                         event->enc_change.status,
                         find_rc);
                (void)ble_gap_terminate(event->enc_change.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc = { 0 };
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            (void)ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
    {
        const bool should_resume_advertising = RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
        if (should_resume_advertising &&
            !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
            !RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) &&
            !RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE)) {
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        }
        (void)runtime_project_bluetooth_snapshot(runtime);
        ESP_LOGI(TAG, "[bt] local Bluetooth advertising complete: reason=%d",
                 event->adv_complete.reason);
        return 0;
    }
    default:
        return 0;
    }
}

static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RUNTIME_FLAG(runtime, BMS_BLE_READY) || !RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_ERR_INVALID_STATE;
    }
    if (RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) || RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) || ble_gap_adv_active()) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, ble_gap_adv_active() != 0);
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_OK;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) || ble_gap_disc_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        return ESP_FAIL;
    }

    uint8_t own_addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(runtime->bluetooth_name[0] != '\0'
                                         ? runtime->bluetooth_name
                                         : LOCAL_BLUETOOTH_NAME);
    if (rc != 0) {
        return ESP_FAIL;
    }

    struct ble_hs_adv_fields fields = { 0 };
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    const int fields_rc = ble_gap_adv_set_fields(&fields);
    if (fields_rc != 0) {
        return ESP_FAIL;
    }

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(LOCAL_BLUETOOTH_ADV_INTERVAL_MS);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(LOCAL_BLUETOOTH_ADV_INTERVAL_MS + 10U);

    rc = ble_gap_adv_start(own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &params,
                           runtime_bluetooth_gap_event,
                           runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    runtime->bluetooth_own_addr_type = own_addr_type;
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, true);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT ON");
    ESP_LOGI(TAG, "[bt] local Bluetooth advertising started: name='%s'", runtime->snapshot.bluetooth_name);
    return ESP_OK;
}

static void runtime_ble_host_on_reset(int reason)
{
    esp_bms_idf_runtime_t *runtime = s_ble_host_runtime;
    if (runtime) {
        RUNTIME_SET_FLAG(runtime, BLE_HOST_SYNCED, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
        runtime->bluetooth_conn_handle = 0xFFFFU;
        if (runtime->bms_ble_driver && runtime->bms_ble_driver->on_ble_reset) {
            runtime->bms_ble_driver->on_ble_reset(runtime);
        }
        if (runtime->controller_ble_driver && runtime->controller_ble_driver->on_ble_reset) {
            runtime->controller_ble_driver->on_ble_reset(runtime);
        }
        (void)runtime_project_bluetooth_snapshot(runtime);
    }
    ESP_LOGW(TAG, "[ble] NimBLE reset: reason=%d", reason);
}

static void runtime_ble_host_on_sync(void)
{
    esp_bms_idf_runtime_t *runtime = s_ble_host_runtime;
    if (!runtime) {
        return;
    }
    RUNTIME_SET_FLAG(runtime, BLE_HOST_SYNCED, true);
    ESP_LOGI(TAG, "[ble] NimBLE synced");
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
        const esp_err_t ret = esp_bms_idf_runtime_start_controller_scan(runtime);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[controller] deferred scan failed: %s", esp_err_to_name(ret));
        }
    } else if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = runtime->bms_ble_driver && runtime->bms_ble_driver->resume_scan
                                  ? runtime->bms_ble_driver->resume_scan(runtime)
                                  : ESP_ERR_NOT_SUPPORTED;
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[bms] deferred BLE scan start failed: %s", esp_err_to_name(ret));
        }
    } else if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED)) {
        const esp_err_t ret = runtime_bluetooth_start_advertising_now(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] deferred local Bluetooth advertising failed: %s",
                     esp_err_to_name(ret));
        }
    }
}

static void runtime_ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "[ble] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t runtime_init_ble_host(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, BLE_HOST_READY)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_ble_host_runtime = runtime;
    ble_svc_gap_init();
    int gap_rc = ble_svc_gap_device_name_set(runtime->bluetooth_name[0] != '\0'
                                                 ? runtime->bluetooth_name
                                                 : LOCAL_BLUETOOTH_NAME);
    if (gap_rc != 0) {
        return ESP_FAIL;
    }
    gap_rc = ble_svc_gap_device_appearance_set(0x0200U);
    if (gap_rc != 0) {
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = runtime_ble_host_on_reset;
    ble_hs_cfg.sync_cb = runtime_ble_host_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    BaseType_t task_ret = xTaskCreate(runtime_ble_host_task,
                                      "nimble-host",
                                      BMS_SCAN_HOST_TASK_STACK,
                                      NULL,
                                      BMS_SCAN_HOST_TASK_PRIORITY,
                                      NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    RUNTIME_SET_FLAG(runtime, BLE_HOST_READY, true);
    RUNTIME_SET_FLAG(runtime, BLE_HOST_STARTED, true);
    ESP_LOGI(TAG, "[ble] NimBLE initialized");
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_ensure_ble_host(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime_init_ble_host(runtime);
}

void esp_bms_idf_runtime_register_bms_frame_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_bms_frame_handler_t handler)
{
    if (runtime) {
        runtime->bms_frame_handler = handler;
    }
}

void esp_bms_idf_runtime_register_bms_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_bms_ble_driver_t *driver)
{
    if (runtime) {
        runtime->bms_ble_driver = driver;
    }
}

void esp_bms_idf_runtime_register_controller_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_controller_ble_driver_t *driver)
{
    if (runtime) {
        runtime->controller_ble_driver = driver;
    }
}

void esp_bms_idf_runtime_register_network_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_network_driver_t *driver)
{
    if (runtime) {
        runtime->network_driver = driver;
    }
}

static bool runtime_sample_battery(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, BATTERY_ADC_READY) || !runtime->battery_adc) {
        return false;
    }

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(runtime->battery_adc, runtime->battery_adc_channel, &raw);
    if (ret != ESP_OK) {
        runtime->battery_read_failures++;
        if (runtime->battery_read_failures == 1 || (runtime->battery_read_failures % 32U) == 0U) {
            ESP_LOGW(TAG, "battery ADC read failed: %s", esp_err_to_name(ret));
        }
        return false;
    }

    runtime->battery_read_failures = 0;
    if (raw < 0) {
        raw = 0;
    } else if ((uint32_t)raw > BATTERY_ADC_MAX) {
        raw = BATTERY_ADC_MAX;
    }

    const uint32_t battery_mv = runtime_battery_mv_from_raw((uint16_t)raw);
    const bool changed =
        !RUNTIME_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID) || runtime->snapshot.local_battery_mv != battery_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID, true);
    runtime->snapshot.local_battery_mv = battery_mv;
    runtime->battery_samples_seen++;
    return changed;
}

void esp_bms_idf_runtime_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->cast_socket_fd = -1;
    runtime->http_pending_lock = xSemaphoreCreateMutex();
    if (!runtime->http_pending_lock) {
        ESP_LOGW(TAG, "[http] pending config mutex allocation failed");
    }
    runtime->bms_scan_lock = xSemaphoreCreateMutex();
    if (!runtime->bms_scan_lock) {
        ESP_LOGW(TAG, "[bms] scan candidate mutex allocation failed");
    }
    runtime_reset_state(runtime);
    runtime_ensure_setup_ap_credentials(runtime);
    runtime_init_battery_adc(runtime);
    (void)runtime_sample_battery(runtime);
}

esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime->network_driver && runtime->network_driver->start_setup_ap
               ? runtime->network_driver->start_setup_ap(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime->network_driver && runtime->network_driver->start_http_server
               ? runtime->network_driver->start_http_server(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_stop_setup_services(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    return runtime->network_driver && runtime->network_driver->stop_setup_services
               ? runtime->network_driver->stop_setup_services(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_start_bluetooth_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    if (runtime->bluetooth_name[0] == '\0') {
        runtime_copy_snapshot_text(runtime->bluetooth_name,
                                   sizeof(runtime->bluetooth_name),
                                   LOCAL_BLUETOOTH_NAME);
    }
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
    runtime_project_bluetooth_snapshot(runtime);

    esp_err_t ret = esp_bms_idf_runtime_ensure_ble_host(runtime);
    if (ret != ESP_OK) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
        return ret;
    }
    if (!RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        runtime_set_error(runtime, "BT WAIT");
        return ESP_OK;
    }

    ret = runtime_bluetooth_start_advertising_now(runtime);
    if (ret == ESP_ERR_INVALID_STATE) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT BUSY");
    } else if (ret != ESP_OK) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
    }
    return ret;
}

esp_err_t esp_bms_idf_runtime_start_controller_ble_if_enabled(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_ble_driver || !runtime->controller_ble_driver->start_if_enabled) {
        runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    return runtime->controller_ble_driver->start_if_enabled(runtime);
}

esp_err_t esp_bms_idf_runtime_start_controller_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_ble_driver || !runtime->controller_ble_driver->start_scan) {
        runtime_project_controller_snapshot(runtime);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return runtime->controller_ble_driver->start_scan(runtime);
}

void esp_bms_idf_runtime_stop_controller_ble(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->controller_ble_driver && runtime->controller_ble_driver->stop) {
        runtime->controller_ble_driver->stop(runtime);
    } else {
        runtime_project_controller_snapshot(runtime);
    }
}

static esp_err_t runtime_bluetooth_stop_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
    if (runtime->bluetooth_conn_handle != 0xFFFFU) {
        (void)ble_gap_terminate(runtime->bluetooth_conn_handle,
                                BLE_ERR_REM_USER_CONN_TERM);
    }
    if (RUNTIME_FLAG(runtime, BMS_BLE_READY) &&
        RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) &&
        ble_gap_adv_active()) {
        const int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT FAIL");
            return ESP_FAIL;
        }
    }
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT HIDE");
    return ESP_OK;
}

void esp_bms_idf_runtime_set_active_data_source(esp_bms_idf_runtime_t *runtime,
                                                esp_bms_lvgl_data_source_t source)
{
    if (!runtime || runtime->active_data_source == source) {
        return;
    }
    runtime->active_data_source = source;
    const bool bms_collection_active = source == ESP_BMS_LVGL_DATA_SOURCE_BMS ||
                                       source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD ||
                                       runtime->trip_efficiency.started;
    runtime->bms_status_poll_elapsed_ms = bms_collection_active ? BMS_STATUS_POLL_PERIOD_MS : 0U;
}

void esp_bms_idf_runtime_register_optional_http_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_optional_http_handler_t handler,
    void *context)
{
    if (!runtime) {
        return;
    }
    runtime->optional_http_handler = handler;
    runtime->optional_http_context = context;
}

bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }

    const bool cast_active = __atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED);
    if (cast_active) {
        runtime->cast_heartbeat_elapsed_ms += elapsed_ms;
        if (runtime->cast_heartbeat_elapsed_ms >= CAST_HEARTBEAT_TIMEOUT_MS) {
            esp_bms_idf_runtime_stop_cast(runtime, "heartbeat timeout");
        }
    }

    bool changed = RUNTIME_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY) ||
                   RUNTIME_FLAG(runtime, BMS_SNAPSHOT_DIRTY) ||
                   RUNTIME_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY);
    if (runtime->snapshot.cast_active != cast_active) {
        runtime->snapshot.cast_active = cast_active;
        changed = true;
    }
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY, false);
    RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY, false);
    if (!runtime->bms_ble_driver && RUNTIME_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, false);
        changed = true;
    }
    changed = runtime_apply_pending_http_ap_password(runtime) || changed;
    changed = runtime_apply_pending_http_bms_scan(runtime) || changed;
    changed = runtime_apply_pending_http_bms_bind(runtime) || changed;
    if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
        runtime->bluetooth_conn_handle == 0xFFFFU &&
        !RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = esp_bms_idf_runtime_start_bluetooth_advertising(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] local Bluetooth advertising start failed: %s",
                     esp_err_to_name(ret));
        }
        changed = true;
    }
    if (runtime->bms_ble_driver && runtime->bms_ble_driver->tick) {
        changed = runtime->bms_ble_driver->tick(runtime, elapsed_ms) || changed;
    }
    if (runtime->controller_ble_driver && runtime->controller_ble_driver->tick) {
        changed = runtime->controller_ble_driver->tick(runtime, elapsed_ms) || changed;
    }
    runtime->battery_sample_elapsed_ms += elapsed_ms;
    if (runtime->battery_sample_elapsed_ms >= BATTERY_SAMPLE_PERIOD_MS) {
        runtime->battery_sample_elapsed_ms = 0;
        changed = runtime_sample_battery(runtime) || changed;
    }
    runtime->elapsed_ms += elapsed_ms;
    while (runtime->elapsed_ms >= 1000) {
        runtime->elapsed_ms -= 1000;
        runtime->tick_count++;
    }
    const uint64_t uptime_seconds = (uint64_t)esp_timer_get_time() / UINT64_C(1000000);
    const uint32_t displayed_uptime = uptime_seconds > UINT32_MAX
                                          ? UINT32_MAX
                                          : (uint32_t)uptime_seconds;
    if (runtime->snapshot.uptime_seconds != displayed_uptime) {
        runtime->snapshot.uptime_seconds = displayed_uptime;
        changed = true;
    }
    return changed;
}

uint8_t esp_bms_idf_runtime_take_connection_audio_events(esp_bms_idf_runtime_t *runtime)
{
    return runtime ? __atomic_exchange_n(&runtime->pending_audio_events, 0U, __ATOMIC_RELAXED) : 0U;
}

static bool runtime_action_feature_enabled(esp_bms_lvgl_action_t action)
{
    switch (action) {
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        return ESP_BMS_FEATURE_NETWORK;
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        return ESP_BMS_FEATURE_BMS;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        return ESP_BMS_FEATURE_CONTROLLER;
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        return ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        return ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER;
    default:
        return true;
    }
}

bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event)
{
    if (!runtime || !event || event->action == ESP_BMS_LVGL_ACTION_NONE ||
        !runtime_action_feature_enabled(event->action)) {
        return false;
    }

    switch (event->action) {
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        if (RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED)) {
            RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
            runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
            runtime_set_error(runtime, "AP OFF");
        } else {
            RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, true);
            runtime->snapshot.wifi = ESP_BMS_WIFI_SETUP_AP;
            runtime_set_error(runtime, "SETUP AP");
        }
        return true;
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        (void)runtime_set_brightness_percent(runtime,
                                             runtime->brightness_percent >= 85 ? 30 :
                                             runtime->brightness_percent >= 60 ? 85 : 60);
        runtime_set_error(runtime, runtime->brightness_percent >= 85 ? "BRIGHT 85" :
                                   runtime->brightness_percent >= 60 ? "BRIGHT 60" : "BRIGHT 30");
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        if (!ACTION_EVENT_FLAG(event, BRIGHTNESS_PERCENT_VALID) ||
            !runtime_brightness_matches_policy(event->brightness_percent)) {
            return false;
        }
        if (runtime->brightness_percent == event->brightness_percent) {
            return false;
        }
        (void)runtime_set_brightness_percent(runtime, event->brightness_percent);
        runtime_set_error(runtime, "BRIGHT SET");
        return true;
    case ESP_BMS_LVGL_ACTION_SET_VOLUME:
        if (!ACTION_EVENT_FLAG(event, VOLUME_PERCENT_VALID) ||
            !runtime_volume_matches_policy(event->volume_percent)) {
            return false;
        }
        if (runtime->volume_percent == event->volume_percent) {
            return false;
        }
        (void)runtime_set_volume_percent(runtime, event->volume_percent);
        runtime_set_error(runtime, "VOL SET");
        return true;
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        runtime->display_rotation = runtime_next_rotation(runtime->display_rotation);
        runtime_set_error(runtime, runtime_rotation_text(runtime->display_rotation));
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        runtime->snapshot.speed_unit = runtime->snapshot.speed_unit == ESP_BMS_SPEED_UNIT_KMH
                                           ? ESP_BMS_SPEED_UNIT_MPH
                                           : ESP_BMS_SPEED_UNIT_KMH;
        runtime_update_snapshot_speed(runtime);
        runtime_set_error(runtime, runtime->snapshot.speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "SPEED MPH" : "SPEED KMH");
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE:
        RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, !RUNTIME_FLAG(runtime, LANGUAGE_ZH));
        runtime_set_error(runtime, RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? "LANG ZH" : "LANG EN");
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
        runtime->controller_connection_enabled = !runtime->controller_connection_enabled;
        if (!runtime->controller_connection_enabled) {
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
            esp_bms_idf_runtime_stop_controller_ble(runtime);
        } else if (runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
        if (!esp_bms_lvgl_ui_speed_dashboard_style_available(
                ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER)) {
            return false;
        }
        runtime->controller_page_enabled = !runtime->controller_page_enabled;
        runtime->snapshot.speed_dashboard_style =
            runtime->controller_page_enabled
                ? ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER
                : esp_bms_lvgl_ui_default_speed_dashboard_style();
        if (runtime->controller_page_enabled) {
            runtime->controller_connection_enabled = true;
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            !runtime_speed_dashboard_style_matches_policy(event->numeric_delta)) {
            return false;
        }
        {
            const esp_bms_speed_dashboard_style_t style =
                (esp_bms_speed_dashboard_style_t)event->numeric_delta;
            if (runtime->snapshot.speed_dashboard_style == style) {
                return false;
            }
            runtime->snapshot.speed_dashboard_style = style;
            runtime->controller_page_enabled =
                style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
            if (style != ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR) {
                runtime->controller_connection_enabled = true;
                (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
            }
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            !runtime_boot_animation_style_matches_policy(event->numeric_delta) ||
            runtime->snapshot.boot_animation_style == (uint8_t)event->numeric_delta) {
            return false;
        }
        runtime->snapshot.boot_animation_style = (uint8_t)event->numeric_delta;
        runtime_set_error(runtime,
                          runtime->snapshot.boot_animation_style ==
                                  (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_SWEEP
                              ? "BOOT GAUGE"
                              : "BOOT CHARGE");
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        {
            const esp_bms_speed_source_t target =
                runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_GPS
                    ? ESP_BMS_SPEED_SOURCE_CONTROLLER
                    : ESP_BMS_SPEED_SOURCE_GPS;
            if (target == ESP_BMS_SPEED_SOURCE_GPS &&
                runtime->snapshot.gps_module_state !=
                    (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE) {
                runtime_set_error(runtime, "GPS OFFLINE");
                return false;
            }
            runtime->snapshot.speed_source = target;
        }
        if (runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER &&
            runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            event->numeric_delta < 0 ||
            event->numeric_delta > (int16_t)ESP_BMS_REMAINING_RANGE_MAX_KM) {
            return false;
        }
        if (runtime->snapshot.preset_range_km == (uint16_t)event->numeric_delta) {
            return false;
        }
        runtime->snapshot.preset_range_km = (uint16_t)event->numeric_delta;
        runtime_update_snapshot_speed(runtime);
        runtime_set_error(runtime, "RANGE SET");
        return true;
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
        if (ACTION_EVENT_FLAG(event, CONTROLLER_MAC_VALID)) {
            char normalized_mac[sizeof(runtime->controller_bound_mac)] = { 0 };
            if (!runtime_normalize_mac_text(event->controller_mac,
                                            normalized_mac,
                                            sizeof(normalized_mac))) {
                return false;
            }
            const bool binding_changed = strcmp(runtime->controller_bound_mac, normalized_mac) != 0;
            runtime_copy_snapshot_text(runtime->controller_bound_mac,
                                       sizeof(runtime->controller_bound_mac),
                                       normalized_mac);
            for (uint8_t index = 0; index < runtime->controller_scan_candidate_count; ++index) {
                if (strcmp(runtime->controller_scan_candidates[index].mac, normalized_mac) == 0) {
                    runtime_copy_snapshot_text(runtime->controller_bound_name,
                                               sizeof(runtime->controller_bound_name),
                                               runtime->controller_scan_candidates[index].name);
                    break;
                }
            }
            runtime->controller_connection_enabled = true;
            if (binding_changed && runtime->controller_conn_handle != 0xFFFFU) {
                RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
                (void)ble_gap_terminate(runtime->controller_conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            } else {
                (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
            }
        } else {
            (void)esp_bms_idf_runtime_start_controller_scan(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID)) {
            return false;
        }
        {
            int32_t value = (int32_t)runtime->controller_state.fallback_wheel_circumference_mm +
                            event->numeric_delta;
            value = value < 0 ? 0 : value > 4000 ? 4000 : value;
            runtime->controller_state.fallback_wheel_circumference_mm = (uint16_t)value;
            runtime->controller_fallback_tire_rim_inch = 0U;
            runtime->controller_fallback_tire_aspect_percent = 0U;
            runtime->controller_fallback_tire_width_mm = 0U;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID)) {
            return false;
        }
        {
            int32_t value = (int32_t)runtime->controller_state.fallback_gear_ratio_centi +
                            event->numeric_delta;
            value = value < (int32_t)CONTROLLER_RATIO_CENTI_MIN
                        ? (int32_t)CONTROLLER_RATIO_CENTI_MIN
                        : value > (int32_t)CONTROLLER_RATIO_CENTI_MAX
                              ? (int32_t)CONTROLLER_RATIO_CENTI_MAX
                              : value;
            runtime->controller_state.fallback_gear_ratio_centi = (uint16_t)value;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
        if (!ACTION_EVENT_FLAG(event, CONTROLLER_SETTING_VALID) ||
            !runtime_controller_tire_matches_policy(event->controller_tire_rim_inch,
                                                    event->controller_tire_aspect_percent,
                                                    event->controller_tire_width_mm)) {
            return false;
        }
        {
            uint16_t circumference_mm = 0U;
            if (!esp_fardriver_tire_circumference_mm(event->controller_tire_rim_inch,
                                                      event->controller_tire_aspect_percent,
                                                      event->controller_tire_width_mm,
                                                      &circumference_mm)) {
                return false;
            }
            runtime->controller_fallback_tire_rim_inch = event->controller_tire_rim_inch;
            runtime->controller_fallback_tire_aspect_percent =
                event->controller_tire_aspect_percent;
            runtime->controller_fallback_tire_width_mm = event->controller_tire_width_mm;
            runtime->controller_state.fallback_wheel_circumference_mm = circumference_mm;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        if (!ACTION_EVENT_FLAG(event, CONTROLLER_SETTING_VALID) ||
            !runtime_controller_ratio_matches_policy(event->controller_gear_ratio_centi)) {
            return false;
        }
        runtime->controller_state.fallback_gear_ratio_centi =
            event->controller_gear_ratio_centi;
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        if (ACTION_EVENT_FLAG(event, BMS_MAC_VALID)) {
            char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
            if (!runtime_normalize_mac_text(event->bms_mac, normalized_mac, sizeof(normalized_mac)) ||
                !runtime_set_pending_http_bms_bind(runtime, normalized_mac)) {
                runtime_set_bms_info(runtime, "BMS BIND FAIL");
                ESP_LOGW(TAG, "[bms] bind action queue failed: mac=%s", event->bms_mac);
                return true;
            }
            runtime_set_bms_info(runtime, "BMS BIND");
            ESP_LOGI(TAG, "[bms] bind action queued: mac=%s", normalized_mac);
            return true;
        }
        if (!runtime_set_pending_http_bms_scan(runtime)) {
            runtime_set_bms_info(runtime, "BMS Q FAIL");
            ESP_LOGW(TAG, "[bms] BLE scan action queue failed");
        } else {
            ESP_LOGI(TAG, "[bms] BLE scan action queued");
        }
        return true;
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
        return runtime->bms_ble_driver && runtime->bms_ble_driver->stop
                   ? runtime->bms_ble_driver->stop(runtime)
                   : false;
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) ||
            RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING)) {
            return runtime_bluetooth_stop_advertising(runtime) == ESP_OK;
        }
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT ON");
        return true;
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_ANT);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JK);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JBD);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_DALY);
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        esp_bms_idf_runtime_stop_controller_ble(runtime);
        runtime_reset_state(runtime);
        (void)runtime_sample_battery(runtime);
        runtime_set_error(runtime, "RESTORED");
        return true;
    case ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD:
    case ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU:
    case ESP_BMS_LVGL_ACTION_SHOW_SETTINGS:
    case ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION:
    case ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION:
    case ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE:
    case ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION:
        return false;
    case ESP_BMS_LVGL_ACTION_NONE:
    default:
        return false;
    }
}

bool esp_bms_idf_runtime_apply_action(esp_bms_idf_runtime_t *runtime, esp_bms_lvgl_action_t action)
{
    const esp_bms_lvgl_action_event_t event = {
        .action = action,
    };
    return esp_bms_idf_runtime_apply_action_event(runtime, &event);
}

const char *esp_bms_idf_runtime_action_name(esp_bms_lvgl_action_t action)
{
    switch (action) {
    case ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD:
        return "show-dashboard";
    case ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU:
        return "show-quick-menu";
    case ESP_BMS_LVGL_ACTION_SHOW_SETTINGS:
        return "show-settings";
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        return "enable-wifi-reprovisioning";
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        return "cycle-brightness";
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        return "set-brightness";
    case ESP_BMS_LVGL_ACTION_SET_VOLUME:
        return "set-volume";
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        return "rotate-display";
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        return "toggle-speed-unit";
    case ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE:
        return "toggle-language";
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
        return "toggle-controller-connection";
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
        return "toggle-controller-page";
    case ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE:
        return "set-speed-dashboard-style";
    case ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE:
        return "set-boot-animation-style";
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        return "toggle-speed-source";
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
        return "start-controller-bind";
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
        return "adjust-controller-wheel";
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
        return "adjust-controller-ratio";
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
        return "set-controller-tire";
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        return "set-controller-ratio";
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        return "set-preset-range";
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        return "start-bms-bind";
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
        return "cancel-bms-connection";
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        return "enable-bluetooth-advertising";
    case ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION:
        return "cycle-level-position";
    case ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION:
        return "start-touch-calibration";
    case ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE:
        return "add-touch-calibration-sample";
    case ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION:
        return "cancel-touch-calibration";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return "select-bms-ant";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return "select-bms-jk";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return "select-bms-jbd";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return "select-bms-daly";
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        return "restore-defaults";
    case ESP_BMS_LVGL_ACTION_NONE:
    default:
        return "none";
    }
}
