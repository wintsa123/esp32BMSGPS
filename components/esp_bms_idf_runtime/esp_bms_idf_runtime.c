#include "esp_bms_idf_runtime.h"

#include "esp_bms_lvgl_bridge.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
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

#define BATTERY_GPIO 34
#define BATTERY_SAMPLE_PERIOD_MS 2000U
#define BATTERY_ADC_MAX 4095U
#define BATTERY_REFERENCE_MV 3300U
#define BATTERY_DIVIDER_TOP_OHMS 100000U
#define BATTERY_DIVIDER_BOTTOM_OHMS 100000U
#define GPS_UART_PORT UART_NUM_1
#define GPS_UART_TX_GPIO 18
#define GPS_UART_RX_GPIO 27
#define GPS_UART_BAUD 115200
#define GPS_UART_RX_BUFFER_SIZE 1024
#define GPS_RMC_MAX_LINE 96U
#define GPS_PPS_GPIO GPIO_NUM_35
#define GPS_PPS_TIMEOUT_SECONDS 3U
#define GPS_DIAGNOSTIC_LOG_PERIOD_SECONDS 60U
#define GPS_RMC_TIMEOUT_SECONDS 3U
#define GPS_UART_STARTUP_DIAGNOSTIC_SECONDS 10U
#define BMS_TELEMETRY_FRESHNESS_US INT64_C(2000000)
#define SETUP_AP_SSID_PREFIX "fuckingBms_"
#define SETUP_AP_SSID_SUFFIX_LEN 6U
#define SETUP_AP_PASSWORD_LEN 8U
#define SETUP_AP_CHANNEL 1U
#define SETUP_AP_MAX_CONNECTIONS 1U
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
#define BMS_FRAME_MIN_LEN 10U
#define BMS_FRAME_START_1 0x7EU
#define BMS_FRAME_START_2 0xA1U
#define BMS_FRAME_END_1 0xAAU
#define BMS_FRAME_END_2 0x55U
#define BMS_FRAME_TYPE_STATUS 0x11U
#define BMS_FRAME_TYPE_DEVICE_INFO 0x12U
#define BMS_MAX_CELLS 32U
#define BMS_MAX_TEMPERATURE_SENSORS 4U
#define BMS_STATUS_PROTECTION_MASK_OFFSET 10U
#define BMS_STATUS_WARNING_MASK_OFFSET 18U
#define BMS_STATUS_DYNAMIC_BASE_OFFSET 34U
#define BMS_NVS_BOUND_MAC_KEY "bms_mac"
#define BMS_NVS_BOUND_NAME_KEY "bms_name"
#define DISPLAY_NVS_BRIGHTNESS_KEY "disp_bright"
#define DISPLAY_NVS_VOLUME_KEY "disp_vol"
#define DISPLAY_NVS_ROTATION_KEY "disp_rot"
#define DISPLAY_NVS_SPEED_UNIT_KEY "speed_unit"
#define DISPLAY_NVS_SPEED_SOURCE_KEY "speed_src"
#define DISPLAY_NVS_LANGUAGE_KEY "lang"
#define DISPLAY_NVS_BMS_TYPE_KEY "bms_type"
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
#define HTTP_SERVER_TASK_PRIORITY 3U
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

static void runtime_cast_stop(esp_bms_idf_runtime_t *runtime, const char *reason)
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

extern const char web_index_html_start[] asm("_binary_index_html_start");
extern const char web_index_html_end[] asm("_binary_index_html_end");

typedef enum {
    GPS_PARSE_IGNORE,
    GPS_PARSE_ERROR,
    GPS_PARSE_FIX,
} gps_parse_result_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} gps_utc_time_t;

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

static esp_err_t runtime_apply_setup_ap_wifi_config(const esp_bms_idf_runtime_t *runtime);
static int runtime_bms_ble_gap_event(struct ble_gap_event *event, void *arg);
static int runtime_controller_gap_event(struct ble_gap_event *event, void *arg);
static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t runtime_bms_ble_start_scan(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_bms_ble_send_poll_request(esp_bms_idf_runtime_t *runtime,
                                                   bool include_device_info);
static esp_err_t runtime_init_bms_ble(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_controller_start_scan(esp_bms_idf_runtime_t *runtime);
static void runtime_controller_set_subscription(esp_bms_idf_runtime_t *runtime, bool enabled);
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

static esp_bms_idf_runtime_t *s_bms_ble_runtime;

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
    runtime->controller_page_enabled = snapshot->speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER;
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

static void runtime_sync_controller_parameters(esp_bms_idf_runtime_t *runtime)
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

    if (!runtime_controller_tire_matches_policy(state->tire_rim_inch,
                                                state->tire_aspect_percent,
                                                state->tire_width_mm) ||
        !runtime_controller_ratio_matches_policy(state->gear_ratio_centi)) {
        ESP_LOGW(TAG,
                 "[controller] parameters not synchronized: tire=%u-%u-%u ratio=%u.%02u",
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
             "[controller] parameters synchronized: tire=%u-%u-%u ratio=%u.%02u",
             state->tire_rim_inch,
             state->tire_aspect_percent,
             state->tire_width_mm,
             state->gear_ratio_centi / 100U,
             state->gear_ratio_centi % 100U);
}

static void runtime_clear_controller_telemetry(esp_bms_idf_runtime_t *runtime)
{
    const uint16_t wheel = runtime->controller_state.fallback_wheel_circumference_mm;
    const uint16_t ratio = runtime->controller_state.fallback_gear_ratio_centi;
    memset(&runtime->controller_state, 0, sizeof(runtime->controller_state));
    runtime->controller_state.fallback_wheel_circumference_mm = wheel;
    runtime->controller_state.fallback_gear_ratio_centi = ratio;
    runtime_project_controller_snapshot(runtime);
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
    const bool changed = RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED) != enabled ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING) !=
                             RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_CONNECTED) !=
                             RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) ||
                         strcmp(runtime->snapshot.bluetooth_name, runtime->bluetooth_name) != 0;

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED, enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING, RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING));
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

static void runtime_ble_addr_to_mac_text(const uint8_t addr[6], char *out, size_t out_len)
{
    if (!out || out_len < 18U) {
        return;
    }
    (void)snprintf(out,
                   out_len,
                   "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                   runtime_hex_char(addr[5] >> 4),
                   runtime_hex_char(addr[5] & 0x0FU),
                   runtime_hex_char(addr[4] >> 4),
                   runtime_hex_char(addr[4] & 0x0FU),
                   runtime_hex_char(addr[3] >> 4),
                   runtime_hex_char(addr[3] & 0x0FU),
                   runtime_hex_char(addr[2] >> 4),
                   runtime_hex_char(addr[2] & 0x0FU),
                   runtime_hex_char(addr[1] >> 4),
                   runtime_hex_char(addr[1] & 0x0FU),
                   runtime_hex_char(addr[0] >> 4),
                   runtime_hex_char(addr[0] & 0x0FU));
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

static bool runtime_bms_scan_project_snapshot(esp_bms_idf_runtime_t *runtime)
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

static void runtime_bms_scan_clear_candidates(esp_bms_idf_runtime_t *runtime)
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
    (void)runtime_bms_scan_project_snapshot(runtime);
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

static void runtime_bms_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
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

static void runtime_append_bms_code(char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],
                                    uint8_t *count,
                                    const char prefix,
                                    uint8_t bit)
{
    if (!codes || !count || *count >= ESP_BMS_BMS_CODE_MAX_COUNT) {
        return;
    }
    (void)snprintf(codes[*count], ESP_BMS_BMS_CODE_TEXT_LEN, "%c%02u", prefix, (unsigned)bit);
    (*count)++;
}

static void runtime_apply_bms_fault_masks(esp_bms_dashboard_snapshot_t *snapshot,
                                          uint64_t protection_mask,
                                          uint64_t warning_mask)
{
    snapshot->bms_protection_count = 0;
    memset(snapshot->bms_protection_codes, 0, sizeof(snapshot->bms_protection_codes));
    snapshot->bms_warning_count = 0;
    memset(snapshot->bms_warning_codes, 0, sizeof(snapshot->bms_warning_codes));

    uint64_t remaining = protection_mask;
    while (remaining != 0ULL && snapshot->bms_protection_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
        const uint8_t bit = (uint8_t)__builtin_ctzll(remaining);
        runtime_append_bms_code(snapshot->bms_protection_codes,
                                &snapshot->bms_protection_count,
                                'P',
                                bit);
        remaining &= remaining - 1ULL;
    }

    remaining = warning_mask;
    while (remaining != 0ULL && snapshot->bms_warning_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
        const uint8_t bit = (uint8_t)__builtin_ctzll(remaining);
        runtime_append_bms_code(snapshot->bms_warning_codes,
                                &snapshot->bms_warning_count,
                                'W',
                                bit);
        remaining &= remaining - 1ULL;
    }
}

static uint16_t runtime_crc16_modbus(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    const uint8_t *cursor = bytes;
    const uint8_t *const end = bytes + len;
    while (cursor < end) {
        crc ^= *cursor++;
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc & 0x0001U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                                  : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static bool runtime_read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || len < 2U || index > len - 2U) {
        return false;
    }
    const uint8_t *const cursor = data + index;
    *out = (uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8);
    return true;
}

static bool runtime_read_i16_le(const uint8_t *data, size_t len, size_t index, int16_t *out)
{
    uint16_t value = 0;
    if (!runtime_read_u16_le(data, len, index, &value)) {
        return false;
    }
    *out = (int16_t)value;
    return true;
}

static bool runtime_read_u32_le(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || len < 4U || index > len - 4U) {
        return false;
    }
    const uint8_t *const cursor = data + index;
    *out = (uint32_t)cursor[0] |
           ((uint32_t)cursor[1] << 8) |
           ((uint32_t)cursor[2] << 16) |
           ((uint32_t)cursor[3] << 24);
    return true;
}

static bool runtime_read_u64_le(const uint8_t *data, size_t len, size_t index, uint64_t *out)
{
    if (!data || !out || len < 8U || index > len - 8U) {
        return false;
    }
    uint64_t value = 0;
    const uint8_t *cursor = data + index;
    for (uint8_t shift = 0; shift < 64U; shift += 8U) {
        value |= ((uint64_t)*cursor++) << shift;
    }
    *out = value;
    return true;
}

static bool runtime_validate_bms_frame(const uint8_t *data,
                                       size_t len,
                                       uint8_t *function,
                                       size_t *protocol_len)
{
    if (!data || !function || !protocol_len || len < BMS_FRAME_MIN_LEN ||
        len > ESP_BMS_IDF_BMS_FRAME_MAX_LEN ||
        data[0] != BMS_FRAME_START_1 ||
        data[1] != BMS_FRAME_START_2 ||
        data[len - 2U] != BMS_FRAME_END_1 ||
        data[len - 1U] != BMS_FRAME_END_2) {
        return false;
    }

    *function = data[2];
    *protocol_len = 6U + data[5] + 4U;
    if (*protocol_len > len || *protocol_len < BMS_FRAME_MIN_LEN) {
        return false;
    }
    if (*function != BMS_FRAME_TYPE_DEVICE_INFO && *protocol_len != len) {
        return false;
    }

    const size_t crc_offset = *protocol_len - 4U;
    const uint16_t expected_crc = runtime_crc16_modbus(&data[1], crc_offset - 1U);
    const uint16_t remote_crc = (uint16_t)data[crc_offset] |
                                ((uint16_t)data[crc_offset + 1U] << 8);
    return expected_crc == remote_crc;
}

static bool runtime_apply_bms_status_frame(esp_bms_idf_runtime_t *runtime,
                                           const uint8_t *data,
                                           size_t len)
{
    uint8_t function = 0;
    size_t protocol_len = 0;
    if (!runtime_validate_bms_frame(data, len, &function, &protocol_len) ||
        function != BMS_FRAME_TYPE_STATUS) {
        return false;
    }

    const uint8_t temperature_sensor_count = data[8];
    const uint8_t cell_count = data[9];
    if (cell_count > BMS_MAX_CELLS ||
        temperature_sensor_count > BMS_MAX_TEMPERATURE_SENSORS) {
        return false;
    }

    const size_t dynamic_offset = ((size_t)cell_count * 2U) +
                                  ((size_t)temperature_sensor_count * 2U);
    uint16_t pack_voltage_dv = 0;
    int16_t current_deci_amps = 0;
    uint16_t soc_percent = 0;
    uint32_t total_capacity_uah = 0;
    uint32_t capacity_remaining_uah = 0;
    uint64_t protection_mask = 0;
    uint64_t warning_mask = 0;
    uint16_t max_cell_mv = 0;
    uint16_t min_cell_mv = 0;
    uint16_t delta_cell_mv = 0;
    uint16_t average_cell_mv = 0;
    int16_t temperatures[ESP_BMS_BMS_TEMP_MAX_COUNT] = { 0 };
    bool temperature_valid[ESP_BMS_BMS_TEMP_MAX_COUNT] = { false };

    if (!runtime_read_u64_le(data, protocol_len, BMS_STATUS_PROTECTION_MASK_OFFSET, &protection_mask) ||
        !runtime_read_u64_le(data, protocol_len, BMS_STATUS_WARNING_MASK_OFFSET, &warning_mask) ||
        !runtime_read_u16_le(data, protocol_len, 38U + dynamic_offset, &pack_voltage_dv) ||
        !runtime_read_i16_le(data, protocol_len, 40U + dynamic_offset, &current_deci_amps) ||
        !runtime_read_u16_le(data, protocol_len, 42U + dynamic_offset, &soc_percent) ||
        !runtime_read_u32_le(data, protocol_len, 50U + dynamic_offset, &total_capacity_uah) ||
        !runtime_read_u32_le(data, protocol_len, 54U + dynamic_offset, &capacity_remaining_uah) ||
        !runtime_read_u16_le(data, protocol_len, 74U + dynamic_offset, &max_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 78U + dynamic_offset, &min_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 82U + dynamic_offset, &delta_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 84U + dynamic_offset, &average_cell_mv)) {
        return false;
    }

    const size_t temperature_offset = BMS_STATUS_DYNAMIC_BASE_OFFSET + ((size_t)cell_count * 2U);
    const uint8_t temperature_count = temperature_sensor_count > ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          ? ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          : temperature_sensor_count;
    for (uint8_t index = 0; index < temperature_count; index++) {
        if (!runtime_read_i16_le(data, protocol_len, temperature_offset + ((size_t)index * 2U), &temperatures[index])) {
            return false;
        }
        temperature_valid[index] = true;
    }
    if (!runtime_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset, &temperatures[4]) ||
        !runtime_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset + 2U, &temperatures[5])) {
        return false;
    }
    temperature_valid[4] = true;
    temperature_valid[5] = true;

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, true);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID, true);
    runtime->snapshot.pack_voltage_mv = (uint32_t)pack_voltage_dv * 10U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CURRENT_VALID, true);
    runtime->snapshot.current_deci_amps = current_deci_amps;
    runtime->bms_telemetry_last_us = esp_timer_get_time();
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SOC_VALID, true);
    runtime->snapshot.soc_percent = soc_percent;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID, true);
    runtime->snapshot.max_cell_voltage_mv = max_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID, true);
    runtime->snapshot.min_cell_voltage_mv = min_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID, true);
    runtime->snapshot.delta_cell_voltage_mv = delta_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID, true);
    runtime->snapshot.average_cell_voltage_mv = average_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID, true);
    runtime->snapshot.total_capacity_mah = total_capacity_uah / 1000U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID, true);
    runtime->snapshot.capacity_remaining_mah = capacity_remaining_uah / 1000U;
    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot, index, temperature_valid[index]);
    }
    memcpy(runtime->snapshot.bms_temperature_celsius,
           temperatures,
           sizeof(runtime->snapshot.bms_temperature_celsius));
    runtime_apply_bms_fault_masks(&runtime->snapshot, protection_mask, warning_mask);
    runtime_set_bms_info(runtime, "BMS OK");
    ESP_LOGI(TAG,
             "[bms] telemetry parsed: voltage=%lumV current_deci_amps=%d soc=%u%% temps=%u prot=%u warn=%u",
             (unsigned long)runtime->snapshot.pack_voltage_mv,
             (int)current_deci_amps,
             (unsigned)runtime->snapshot.soc_percent,
             (unsigned)temperature_count,
             (unsigned)runtime->snapshot.bms_protection_count,
             (unsigned)runtime->snapshot.bms_warning_count);
    return true;
}

static bool runtime_apply_bms_frame(esp_bms_idf_runtime_t *runtime,
                                    const uint8_t *data,
                                    size_t len)
{
    uint8_t function = 0;
    size_t protocol_len = 0;
    if (!runtime_validate_bms_frame(data, len, &function, &protocol_len)) {
        return false;
    }

    if (function == BMS_FRAME_TYPE_STATUS) {
        return runtime_apply_bms_status_frame(runtime, data, len);
    }
    if (function == BMS_FRAME_TYPE_DEVICE_INFO) {
        RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, true);
        ESP_LOGI(TAG, "[bms] device info parsed: len=%u", (unsigned)protocol_len);
        return true;
    }
    return false;
}

static bool runtime_bms_frame_push(esp_bms_idf_runtime_t *runtime,
                                   const uint8_t *chunk,
                                   size_t chunk_len)
{
    if (!runtime || !chunk || chunk_len == 0U) {
        return false;
    }

    if (chunk_len >= 2U && chunk[0] == BMS_FRAME_START_1 && chunk[1] == BMS_FRAME_START_2) {
        runtime->bms_frame_len = 0;
    } else if (runtime->bms_frame_len == 0U && chunk[0] != BMS_FRAME_START_1) {
        return false;
    }

    if ((size_t)runtime->bms_frame_len + chunk_len > sizeof(runtime->bms_frame)) {
        runtime->bms_frame_len = 0;
        return false;
    }

    memcpy(&runtime->bms_frame[runtime->bms_frame_len], chunk, chunk_len);
    runtime->bms_frame_len = (uint16_t)(runtime->bms_frame_len + chunk_len);

    if (runtime->bms_frame_len >= BMS_FRAME_MIN_LEN &&
        runtime->bms_frame[runtime->bms_frame_len - 2U] == BMS_FRAME_END_1 &&
        runtime->bms_frame[runtime->bms_frame_len - 1U] == BMS_FRAME_END_2) {
        const bool applied = runtime_apply_bms_frame(runtime,
                                                     runtime->bms_frame,
                                                     runtime->bms_frame_len);
        runtime->bms_frame_len = 0;
        return applied;
    }

    return true;
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

static bool runtime_parse_decimal_milli(const char *text, size_t len, uint32_t *out_milli)
{
    uint64_t whole = 0;
    uint32_t fraction = 0;
    uint32_t fraction_scale = 100;
    bool seen_digit = false;
    bool seen_decimal = false;

    for (size_t index = 0; index < len; index++) {
        const char value = text[index];
        if (value >= '0' && value <= '9') {
            seen_digit = true;
            if (seen_decimal) {
                if (fraction_scale > 0) {
                    fraction += (uint32_t)(value - '0') * fraction_scale;
                    fraction_scale /= 10;
                }
            } else {
                whole = (whole * 10U) + (uint32_t)(value - '0');
                if (whole > UINT32_MAX) {
                    return false;
                }
            }
        } else if (value == '.' && !seen_decimal) {
            seen_decimal = true;
        } else {
            return false;
        }
    }

    if (!seen_digit) {
        return false;
    }

    const uint64_t milli = (whole * 1000U) + fraction;
    if (milli > UINT32_MAX) {
        return false;
    }
    *out_milli = (uint32_t)milli;
    return true;
}

static bool runtime_field_equals(const char *field, size_t len, const char *expected)
{
    return strlen(expected) == len && memcmp(field, expected, len) == 0;
}

static bool runtime_is_rmc_kind(const char *field, size_t len)
{
    return runtime_field_equals(field, len, "GPRMC") ||
           runtime_field_equals(field, len, "GNRMC") ||
           runtime_field_equals(field, len, "GARMC") ||
           runtime_field_equals(field, len, "GLRMC") ||
           runtime_field_equals(field, len, "BDRMC");
}

static bool runtime_validate_nmea_checksum(const char *payload, size_t payload_len, const char *checksum)
{
    const int high = hex_value(checksum[0]);
    const int low = hex_value(checksum[1]);
    if (high < 0 || low < 0) {
        return false;
    }

    uint8_t actual = 0;
    const char *cursor = payload;
    const char *const end = payload + payload_len;
    while (cursor < end) {
        actual ^= (uint8_t)*cursor++;
    }
    return actual == (uint8_t)((high << 4) | low);
}

static bool runtime_parse_two_digits(const char *field, uint8_t *value)
{
    if (!isdigit((unsigned char)field[0]) || !isdigit((unsigned char)field[1])) {
        return false;
    }
    *value = (uint8_t)(((uint8_t)(field[0] - '0') * 10U) + (uint8_t)(field[1] - '0'));
    return true;
}

static bool runtime_parse_rmc_utc_time(const char *field,
                                       size_t len,
                                       gps_utc_time_t *utc)
{
    if (len < 6U || !runtime_parse_two_digits(field, &utc->hour) ||
        !runtime_parse_two_digits(field + 2, &utc->minute) ||
        !runtime_parse_two_digits(field + 4, &utc->second) || utc->hour > 23U ||
        utc->minute > 59U || utc->second > 60U) {
        return false;
    }
    if (len == 6U) {
        return true;
    }
    if (field[6] != '.' || len == 7U) {
        return false;
    }
    for (size_t index = 7U; index < len; ++index) {
        if (!isdigit((unsigned char)field[index])) {
            return false;
        }
    }
    return true;
}

static bool runtime_parse_rmc_utc_date(const char *field,
                                       size_t len,
                                       gps_utc_time_t *utc)
{
    uint8_t year = 0U;
    if (len != 6U || !runtime_parse_two_digits(field, &utc->day) ||
        !runtime_parse_two_digits(field + 2, &utc->month) ||
        !runtime_parse_two_digits(field + 4, &year) || utc->day == 0U ||
        utc->day > 31U || utc->month == 0U || utc->month > 12U) {
        return false;
    }
    utc->year = year >= 80U ? (uint16_t)(1900U + year) : (uint16_t)(2000U + year);
    return true;
}

static gps_parse_result_t runtime_parse_rmc(const uint8_t *line,
                                            size_t len,
                                            bool *fix_valid,
                                            uint32_t *speed_knots_milli,
                                            gps_utc_time_t *utc)
{
    if (len == 0) {
        return GPS_PARSE_IGNORE;
    }

    const char *payload = (const char *)line;
    size_t payload_len = len;
    if (payload_len > 0 && payload[0] == '$') {
        payload++;
        payload_len--;
    }

    for (size_t index = 0; index < payload_len; index++) {
        if (payload[index] == '*') {
            if ((payload_len - index) < 3U ||
                !runtime_validate_nmea_checksum(payload, index, &payload[index + 1])) {
                return GPS_PARSE_ERROR;
            }
            payload_len = index;
            break;
        }
    }

    bool is_rmc = false;
    bool status_seen = false;
    bool speed_seen = false;
    bool time_seen = false;
    bool date_seen = false;
    uint8_t status = 'V';
    uint32_t parsed_speed_milli = 0;
    size_t field_index = 0;
    size_t field_start = 0;

    for (size_t index = 0; index <= payload_len; index++) {
        if (index != payload_len && payload[index] != ',') {
            continue;
        }

        const char *field = &payload[field_start];
        const size_t field_len = index - field_start;
        switch (field_index) {
        case 0:
            is_rmc = runtime_is_rmc_kind(field, field_len);
            if (!is_rmc) {
                return GPS_PARSE_IGNORE;
            }
            break;
        case 1:
            if (!runtime_parse_rmc_utc_time(field, field_len, utc)) {
                return GPS_PARSE_ERROR;
            }
            time_seen = true;
            break;
        case 2:
            if (field_len == 0) {
                return GPS_PARSE_ERROR;
            }
            status = (uint8_t)field[0];
            status_seen = true;
            break;
        case 7:
            if (field_len == 0U && status_seen && status != 'A') {
                parsed_speed_milli = 0U;
            } else if (!runtime_parse_decimal_milli(field, field_len, &parsed_speed_milli)) {
                return GPS_PARSE_ERROR;
            }
            speed_seen = true;
            break;
        case 9:
            if (!runtime_parse_rmc_utc_date(field, field_len, utc)) {
                return GPS_PARSE_ERROR;
            }
            date_seen = true;
            break;
        default:
            break;
        }

        field_index++;
        field_start = index + 1U;
    }

    if (!status_seen || !speed_seen || !time_seen || !date_seen) {
        return GPS_PARSE_ERROR;
    }

    *fix_valid = status == 'A';
    *speed_knots_milli = parsed_speed_milli;
    return GPS_PARSE_FIX;
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
    snapshot->active_speed_source =
        snapshot->speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER && controller_online
            ? ESP_BMS_SPEED_SOURCE_CONTROLLER
            : ESP_BMS_SPEED_SOURCE_GPS;

    const bool speed_valid = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                                 ? runtime->controller_state.speed_valid
                                 : RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID);
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

    snapshot->gps_local_time_valid = false;
    if (runtime->gps_utc_valid && !runtime->gps_rmc_timed_out) {
        const esp_bms_gps_datetime_t utc = {
            .year = runtime->gps_utc_year,
            .month = runtime->gps_utc_month,
            .day = runtime->gps_utc_day,
            .hour = runtime->gps_utc_hour,
            .minute = runtime->gps_utc_minute,
            .second = runtime->gps_utc_second,
        };
        esp_bms_gps_datetime_t local = { 0 };
        if (esp_bms_gps_utc_to_local_utc8(&utc, &local)) {
            snapshot->gps_local_hour = local.hour;
            snapshot->gps_local_minute = local.minute;
            snapshot->gps_local_time_valid = true;
        }
    }

    snapshot->average_consumption_valid =
        esp_bms_trip_efficiency_consumption(&runtime->trip_efficiency,
                                            snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH,
                                            &snapshot->average_consumption_deci_wh_per_distance);
    if (!snapshot->average_consumption_valid) {
        snapshot->average_consumption_deci_wh_per_distance = 0;
    }
}

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
    runtime->gps_bytes_seen = 0;
    runtime->gps_parse_errors = 0;
    runtime->gps_speed_knots_milli = 0;
    runtime->gps_line_len = 0;
    runtime->gps_debug_lines_logged = 0U;
    runtime->gps_raw_sample_len = 0U;
    runtime->gps_pps_processed_count = runtime->gps_pps_isr_count;
    runtime->gps_pps_last_tick = 0U;
    runtime->gps_pps_last_summary_tick = 0U;
    runtime->gps_rmc_last_tick = 0U;
    runtime->gps_rmc_last_log_tick = 0U;
    runtime->bms_telemetry_last_us = 0;
    runtime->gps_pps_active = false;
    runtime->gps_pps_ever_seen = false;
    runtime->gps_rmc_seen = false;
    runtime->gps_rmc_timed_out = false;
    runtime->gps_utc_valid = false;
    runtime->gps_utc_logged = false;
    runtime->gps_uart_diagnostic_logged = false;
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
    runtime_bms_scan_clear_candidates(runtime);
    runtime_clear_bms_telemetry(runtime);
    runtime_update_setup_ap_snapshot(runtime);
    runtime_set_bms_info(runtime, "BMS OFF");
    runtime_update_snapshot_speed(runtime);
    runtime_project_controller_snapshot(runtime);
}

static void runtime_init_battery_adc(esp_bms_idf_runtime_t *runtime)
{
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

static void runtime_gps_pps_isr(void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    runtime->gps_pps_isr_count++;
}

static void runtime_init_gps_pps(esp_bms_idf_runtime_t *runtime)
{
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << GPS_PPS_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[gps] PPS GPIO%d config failed: %s", GPS_PPS_GPIO,
                 esp_err_to_name(ret));
        return;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[gps] PPS ISR service failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = gpio_isr_handler_add(GPS_PPS_GPIO, runtime_gps_pps_isr, runtime);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[gps] PPS GPIO%d handler failed: %s", GPS_PPS_GPIO,
                 esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "[gps] PPS ready: gpio=%d edge=rising pull=external", GPS_PPS_GPIO);
}

static void runtime_init_gps_uart(esp_bms_idf_runtime_t *runtime)
{
    uart_config_t config = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    runtime->gps_uart = GPS_UART_PORT;
    esp_err_t ret = uart_param_config(runtime->gps_uart, &config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART config failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = uart_set_pin(runtime->gps_uart, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART pin config failed: %s", esp_err_to_name(ret));
        return;
    }

    if (!uart_is_driver_installed(runtime->gps_uart)) {
        ret = uart_driver_install(runtime->gps_uart, GPS_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GPS UART driver install failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    RUNTIME_SET_FLAG(runtime, GPS_UART_READY, true);
    ESP_LOGI(TAG, "GPS UART ready: uart=%d rx=%d tx=%d baud=%d",
             runtime->gps_uart, GPS_UART_RX_GPIO, GPS_UART_TX_GPIO, GPS_UART_BAUD);
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
    uint8_t language = 0;
    uint8_t bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    uint8_t controller_connection_enabled = 0;
    uint8_t controller_page_enabled = 0;
    uint8_t controller_tire_rim_inch = 0;
    uint8_t controller_tire_aspect_percent = 0;
    uint16_t controller_tire_width_mm = 0;
    uint16_t controller_wheel_mm = 0;
    uint16_t controller_ratio_centi = 0;
    bool speed_source_migration_needed = false;

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
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_CONNECTION_KEY,
                                          &controller_connection_enabled);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_PAGE_KEY,
                                          &controller_page_enabled);
    }
    if (ret == ESP_OK) {
        const esp_err_t source_ret = nvs_get_u8(handle,
                                                DISPLAY_NVS_SPEED_SOURCE_KEY,
                                                &speed_source);
        if (source_ret == ESP_ERR_NVS_NOT_FOUND) {
            speed_source = controller_page_enabled != 0U
                               ? (uint8_t)ESP_BMS_SPEED_SOURCE_CONTROLLER
                               : (uint8_t)ESP_BMS_SPEED_SOURCE_GPS;
            speed_source_migration_needed = true;
        } else {
            ret = source_ret;
        }
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

    if (!runtime_brightness_matches_policy(brightness_percent) ||
        !runtime_volume_matches_policy(volume_percent) ||
        !runtime_rotation_matches_policy(rotation) ||
        !runtime_speed_unit_matches_policy(speed_unit) ||
        !runtime_speed_source_matches_policy(speed_source) ||
        !runtime_language_matches_policy(language) ||
        !runtime_bms_type_matches_policy(bms_type) ||
        controller_connection_enabled > 1U || controller_page_enabled > 1U) {
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
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, language != 0U);
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime->controller_page_enabled =
        runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER;
    runtime->controller_connection_enabled = controller_connection_enabled != 0U;
    runtime->controller_fallback_tire_rim_inch = controller_tire_rim_inch;
    runtime->controller_fallback_tire_aspect_percent = controller_tire_aspect_percent;
    runtime->controller_fallback_tire_width_mm = controller_tire_width_mm;
    runtime->controller_state.fallback_wheel_circumference_mm = controller_wheel_mm;
    runtime->controller_state.fallback_gear_ratio_centi = controller_ratio_centi;
    runtime_project_controller_snapshot(runtime);
    runtime_update_snapshot_speed(runtime);
    *loaded = true;
    if (speed_source_migration_needed) {
        const esp_err_t migration_ret = esp_bms_idf_runtime_save_display_settings(runtime);
        if (migration_ret != ESP_OK) {
            ESP_LOGW(TAG, "[settings] speed source migration save failed: %s",
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
    ESP_RETURN_ON_FALSE(runtime_bms_type_matches_policy(runtime->bms_type),
                        ESP_ERR_INVALID_STATE, TAG, "invalid BMS type");
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
        ret = nvs_set_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, runtime->bms_type);
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
    runtime->controller_page_enabled = speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER;
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
                                 "Content-Type");
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

static esp_err_t runtime_http_index_handler(httpd_req_t *req)
{
    const size_t html_size = (size_t)(web_index_html_end - web_index_html_start);
    const size_t html_len = html_size > 0U && web_index_html_start[html_size - 1U] == '\0'
                                ? html_size - 1U
                                : html_size;
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/html; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, web_index_html_start, (ssize_t)html_len);
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

static esp_err_t runtime_http_cast_ws_handler(httpd_req_t *req)
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
        runtime_cast_stop(runtime, "invalid frame");
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
        runtime_cast_stop(runtime, "unknown message");
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
        runtime_cast_stop(runtime, "block out of bounds");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(esp_bms_lvgl_bridge_lock(-1), TAG, "lock display for cast failed");
    const esp_err_t ret = esp_bms_lvgl_bridge_write_rgb565(x, y, width, height,
                                                           &message[CAST_BLOCK_HEADER_BYTES], pixel_bytes);
    esp_bms_lvgl_bridge_unlock();
    return ret;
}

static esp_err_t runtime_http_api_handler(httpd_req_t *req)
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
    ESP_LOGI(TAG, "[http] route not implemented: method=%d uri=%s", req->method, req->uri);
    return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
}

static esp_err_t runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, HTTP_SERVER_STARTED)) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.max_uri_handlers = 5;
    config.stack_size = 4096;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&runtime->http_server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = runtime_http_index_handler,
        .user_ctx = runtime,
    };
    const httpd_uri_t api = {
        .uri = "/api/*",
        .method = HTTP_ANY,
        .handler = runtime_http_api_handler,
        .user_ctx = runtime,
    };
    const httpd_uri_t cast = {
        .uri = "/cast",
        .method = HTTP_GET,
        .handler = runtime_http_cast_ws_handler,
        .user_ctx = runtime,
        .is_websocket = true,
    };

    ret = httpd_register_uri_handler(runtime->http_server, &root);
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(runtime->http_server, &api);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(runtime->http_server, &cast);
    }
    if (ret != ESP_OK) {
        httpd_stop(runtime->http_server);
        runtime->http_server = NULL;
        return ret;
    }

    RUNTIME_SET_FLAG(runtime, HTTP_SERVER_STARTED, true);
    ESP_LOGI(TAG, "[http] server started: port=80 routes=/,/api/*,/cast");
    runtime_log_heap_state("http_server_started");
    return ESP_OK;
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

static esp_err_t runtime_load_bms_binding(esp_bms_idf_runtime_t *runtime)
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
        ret = runtime_apply_setup_ap_wifi_config(runtime);
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
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(runtime_apply_setup_ap_wifi_config(runtime));
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
    runtime_clear_bms_telemetry(runtime);
    runtime_bms_scan_clear_candidates(runtime);

    const esp_err_t ret = esp_bms_idf_runtime_start_bms_ble_for_bind(runtime);
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
        runtime_clear_bms_telemetry(runtime);
        ESP_LOGI(TAG, "[bms] bound MAC saved: mac=%s", mac);
        const esp_err_t scan_ret = esp_bms_idf_runtime_start_bms_ble_for_bind(runtime);
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

static esp_err_t runtime_configure_setup_ap_ip(esp_netif_t *netif)
{
    esp_err_t ret = esp_netif_dhcps_stop(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return ret;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_netif_set_ip4_addr(&ip_info.ip, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.gw, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);
    ret = esp_netif_set_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_dhcps_start(netif);
    if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return ESP_OK;
    }
    return ret;
}

static void runtime_bms_ble_reset_connection_state(esp_bms_idf_runtime_t *runtime,
                                                   bms_ble_phase_t phase)
{
    runtime->bms_ble_phase = (uint8_t)phase;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    runtime->bms_char_val_handle = 0;
    runtime->bms_cccd_handle = 0;
    runtime->bms_frame_len = 0;
    runtime->bms_status_poll_elapsed_ms = 0;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, false);
}

static int runtime_bms_ble_write_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    struct ble_gatt_attr *attr,
                                    void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }

    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    if (error && error->status != 0) {
        runtime_set_bms_info(runtime, "BMS WR");
        ESP_LOGW(TAG, "[bms] GATT write failed: conn=%u status=%u",
                 conn_handle, (unsigned)error->status);
        return 0;
    }

    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SUBSCRIBING) {
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_ONLINE;
        runtime_set_bms_info(runtime, "BMS ON");
        ESP_LOGI(TAG, "[bms] notifications subscribed: conn=%u cccd=%u",
                 conn_handle, runtime->bms_cccd_handle);
        const esp_err_t poll_ret = runtime_bms_ble_send_poll_request(runtime, false);
        if (poll_ret != ESP_OK) {
            runtime->bms_status_poll_elapsed_ms = BMS_STATUS_POLL_PERIOD_MS;
            ESP_LOGW(TAG, "[bms] initial status poll failed: %s", esp_err_to_name(poll_ret));
        }
    }
    return 0;
}

static esp_err_t runtime_bms_ble_write_frame(esp_bms_idf_runtime_t *runtime,
                                             const uint8_t *frame,
                                             size_t frame_len)
{
    if (!runtime || !frame || frame_len == 0U ||
        runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_char_val_handle == 0U ||
        RUNTIME_FLAG(runtime, BMS_WRITE_IN_FLIGHT)) {
        return ESP_ERR_INVALID_STATE;
    }

    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_char_val_handle,
                                        frame,
                                        (uint16_t)frame_len,
                                        runtime_bms_ble_write_cb,
                                        runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
    return ESP_OK;
}

static esp_err_t runtime_bms_ble_send_poll_request(esp_bms_idf_runtime_t *runtime,
                                                   bool include_device_info)
{
    static const uint8_t status_request[] = {
        0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55,
    };
    static const uint8_t device_info_request[] = {
        0x7E, 0xA1, 0x02, 0x6C, 0x02, 0x20, 0x58, 0xC4, 0xAA, 0x55,
    };

    if (!runtime || runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *frame = status_request;
    size_t frame_len = sizeof(status_request);
    const bool send_device_info = include_device_info &&
                                  !RUNTIME_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED);
    if (send_device_info) {
        frame = device_info_request;
        frame_len = sizeof(device_info_request);
    }

    const esp_err_t ret = runtime_bms_ble_write_frame(runtime, frame, frame_len);
    if (ret == ESP_OK) {
        runtime->bms_status_poll_elapsed_ms = 0;
        if (send_device_info) {
            RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, true);
        }
        ESP_LOGI(TAG, "[bms] poll sent: type=%s conn=%u handle=%u len=%u",
                 send_device_info ? "device-info" : "status",
                 runtime->bms_conn_handle,
                 runtime->bms_char_val_handle,
                 (unsigned)frame_len);
    }
    return ret;
}

static esp_err_t runtime_bms_ble_subscribe(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_cccd_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t value[2] = { 1, 0 };
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SUBSCRIBING;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_cccd_handle,
                                        value,
                                        sizeof(value),
                                        runtime_bms_ble_write_cb,
                                        runtime);
    if (rc != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int runtime_bms_ble_dsc_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  uint16_t chr_val_handle,
                                  const struct ble_gatt_dsc *dsc,
                                  void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        chr_val_handle != runtime->bms_char_val_handle) {
        return 0;
    }

    if (error && error->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            runtime->bms_cccd_handle = dsc->handle;
            ESP_LOGI(TAG, "[bms] CCCD discovered: conn=%u value_handle=%u cccd=%u",
                     conn_handle, chr_val_handle, dsc->handle);
        }
        return 0;
    }

    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_cccd_handle == 0U) {
            runtime_set_bms_info(runtime, "BMS NO CCCD");
            ESP_LOGW(TAG, "[bms] characteristic lacks CCCD: conn=%u val_handle=%u",
                     conn_handle, runtime->bms_char_val_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        if (runtime_bms_ble_subscribe(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS SUB");
            ESP_LOGW(TAG, "[bms] subscribe request failed: conn=%u cccd=%u",
                     conn_handle, runtime->bms_cccd_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS DSC");
    ESP_LOGW(TAG, "[bms] descriptor discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_descriptor_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_char_val_handle == 0U ||
        runtime->bms_char_val_handle >= runtime->bms_service_end_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD;
    runtime->bms_cccd_handle = 0;
    ESP_LOGI(TAG, "[bms] descriptor discovery: conn=%u start=%u end=%u",
             runtime->bms_conn_handle,
             runtime->bms_char_val_handle,
             runtime->bms_service_end_handle);
    const int rc = ble_gattc_disc_all_dscs(runtime->bms_conn_handle,
                                           runtime->bms_char_val_handle,
                                           runtime->bms_service_end_handle,
                                           runtime_bms_ble_dsc_cb,
                                           runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static int runtime_bms_ble_chr_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr,
                                  void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }

    if (error && error->status == 0 && chr) {
        runtime->bms_char_val_handle = chr->val_handle;
        ESP_LOGI(TAG, "[bms] characteristic FFE1 discovered: def=%u value=%u props=0x%02x",
                 chr->def_handle, chr->val_handle, chr->properties);
        if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0U) {
            ESP_LOGW(TAG, "[bms] characteristic has no notify property: props=0x%02x",
                     chr->properties);
        }
        if ((chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) == 0U) {
            ESP_LOGW(TAG, "[bms] characteristic has no write property: props=0x%02x",
                     chr->properties);
        }
        return 0;
    }

    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_char_val_handle == 0U ||
            runtime_bms_ble_start_descriptor_discovery(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS NO CHR");
            ESP_LOGW(TAG, "[bms] characteristic discovery failed or missing: conn=%u",
                     conn_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS CHR");
    ESP_LOGW(TAG, "[bms] characteristic discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_service_start_handle == 0U ||
        runtime->bms_service_end_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_uuid16_t characteristic_uuid = BLE_UUID16_INIT(ANT_BMS_CHARACTERISTIC_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
    runtime->bms_char_val_handle = 0;
    const int rc = ble_gattc_disc_chrs_by_uuid(runtime->bms_conn_handle,
                                               runtime->bms_service_start_handle,
                                               runtime->bms_service_end_handle,
                                               &characteristic_uuid.u,
                                               runtime_bms_ble_chr_cb,
                                               runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static int runtime_bms_ble_service_cb(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      const struct ble_gatt_svc *service,
                                      void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }

    if (error && error->status == 0 && service) {
        runtime->bms_service_start_handle = service->start_handle;
        runtime->bms_service_end_handle = service->end_handle;
        ESP_LOGI(TAG, "[bms] service FFE0 discovered: start=%u end=%u",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_service_start_handle == 0U ||
            runtime_bms_ble_start_characteristic_discovery(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS NO SVC");
            ESP_LOGW(TAG, "[bms] service discovery failed or missing: conn=%u",
                     conn_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS SVC");
    ESP_LOGW(TAG, "[bms] service discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_service_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_uuid16_t service_uuid = BLE_UUID16_INIT(ANT_BMS_SERVICE_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    const int rc = ble_gattc_disc_svc_by_uuid(runtime->bms_conn_handle,
                                              &service_uuid.u,
                                              runtime_bms_ble_service_cb,
                                              runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static int runtime_controller_write_cb(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr,
                                       void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "[controller] GATT write failed: status=%u", (unsigned)error->status);
    }
    return 0;
}

static void runtime_controller_send_gather(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_char_val_handle == 0U) {
        return;
    }
    uint8_t frame[8] = { 0xAA, 0x46, 0xA0, 0xA0, 0x06, 0x88 };
    const uint16_t crc = esp_fardriver_crc(frame, sizeof(frame) - 2U);
    frame[6] = (uint8_t)(crc >> 8U);
    frame[7] = (uint8_t)crc;
    (void)ble_gattc_write_flat(runtime->controller_conn_handle,
                               runtime->controller_char_val_handle,
                               frame,
                               sizeof(frame),
                               runtime_controller_write_cb,
                               runtime);
    runtime->controller_keepalive_elapsed_ms = 0U;
}

static void runtime_controller_set_subscription(esp_bms_idf_runtime_t *runtime, bool enabled)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_cccd_handle == 0U ||
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED) == enabled) {
        return;
    }
    const uint8_t value[2] = { enabled ? 1U : 0U, 0U };
    if (ble_gattc_write_flat(runtime->controller_conn_handle,
                            runtime->controller_cccd_handle,
                            value,
                            sizeof(value),
                            runtime_controller_write_cb,
                            runtime) == 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, enabled);
        if (enabled) {
            runtime_controller_send_gather(runtime);
        }
    }
}

static int runtime_controller_dsc_cb(uint16_t conn_handle,
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
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_cccd_handle != 0U) {
        runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_ONLINE;
        runtime_project_controller_snapshot(runtime);
        runtime_controller_set_subscription(runtime, true);
        return 0;
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int runtime_controller_chr_cb(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     const struct ble_gatt_chr *chr,
                                     void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        runtime->controller_char_val_handle = chr->val_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_char_val_handle != 0U) {
        runtime->controller_cccd_handle = 0U;
        runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD;
        if (ble_gattc_disc_all_dscs(conn_handle,
                                   runtime->controller_char_val_handle,
                                   runtime->controller_service_end_handle,
                                   runtime_controller_dsc_cb,
                                   runtime) == 0) {
            return 0;
        }
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int runtime_controller_service_cb(uint16_t conn_handle,
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
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_service_start_handle != 0U) {
        ble_uuid16_t uuid = BLE_UUID16_INIT(FARDRIVER_CHARACTERISTIC_UUID_16);
        runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
        if (ble_gattc_disc_chrs_by_uuid(conn_handle,
                                       runtime->controller_service_start_handle,
                                       runtime->controller_service_end_handle,
                                       &uuid.u,
                                       runtime_controller_chr_cb,
                                       runtime) == 0) {
            return 0;
        }
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_controller_connect(esp_bms_idf_runtime_t *runtime,
                                            const struct ble_gap_disc_desc *disc)
{
    if (!runtime || !disc || runtime->controller_conn_handle != 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }
    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0 ||
        ble_gap_connect(own_addr_type,
                        &disc->addr,
                        BMS_CONNECT_TIMEOUT_MS,
                        NULL,
                        runtime_controller_gap_event,
                        runtime) != 0) {
        return ESP_FAIL;
    }
    runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_CONNECTING;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    return ESP_OK;
}

static int runtime_controller_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            if (!runtime->controller_connection_enabled) {
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            runtime->controller_conn_handle = event->connect.conn_handle;
            __atomic_fetch_or(&runtime->pending_audio_events,
                              ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED,
                              __ATOMIC_RELAXED);
            runtime->controller_service_start_handle = 0U;
            runtime->controller_service_end_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
            ble_uuid16_t uuid = BLE_UUID16_INIT(FARDRIVER_SERVICE_UUID_16);
            if (ble_gattc_disc_svc_by_uuid(event->connect.conn_handle,
                                          &uuid.u,
                                          runtime_controller_service_cb,
                                          runtime) != 0) {
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_BACKOFF;
            runtime_clear_controller_telemetry(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->controller_conn_handle) {
            runtime->controller_conn_handle = 0xFFFFU;
            runtime->controller_cccd_handle = 0U;
            runtime->controller_char_val_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)BMS_BLE_PHASE_BACKOFF;
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            runtime_clear_controller_telemetry(runtime);
            if (runtime->controller_connection_enabled &&
                RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
                const esp_err_t ret = runtime_controller_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[controller] deferred rebind scan failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.conn_handle == runtime->controller_conn_handle &&
            event->notify_rx.attr_handle == runtime->controller_char_val_handle) {
            uint8_t frame[ESP_FARDRIVER_FRAME_LEN];
            const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len == (int)sizeof(frame) &&
                os_mbuf_copydata(event->notify_rx.om, 0, len, frame) == 0) {
                const esp_fardriver_layout_t layout = (frame[1] & 0x3FU) <= 29U
                                                           ? ESP_FARDRIVER_LAYOUT_COMPACT
                                                           : ESP_FARDRIVER_LAYOUT_EXTENDED;
                if (esp_fardriver_parse_frame(&runtime->controller_state,
                                              frame,
                                              sizeof(frame),
                                              layout)) {
                    runtime_sync_controller_parameters(runtime);
                    runtime_project_controller_snapshot(runtime);
                }
            }
        }
        return 0;
    default:
        return 0;
    }
}

static esp_err_t runtime_bms_ble_connect_to_disc(esp_bms_idf_runtime_t *runtime,
                                                 const struct ble_gap_disc_desc *disc,
                                                 const char *mac)
{
    if (!runtime || !disc || !mac || runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING ||
        runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_gap_disc_active()) {
        const int cancel_rc = ble_gap_disc_cancel();
        if (cancel_rc != 0) {
            return ESP_FAIL;
        }
    }

    uint8_t own_addr_type = 0;
    const int addr_rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (addr_rc != 0) {
        return ESP_FAIL;
    }

    const int rc = ble_gap_connect(own_addr_type,
                                   &disc->addr,
                                   BMS_CONNECT_TIMEOUT_MS,
                                   NULL,
                                   runtime_bms_ble_gap_event,
                                   runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    runtime->bms_own_addr_type = own_addr_type;
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_CONNECTING;
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    runtime_set_bms_info(runtime, "BMS CONN");
    ESP_LOGI(TAG, "[bms] connecting to bound BMS: mac=%s addr_type=%u",
             mac, disc->addr.type);
    return ESP_OK;
}

static void runtime_bms_ble_handle_notification(esp_bms_idf_runtime_t *runtime,
                                                const struct ble_gap_event *event)
{
    if (!runtime || !event || event->notify_rx.conn_handle != runtime->bms_conn_handle ||
        event->notify_rx.attr_handle != runtime->bms_char_val_handle) {
        return;
    }

    const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len <= 0 || len > (int)sizeof(runtime->bms_frame)) {
        runtime_set_bms_info(runtime, "BMS RX LEN");
        ESP_LOGW(TAG, "[bms] invalid notification length: len=%d", len);
        return;
    }

    ESP_LOGI(TAG, "[bms] notification received: attr=%u len=%d",
             event->notify_rx.attr_handle, len);

    uint8_t chunk[ESP_BMS_IDF_BMS_FRAME_MAX_LEN] = { 0 };
    const int rc = os_mbuf_copydata(event->notify_rx.om, 0, len, chunk);
    if (rc != 0 || !runtime_bms_frame_push(runtime, chunk, (size_t)len)) {
        runtime_set_bms_info(runtime, "BMS RX");
        ESP_LOGW(TAG, "[bms] notification parse failed: len=%d rc=%d", len, rc);
    }
}

static void runtime_controller_store_candidate(esp_bms_idf_runtime_t *runtime,
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
        runtime_bms_scan_cache_name_locked(mac, name);
    } else {
        name = runtime_bms_scan_cached_name_locked(mac);
    }
    bool changed = false;
    if (name && name[0] != '\0' && strcmp(mac, runtime->controller_bound_mac) == 0 &&
        strcmp(name, runtime->controller_bound_name) != 0) {
        runtime_copy_snapshot_text(runtime->controller_bound_name,
                                   sizeof(runtime->controller_bound_name),
                                   name);
        changed = true;
    }
    for (uint8_t index = 0; index < runtime->controller_scan_candidate_count; ++index) {
        if (strcmp(runtime->controller_scan_candidates[index].mac, mac) == 0) {
            runtime->controller_scan_candidates[index].rssi = rssi;
            if (name && name[0] != '\0' &&
                (!runtime->controller_scan_candidates[index].has_name ||
                 strcmp(runtime->controller_scan_candidates[index].name, name) != 0)) {
                runtime_copy_snapshot_text(runtime->controller_scan_candidates[index].name,
                                           sizeof(runtime->controller_scan_candidates[index].name),
                                           name);
                runtime->controller_scan_candidates[index].has_name = true;
                changed = true;
            }
            if (runtime->bms_scan_lock) {
                xSemaphoreGive(runtime->bms_scan_lock);
            }
            if (changed) {
                runtime_project_controller_snapshot(runtime);
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
    runtime_copy_snapshot_text(candidate->mac, sizeof(candidate->mac), mac);
    runtime_copy_snapshot_text(candidate->name, sizeof(candidate->name), name);
    candidate->has_name = name && name[0] != '\0';
    candidate->rssi = rssi;
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    runtime_project_controller_snapshot(runtime);
}

static int runtime_bms_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields = { 0 };
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
        char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U] = { 0 };
        runtime_ble_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = runtime_bms_name_copy(name, sizeof(name), fields.name, fields.name_len);
        const bool matches_binding = RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE) &&
                                     runtime->bms_bound_mac[0] != '\0' &&
                                     strcmp(mac, runtime->bms_bound_mac) == 0;
        const int8_t rssi = event->disc.rssi == 127 ? INT8_MIN : event->disc.rssi;
        if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE)) {
            runtime_bms_scan_store_candidate(runtime, mac, has_name ? name : NULL, rssi);
        }
        if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE)) {
            runtime_controller_store_candidate(runtime, mac, has_name ? name : NULL, rssi);
            if (runtime->controller_connection_enabled &&
                runtime->controller_bound_mac[0] != '\0' &&
                strcmp(mac, runtime->controller_bound_mac) == 0) {
                (void)runtime_controller_connect(runtime, &event->disc);
            }
        }
        if (matches_binding) {
            const esp_err_t ret = runtime_bms_ble_connect_to_disc(runtime, &event->disc, mac);
            if (ret != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS CONN ERR");
                ESP_LOGW(TAG, "[bms] connect request failed: mac=%s ret=%s",
                         mac, esp_err_to_name(ret));
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            runtime->bms_conn_handle = event->connect.conn_handle;
            __atomic_fetch_or(&runtime->pending_audio_events,
                              ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED,
                              __ATOMIC_RELAXED);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
            runtime_set_bms_info(runtime, "BMS DISC");
            ESP_LOGI(TAG, "[bms] connected: conn=%u", event->connect.conn_handle);
            if (runtime_bms_ble_start_service_discovery(runtime) != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS SVC");
                ESP_LOGW(TAG, "[bms] service discovery request failed: conn=%u",
                         event->connect.conn_handle);
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            runtime_clear_bms_telemetry(runtime);
            runtime_set_bms_info(runtime, "BMS CONN FAIL");
            ESP_LOGW(TAG, "[bms] connection failed: status=%d", event->connect.status);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bms_conn_handle ||
            runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SCANNING) {
            const bool scan_requested = RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED);
            runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            runtime_clear_bms_telemetry(runtime);
            runtime_set_bms_info(runtime, "BMS OFF");
            ESP_LOGW(TAG, "[bms] disconnected: reason=%d", event->disconnect.reason);
            if (scan_requested) {
                RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
                const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] deferred scan after disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        runtime_project_controller_snapshot(runtime);
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SCANNING) {
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_IDLE;
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
            runtime_set_bms_info(runtime, "BMS DONE");
        }
        ESP_LOGI(TAG, "[bms] BLE scan complete: reason=%d candidates=%u",
                 event->disc_complete.reason, runtime->bms_scan_candidate_count);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        runtime_bms_ble_handle_notification(runtime, event);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "[bms] MTU updated: conn=%u mtu=%u",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static esp_err_t runtime_bms_ble_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, BMS_BLE_READY) || !RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_ERR_INVALID_STATE;
    }

    if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) || ble_gap_disc_active()) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, true);
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SCANNING;
        runtime_set_bms_info(runtime, "BMS SCAN");
        ESP_LOGI(TAG, "[bms] BLE scan request reused active discovery");
        return ESP_OK;
    }

    uint8_t own_addr_type = 0;
    const int addr_rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (addr_rc != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_FAIL;
    }

    runtime->bms_own_addr_type = own_addr_type;
    struct ble_gap_disc_params disc_params = { 0 };
    disc_params.filter_duplicates = 0;
    disc_params.passive = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    const int rc = ble_gap_disc(own_addr_type,
                                BMS_SCAN_DURATION_MS,
                                &disc_params,
                                runtime_bms_ble_gap_event,
                                runtime);
    if (rc != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
        return ESP_FAIL;
    }

    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, true);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SCANNING;
    runtime_set_bms_info(runtime, "BMS SCAN");
    ESP_LOGI(TAG, "[bms] BLE scan started: duration_ms=%u", (unsigned)BMS_SCAN_DURATION_MS);
    return ESP_OK;
}

static esp_err_t runtime_controller_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(runtime_init_bms_ble(runtime), TAG, "NimBLE init failed");
    if (!RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
        return ESP_OK;
    }
    runtime->controller_scan_candidate_count = 0U;
    memset(runtime->controller_scan_candidates, 0, sizeof(runtime->controller_scan_candidates));
    runtime->controller_scan_revision++;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, true);
    if (ble_gap_disc_active()) {
        runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    const struct ble_gap_disc_params params = {
        .filter_duplicates = 0,
        .passive = 0,
        .filter_policy = 0,
        .limited = 0,
    };
    if (ble_gap_disc(own_addr_type,
                     BMS_SCAN_DURATION_MS,
                     &params,
                     runtime_bms_ble_gap_event,
                     runtime) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    runtime_project_controller_snapshot(runtime);
    return ESP_OK;
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
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, true);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT CONN");
            ESP_LOGI(TAG, "[bt] local Bluetooth connected: conn=%u", event->connect.conn_handle);
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
            runtime->bluetooth_conn_handle = 0xFFFFU;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, !start_bms_scan);
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT OFF");
            ESP_LOGI(TAG, "[bt] local Bluetooth disconnected: reason=%d", event->disconnect.reason);
            if (start_bms_scan) {
                const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] scan after local Bluetooth disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
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

static esp_err_t runtime_bms_ble_start_scan_or_defer(esp_bms_idf_runtime_t *runtime)
{
    const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
    if (ret == ESP_ERR_INVALID_STATE && runtime && RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        return ESP_OK;
    }
    return ret;
}

static void runtime_bms_ble_on_reset(int reason)
{
    esp_bms_idf_runtime_t *runtime = s_bms_ble_runtime;
    if (runtime) {
        RUNTIME_SET_FLAG(runtime, BMS_BLE_SYNCED, false);
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
        runtime->bluetooth_conn_handle = 0xFFFFU;
        (void)runtime_project_bluetooth_snapshot(runtime);
    }
    ESP_LOGW(TAG, "[bms] NimBLE reset: reason=%d", reason);
}

static void runtime_bms_ble_on_sync(void)
{
    esp_bms_idf_runtime_t *runtime = s_bms_ble_runtime;
    if (!runtime) {
        return;
    }
    RUNTIME_SET_FLAG(runtime, BMS_BLE_SYNCED, true);
    ESP_LOGI(TAG, "[bms] NimBLE synced");
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
        const esp_err_t ret = runtime_controller_start_scan(runtime);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[controller] deferred scan failed: %s", esp_err_to_name(ret));
        }
    } else if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
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

static void runtime_bms_ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "[bms] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t runtime_init_bms_ble(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, BMS_BLE_READY)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_bms_ble_runtime = runtime;
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

    ble_hs_cfg.reset_cb = runtime_bms_ble_on_reset;
    ble_hs_cfg.sync_cb = runtime_bms_ble_on_sync;

    BaseType_t task_ret = xTaskCreate(runtime_bms_ble_host_task,
                                      "bms-nimble",
                                      BMS_SCAN_HOST_TASK_STACK,
                                      NULL,
                                      BMS_SCAN_HOST_TASK_PRIORITY,
                                      NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    RUNTIME_SET_FLAG(runtime, BMS_BLE_READY, true);
    RUNTIME_SET_FLAG(runtime, BMS_BLE_HOST_STARTED, true);
    ESP_LOGI(TAG, "[bms] NimBLE initialized");
    return ESP_OK;
}

static void runtime_wifi_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_START) {
        RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, true);
        ESP_LOGI(TAG, "[wifi] AP started: ip=192.168.4.1 dhcp=on");
        return;
    }
    if (event_id == WIFI_EVENT_AP_STOP) {
        RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, false);
        runtime->setup_ap_clients = 0;
        ESP_LOGI(TAG, "[wifi] AP stopped");
        return;
    }
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        if (runtime->setup_ap_clients < UINT8_MAX) {
            runtime->setup_ap_clients++;
        }
        ESP_LOGI(TAG, "[wifi] AP client connected: clients=%u first_mac=" MACSTR,
                 runtime->setup_ap_clients, MAC2STR(event->mac));
        runtime_log_heap_state("ap_client_connected");
        return;
    }
    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)event_data;
        if (runtime->setup_ap_clients > 0) {
            runtime->setup_ap_clients--;
        }
        ESP_LOGI(TAG, "[wifi] AP client disconnected: clients=%u mac=" MACSTR " reason=%u",
                 runtime->setup_ap_clients, MAC2STR(event->mac), event->reason);
        runtime_log_heap_state("ap_client_disconnected");
        return;
    }
}

static void runtime_ip_event_handler(void *arg,
                                     esp_event_base_t event_base,
                                     int32_t event_id,
                                     void *event_data)
{
    (void)arg;
    if (event_base != IP_EVENT) {
        return;
    }

    if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *event = (const ip_event_ap_staipassigned_t *)event_data;
        char ip[16] = { 0 };
        ESP_LOGI(TAG, "[wifi] DHCP lease assigned: ip=%s mac=" MACSTR,
                 esp_ip4addr_ntoa(&event->ip, ip, sizeof(ip)), MAC2STR(event->mac));
        return;
    }
}

static esp_err_t runtime_register_wifi_handlers(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, WIFI_HANDLERS_REGISTERED)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            runtime_wifi_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "Wi-Fi event handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            runtime_ip_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "IP event handler registration failed");
    RUNTIME_SET_FLAG(runtime, WIFI_HANDLERS_REGISTERED, true);
    return ESP_OK;
}

static esp_err_t runtime_init_wifi_stack(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, WIFI_STACK_READY)) {
        ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        runtime->setup_ap_netif = esp_netif_create_default_wifi_ap();
        if (!runtime->setup_ap_netif) {
            return ESP_ERR_NO_MEM;
        }
        ESP_RETURN_ON_ERROR(runtime_configure_setup_ap_ip(runtime->setup_ap_netif),
                            TAG,
                            "setup AP IP config failed");

        RUNTIME_SET_FLAG(runtime, WIFI_STACK_READY, true);
    }

    if (!RUNTIME_FLAG(runtime, WIFI_DRIVER_READY)) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
        RUNTIME_SET_FLAG(runtime, WIFI_DRIVER_READY, true);
    }

    return runtime_register_wifi_handlers(runtime);
}

static esp_err_t runtime_apply_setup_ap_wifi_config(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_setup_ap_ssid_matches_policy(runtime->setup_ap_ssid) ||
        !runtime_setup_ap_password_matches_policy(runtime->setup_ap_password)) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = { 0 };
    const size_t ssid_len = strlen(runtime->setup_ap_ssid);
    const size_t password_len = strlen(runtime->setup_ap_password);
    memcpy(wifi_config.ap.ssid, runtime->setup_ap_ssid, ssid_len);
    memcpy(wifi_config.ap.password, runtime->setup_ap_password, password_len);
    wifi_config.ap.ssid_len = (uint8_t)ssid_len;
    wifi_config.ap.channel = SETUP_AP_CHANNEL;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.max_connection = SETUP_AP_MAX_CONNECTIONS;
    wifi_config.ap.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

static bool runtime_wifi_started(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && RUNTIME_FLAG(runtime, SETUP_AP_STARTED);
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

static bool runtime_apply_gps_line(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->gps_line_len == 0) {
        return false;
    }

    if (runtime->gps_debug_lines_logged < 8U) {
        ESP_LOGI(TAG,
                 "[gps] NMEA sample %u: %.*s",
                 (unsigned)(runtime->gps_debug_lines_logged + 1U),
                 (int)runtime->gps_line_len,
                 (const char *)runtime->gps_line);
        runtime->gps_debug_lines_logged++;
    }

    bool fix_valid = false;
    uint32_t speed_knots_milli = 0;
    gps_utc_time_t utc = { 0 };
    const gps_parse_result_t result =
        runtime_parse_rmc(runtime->gps_line,
                          runtime->gps_line_len,
                          &fix_valid,
                          &speed_knots_milli,
                          &utc);
    if (result == GPS_PARSE_IGNORE) {
        return false;
    }
    if (result == GPS_PARSE_ERROR) {
        if (runtime->gps_parse_errors == 0U) {
            ESP_LOGW(TAG,
                     "[gps] invalid RMC: %.*s",
                     (int)runtime->gps_line_len,
                     (const char *)runtime->gps_line);
        }
        runtime->gps_parse_errors++;
        return false;
    }

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, fix_valid);
    runtime->gps_speed_knots_milli = speed_knots_milli;
    runtime->snapshot.gps_sentences_seen++;
    runtime->gps_rmc_seen = true;
    runtime->gps_rmc_timed_out = false;
    runtime->gps_rmc_last_tick = runtime->tick_count;
    runtime->gps_utc_year = utc.year;
    runtime->gps_utc_month = utc.month;
    runtime->gps_utc_day = utc.day;
    runtime->gps_utc_hour = utc.hour;
    runtime->gps_utc_minute = utc.minute;
    runtime->gps_utc_second = utc.second;
    runtime->gps_utc_valid = true;
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

static bool runtime_feed_gps_byte(esp_bms_idf_runtime_t *runtime, uint8_t byte)
{
    if (byte == '\r') {
        return false;
    }
    if (byte == '$') {
        runtime->gps_line[0] = byte;
        runtime->gps_line_len = 1;
        return false;
    }
    if (byte == '\n') {
        const bool changed = runtime_apply_gps_line(runtime);
        runtime->gps_line_len = 0;
        return changed;
    }
    if (runtime->gps_line_len >= GPS_RMC_MAX_LINE) {
        runtime->gps_line_len = 0;
        runtime->gps_parse_errors++;
        return false;
    }

    runtime->gps_line[runtime->gps_line_len++] = byte;
    return false;
}

static bool runtime_poll_gps_uart(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, GPS_UART_READY)) {
        return false;
    }

    size_t available = 0;
    esp_err_t ret = uart_get_buffered_data_len(runtime->gps_uart, &available);
    if (ret != ESP_OK || available == 0) {
        return false;
    }

    uint8_t bytes[GPS_RMC_MAX_LINE];
    if (available > sizeof(bytes)) {
        available = sizeof(bytes);
    }

    const int read = uart_read_bytes(runtime->gps_uart, bytes, (uint32_t)available, 0);
    if (read <= 0) {
        return false;
    }

    bool changed = false;
    runtime->gps_bytes_seen += (uint32_t)read;
    if (runtime->gps_raw_sample_len < sizeof(runtime->gps_raw_sample)) {
        size_t sample_len = sizeof(runtime->gps_raw_sample) - runtime->gps_raw_sample_len;
        if (sample_len > (size_t)read) {
            sample_len = (size_t)read;
        }
        memcpy(&runtime->gps_raw_sample[runtime->gps_raw_sample_len], bytes, sample_len);
        runtime->gps_raw_sample_len += (uint8_t)sample_len;
    }
    const uint8_t *cursor = bytes;
    const uint8_t *const end = bytes + read;
    while (cursor < end) {
        changed = runtime_feed_gps_byte(runtime, *cursor++) || changed;
    }
    return changed;
}

static bool runtime_update_gps_diagnostics(esp_bms_idf_runtime_t *runtime)
{
    bool changed = false;
    const uint32_t now = runtime->tick_count;
    const uint32_t pps_count = runtime->gps_pps_isr_count;

    if (pps_count != runtime->gps_pps_processed_count) {
        const uint32_t edges = pps_count - runtime->gps_pps_processed_count;
        runtime->gps_pps_processed_count = pps_count;
        runtime->gps_pps_last_tick = now;
        if (!runtime->gps_pps_ever_seen) {
            ESP_LOGI(TAG, "[gps] PPS first: count=%lu uptime_s=%lu",
                     (unsigned long)pps_count, (unsigned long)now);
            runtime->gps_pps_ever_seen = true;
            runtime->gps_pps_last_summary_tick = now;
        } else if (!runtime->gps_pps_active) {
            ESP_LOGI(TAG, "[gps] PPS recovered: count=%lu uptime_s=%lu",
                     (unsigned long)pps_count, (unsigned long)now);
        } else if (edges > 1U) {
            ESP_LOGW(TAG, "[gps] PPS backlog: edges=%lu count=%lu uptime_s=%lu",
                     (unsigned long)edges,
                     (unsigned long)pps_count,
                     (unsigned long)now);
        }
        runtime->gps_pps_active = true;
    }

    if (runtime->gps_pps_active &&
        now - runtime->gps_pps_last_tick >= GPS_PPS_TIMEOUT_SECONDS) {
        runtime->gps_pps_active = false;
        ESP_LOGW(TAG, "[gps] PPS lost: last_count=%lu uptime_s=%lu",
                 (unsigned long)runtime->gps_pps_processed_count,
                 (unsigned long)now);
    }

    if (runtime->gps_pps_active &&
        now - runtime->gps_pps_last_summary_tick >= GPS_DIAGNOSTIC_LOG_PERIOD_SECONDS) {
        runtime->gps_pps_last_summary_tick = now;
        ESP_LOGI(TAG, "[gps] PPS stable: count=%lu uptime_s=%lu",
                 (unsigned long)runtime->gps_pps_processed_count,
                 (unsigned long)now);
    }

    if (runtime->gps_rmc_seen && !runtime->gps_rmc_timed_out &&
        now - runtime->gps_rmc_last_tick >= GPS_RMC_TIMEOUT_SECONDS) {
        runtime->gps_rmc_timed_out = true;
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, false);
        runtime->gps_speed_knots_milli = 0U;
        esp_bms_trip_efficiency_sample(&runtime->trip_efficiency,
                                       esp_timer_get_time(),
                                       false,
                                       0U,
                                       false,
                                       0U,
                                       0);
        runtime_update_snapshot_speed(runtime);
        ESP_LOGW(TAG, "[gps] RMC timeout: uptime_s=%lu", (unsigned long)now);
        changed = true;
    }

    if (runtime->gps_utc_valid &&
        (!runtime->gps_utc_logged ||
         now - runtime->gps_rmc_last_log_tick >= GPS_DIAGNOSTIC_LOG_PERIOD_SECONDS)) {
        runtime->gps_utc_logged = true;
        runtime->gps_rmc_last_log_tick = now;
        ESP_LOGI(TAG,
                 "[gps] UTC=%04u-%02u-%02uT%02u:%02u:%02uZ fix=%d pps=%d uptime_s=%lu",
                 runtime->gps_utc_year,
                 runtime->gps_utc_month,
                 runtime->gps_utc_day,
                 runtime->gps_utc_hour,
                 runtime->gps_utc_minute,
                 runtime->gps_utc_second,
                 RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID) ? 1 : 0,
                 runtime->gps_pps_active ? 1 : 0,
                 (unsigned long)now);
    }

    if (!runtime->gps_uart_diagnostic_logged &&
        now >= GPS_UART_STARTUP_DIAGNOSTIC_SECONDS) {
        runtime->gps_uart_diagnostic_logged = true;
        if (runtime->gps_bytes_seen == 0U) {
            ESP_LOGW(TAG,
                     "[gps] no NMEA bytes: uart=%d rx=%d level=%d uptime_s=%lu",
                     runtime->gps_uart,
                     GPS_UART_RX_GPIO,
                     gpio_get_level(GPS_UART_RX_GPIO),
                     (unsigned long)now);
        } else if (runtime->snapshot.gps_sentences_seen == 0U) {
            ESP_LOGW(TAG,
                     "[gps] NMEA received without valid RMC: bytes=%lu parse_errors=%lu uptime_s=%lu",
                     (unsigned long)runtime->gps_bytes_seen,
                     (unsigned long)runtime->gps_parse_errors,
                     (unsigned long)now);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,
                                     runtime->gps_raw_sample,
                                     runtime->gps_raw_sample_len,
                                     ESP_LOG_WARN);
        }
    }

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
    runtime_init_gps_pps(runtime);
    runtime_init_gps_uart(runtime);
}

esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        return ESP_OK;
    }
    runtime_ensure_setup_ap_credentials(runtime);
    ESP_LOGI(TAG, "[wifi] starting setup AP: ssid='%s' ap_pw_len=%u",
             runtime->setup_ap_ssid,
             (unsigned)strlen(runtime->setup_ap_password));

    esp_err_t ret = runtime_init_wifi_stack(runtime);
    if (ret != ESP_OK) {
        runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
        runtime_set_error(runtime, "AP FAIL");
        return ret;
    }

    const bool wifi_was_started = runtime_wifi_started(runtime);
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret == ESP_OK) {
        ret = runtime_apply_setup_ap_wifi_config(runtime);
    }
    if (ret == ESP_OK && !wifi_was_started) {
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
        runtime_set_error(runtime, "AP FAIL");
        ESP_LOGE(TAG, "[wifi] AP start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, true);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, true);
    runtime->snapshot.wifi = ESP_BMS_WIFI_SETUP_AP;
    runtime_set_error(runtime, "AP READY");
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime_ensure_setup_ap_credentials(runtime);

    const esp_err_t ret = runtime_start_http_server(runtime);
    if (ret == ESP_OK) {
        runtime_set_error(runtime, "HTTP ON");
    }
    return ret;
}

esp_err_t esp_bms_idf_runtime_stop_setup_services(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    runtime_cast_stop(runtime, "setup AP stopped");
    if (runtime->http_server) {
        const esp_err_t http_ret = httpd_stop(runtime->http_server);
        if (http_ret != ESP_OK) {
            ESP_LOGW(TAG, "[http] server stop failed: %s", esp_err_to_name(http_ret));
            result = http_ret;
        }
        runtime->http_server = NULL;
    }
    RUNTIME_SET_FLAG(runtime, HTTP_SERVER_STARTED, false);

    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        const esp_err_t wifi_ret = esp_wifi_stop();
        if (wifi_ret != ESP_OK) {
            ESP_LOGW(TAG, "[wifi] AP stop failed: %s", esp_err_to_name(wifi_ret));
            if (result == ESP_OK) {
                result = wifi_ret;
            }
        }
    }
    RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, false);
    runtime->setup_ap_clients = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
    runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
    runtime_set_error(runtime, "AP OFF");
    runtime_log_heap_state("setup_services_stopped");
    return result;
}

esp_err_t esp_bms_idf_runtime_start_bms_ble_if_bound(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t load_ret = runtime_load_bms_binding(runtime);
    if (load_ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[bms] no bound MAC; NimBLE stays off");
        return ESP_OK;
    }
    if (load_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[bms] bound MAC in NVS is invalid; NimBLE stays off");
        return ESP_OK;
    }
    if (load_ret != ESP_OK) {
        return load_ret;
    }

    ESP_LOGI(TAG, "[bms] bound MAC loaded: mac=%s", runtime->bms_bound_mac);
    ESP_RETURN_ON_ERROR(runtime_init_bms_ble(runtime), TAG, "BMS BLE init failed");
    return runtime_bms_ble_start_scan_or_defer(runtime);
}

esp_err_t esp_bms_idf_runtime_start_bms_ble_for_bind(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(runtime_init_bms_ble(runtime), TAG, "BMS BLE init failed");
    return runtime_bms_ble_start_scan_or_defer(runtime);
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

    esp_err_t ret = runtime_init_bms_ble(runtime);
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
    if (!runtime->controller_connection_enabled ||
        runtime->controller_bound_mac[0] == '\0' ||
        runtime->controller_conn_handle != 0xFFFFU) {
        runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    return runtime_controller_start_scan(runtime);
}

static esp_err_t runtime_bluetooth_stop_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
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

bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }

    const bool cast_active = __atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED);
    if (cast_active) {
        runtime->cast_heartbeat_elapsed_ms += elapsed_ms;
        if (runtime->cast_heartbeat_elapsed_ms >= CAST_HEARTBEAT_TIMEOUT_MS) {
            runtime_cast_stop(runtime, "heartbeat timeout");
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
    if (RUNTIME_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, false);
        (void)runtime_bms_scan_project_snapshot(runtime);
        changed = true;
    }
    changed = runtime_apply_pending_http_ap_password(runtime) || changed;
    changed = runtime_apply_pending_http_bms_scan(runtime) || changed;
    changed = runtime_apply_pending_http_bms_bind(runtime) || changed;
    if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
        !RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = esp_bms_idf_runtime_start_bluetooth_advertising(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] local Bluetooth advertising start failed: %s",
                     esp_err_to_name(ret));
        }
        changed = true;
    }
    const bool bms_collection_active =
        runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_BMS ||
        runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD ||
        runtime->trip_efficiency.started;
    if (bms_collection_active &&
        runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        runtime->bms_status_poll_elapsed_ms += elapsed_ms;
        if (runtime->bms_status_poll_elapsed_ms >= BMS_STATUS_POLL_PERIOD_MS &&
            !RUNTIME_FLAG(runtime, BMS_WRITE_IN_FLIGHT)) {
            const esp_err_t poll_ret = runtime_bms_ble_send_poll_request(runtime, true);
            if (poll_ret != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS POLL");
                ESP_LOGW(TAG, "[bms] poll request failed: %s", esp_err_to_name(poll_ret));
                changed = true;
            }
        }
    }
    runtime->battery_sample_elapsed_ms += elapsed_ms;
    if (runtime->battery_sample_elapsed_ms >= BATTERY_SAMPLE_PERIOD_MS) {
        runtime->battery_sample_elapsed_ms = 0;
        changed = runtime_sample_battery(runtime) || changed;
    }
    changed = runtime_poll_gps_uart(runtime) || changed;
    if ((runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER ||
         runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD) &&
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED)) {
        runtime->controller_keepalive_elapsed_ms += elapsed_ms;
        if (runtime->controller_keepalive_elapsed_ms >= 2000U) {
            runtime_controller_send_gather(runtime);
        }
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
    changed = runtime_update_gps_diagnostics(runtime) || changed;
    return changed;
}

uint8_t esp_bms_idf_runtime_take_connection_audio_events(esp_bms_idf_runtime_t *runtime)
{
    return runtime ? __atomic_exchange_n(&runtime->pending_audio_events, 0U, __ATOMIC_RELAXED) : 0U;
}

bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event)
{
    if (!runtime || !event || event->action == ESP_BMS_LVGL_ACTION_NONE) {
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
            if (runtime->controller_conn_handle != 0xFFFFU) {
                (void)ble_gap_terminate(runtime->controller_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else if (runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        runtime->snapshot.speed_source =
            runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_GPS
                ? ESP_BMS_SPEED_SOURCE_CONTROLLER
                : ESP_BMS_SPEED_SOURCE_GPS;
        runtime->controller_page_enabled =
            runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER;
        if (runtime->controller_page_enabled && runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
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
            (void)runtime_controller_start_scan(runtime);
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
        if (runtime->controller_conn_handle != 0xFFFFU) {
            (void)ble_gap_terminate(runtime->controller_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
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
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        return "start-bms-bind";
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
