#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_bms_lvgl_ui.h"
#include "esp_bms_speed_dashboard.h"
#include "esp_fardriver_protocol.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_BMS_FEATURE_GPS
#define ESP_BMS_FEATURE_GPS 1
#endif

#ifndef ESP_BMS_FEATURE_OTA
#define ESP_BMS_FEATURE_OTA 1
#endif

typedef enum {
    ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT = 0,
    ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE = 1,
    ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT = 2,
    ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE = 3,
} esp_bms_idf_display_rotation_t;

typedef enum {
    ESP_BMS_IDF_BMS_TYPE_ANT = 0,
    ESP_BMS_IDF_BMS_TYPE_JK = 1,
    ESP_BMS_IDF_BMS_TYPE_JBD = 2,
    ESP_BMS_IDF_BMS_TYPE_DALY = 3,
} esp_bms_idf_bms_type_t;

#define ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES ESP_BMS_BMS_SCAN_MAX_CANDIDATES
#define ESP_BMS_IDF_BMS_SCAN_NAME_LEN ESP_BMS_BMS_SCAN_NAME_LEN
#define ESP_BMS_IDF_BMS_FRAME_MAX_LEN 192U

#define ESP_BMS_IDF_RUNTIME_FLAG_BATTERY_ADC_READY (UINT64_C(1) << 0)
#define ESP_BMS_IDF_RUNTIME_FLAG_NVS_READY (UINT64_C(1) << 2)
#define ESP_BMS_IDF_RUNTIME_FLAG_WIFI_STACK_READY (UINT64_C(1) << 3)
#define ESP_BMS_IDF_RUNTIME_FLAG_WIFI_DRIVER_READY (UINT64_C(1) << 4)
#define ESP_BMS_IDF_RUNTIME_FLAG_WIFI_HANDLERS_REGISTERED (UINT64_C(1) << 5)
#define ESP_BMS_IDF_RUNTIME_FLAG_SETUP_AP_STARTED (UINT64_C(1) << 6)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_PENDING (UINT64_C(1) << 11)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SETUP_AP_PASSWORD_PENDING (UINT64_C(1) << 12)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_BMS_SCAN_PENDING (UINT64_C(1) << 14)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_BMS_BIND_PENDING (UINT64_C(1) << 15)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SERVER_STARTED (UINT64_C(1) << 16)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_READY (UINT64_C(1) << 17)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_SYNCED (UINT64_C(1) << 18)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_STARTED (UINT64_C(1) << 19)
/* Compatibility aliases stay local to the transitional runtime implementation. */
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_READY ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_READY
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_SYNCED ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_SYNCED
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_HOST_STARTED ESP_BMS_IDF_RUNTIME_FLAG_BLE_HOST_STARTED
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_SCAN_REQUESTED (UINT64_C(1) << 20)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_SCAN_ACTIVE (UINT64_C(1) << 21)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLUETOOTH_ADVERTISE_REQUESTED (UINT64_C(1) << 25)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLUETOOTH_ADVERTISING (UINT64_C(1) << 26)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLUETOOTH_CONNECTED (UINT64_C(1) << 27)
#define ESP_BMS_IDF_RUNTIME_FLAG_BLUETOOTH_SNAPSHOT_DIRTY (UINT64_C(1) << 28)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_WRITE_IN_FLIGHT (UINT64_C(1) << 29)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_DEVICE_INFO_REQUESTED (UINT64_C(1) << 30)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_DEVICE_INFO_KNOWN (UINT64_C(1) << 31)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_PENDING_LANGUAGE_ZH (UINT64_C(1) << 32)
#define ESP_BMS_IDF_RUNTIME_FLAG_LANGUAGE_ZH (UINT64_C(1) << 33)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BIND_ACTIVE (UINT64_C(1) << 34)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_SCAN_SNAPSHOT_DIRTY (UINT64_C(1) << 35)
#define ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_APPLIED (UINT64_C(1) << 36)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_SNAPSHOT_DIRTY (UINT64_C(1) << 37)
#define ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SCAN_REQUESTED (UINT64_C(1) << 38)
#define ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SCAN_ACTIVE (UINT64_C(1) << 39)
#define ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SNAPSHOT_DIRTY (UINT64_C(1) << 40)
#define ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SUBSCRIBED (UINT64_C(1) << 41)
#define ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SETTINGS_SAVE_REQUESTED (UINT64_C(1) << 42)

#define ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED (UINT8_C(1) << 0)
#define ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED (UINT8_C(1) << 1)

typedef esp_bms_bms_scan_candidate_t esp_bms_idf_bms_scan_candidate_t;

typedef struct esp_bms_idf_runtime esp_bms_idf_runtime_t;

typedef esp_err_t (*esp_bms_idf_runtime_optional_http_handler_t)(httpd_req_t *req,
                                                                  void *context);
typedef bool (*esp_bms_idf_runtime_bms_frame_handler_t)(esp_bms_idf_runtime_t *runtime,
                                                         const uint8_t *chunk,
                                                         size_t chunk_len);

/* Optional BMS transport ownership lives in esp_bms_bms_ble.  The core only
 * dispatches lifecycle events through this stable contract. */
typedef struct {
    esp_err_t (*start_if_bound)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*start_for_bind)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*resume_scan)(esp_bms_idf_runtime_t *runtime);
    bool (*stop)(esp_bms_idf_runtime_t *runtime);
    bool (*tick)(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
    void (*on_ble_reset)(esp_bms_idf_runtime_t *runtime);
} esp_bms_idf_runtime_bms_ble_driver_t;

/* Optional controller transport ownership lives in esp_bms_controller_ble.
 * The core retains settings and snapshot projection, then dispatches transport
 * lifecycle through this contract. */
typedef struct {
    esp_err_t (*start_if_enabled)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*start_scan)(esp_bms_idf_runtime_t *runtime);
    void (*stop)(esp_bms_idf_runtime_t *runtime);
    bool (*tick)(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
    void (*on_ble_reset)(esp_bms_idf_runtime_t *runtime);
} esp_bms_idf_runtime_controller_ble_driver_t;

/* The optional network component owns Wi-Fi, HTTPD, and embedded Web assets.
 * The core only retains its visible state and dispatches lifecycle requests. */
typedef struct {
    esp_err_t (*start_setup_ap)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*start_http_server)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*stop_setup_services)(esp_bms_idf_runtime_t *runtime);
    esp_err_t (*refresh_setup_ap_config)(esp_bms_idf_runtime_t *runtime);
} esp_bms_idf_runtime_network_driver_t;

struct esp_bms_idf_runtime {
    esp_bms_dashboard_snapshot_t snapshot;
    adc_oneshot_unit_handle_t battery_adc;
    adc_channel_t battery_adc_channel;
    uint32_t tick_count;
    uint32_t elapsed_ms;
    uint32_t battery_sample_elapsed_ms;
    uint32_t battery_samples_seen;
    uint32_t battery_read_failures;
    uint32_t gps_speed_knots_milli;
    int64_t bms_telemetry_last_us;
    uint32_t bms_status_poll_elapsed_ms;
    uint32_t controller_keepalive_elapsed_ms;
    uint32_t controller_scan_revision;
    uint16_t bms_frame_len;
    uint16_t bms_conn_handle;
    uint16_t bluetooth_conn_handle;
    uint16_t controller_conn_handle;
    uint16_t bms_service_start_handle;
    uint16_t bms_service_end_handle;
    uint16_t bms_char_val_handle;
    uint16_t bms_cccd_handle;
    uint16_t controller_service_start_handle;
    uint16_t controller_service_end_handle;
    uint16_t controller_char_val_handle;
    uint16_t controller_cccd_handle;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t bms_type;
    uint8_t bms_own_addr_type;
    uint8_t bluetooth_own_addr_type;
    uint8_t bms_ble_phase;
    uint8_t controller_ble_phase;
    uint8_t bms_frame[ESP_BMS_IDF_BMS_FRAME_MAX_LEN];
    char setup_ap_ssid[32];
    char setup_ap_password[9];
    char bms_bound_mac[18];
    char bms_bound_name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
    char controller_bound_mac[18];
    char controller_bound_name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
    char bluetooth_name[32];
    SemaphoreHandle_t http_pending_lock;
    SemaphoreHandle_t bms_scan_lock;
    esp_bms_idf_bms_scan_candidate_t bms_scan_candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
    esp_bms_idf_bms_scan_candidate_t controller_scan_candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
    uint8_t setup_ap_clients;
    uint8_t bms_scan_candidate_count;
    uint8_t controller_scan_candidate_count;
    uint8_t pending_audio_events;
    bool cast_active;
    bool cast_frame_active;
    int cast_socket_fd;
    uint32_t cast_sequence;
    uint32_t cast_heartbeat_elapsed_ms;
    bool controller_connection_enabled;
    bool controller_page_enabled;
    uint8_t controller_fallback_tire_rim_inch;
    uint8_t controller_fallback_tire_aspect_percent;
    uint16_t controller_fallback_tire_width_mm;
    uint8_t controller_observed_tire_rim_inch;
    uint8_t controller_observed_tire_aspect_percent;
    uint16_t controller_observed_tire_width_mm;
    uint16_t controller_observed_gear_ratio_centi;
    esp_bms_lvgl_data_source_t active_data_source;
    esp_bms_trip_efficiency_t trip_efficiency;
    esp_fardriver_state_t controller_state;
    uint8_t http_pending_brightness_percent;
    uint8_t http_pending_volume_percent;
    uint8_t http_pending_bms_type;
    char http_pending_setup_ap_password[9];
    char http_pending_bms_bound_mac[18];
    uint64_t flags;
    esp_bms_idf_display_rotation_t display_rotation;
    esp_bms_idf_display_rotation_t http_pending_display_rotation;
    esp_bms_speed_unit_t http_pending_speed_unit;
    esp_bms_speed_source_t http_pending_speed_source;
    esp_bms_idf_runtime_optional_http_handler_t optional_http_handler;
    void *optional_http_context;
    esp_bms_idf_runtime_bms_frame_handler_t bms_frame_handler;
    const esp_bms_idf_runtime_bms_ble_driver_t *bms_ble_driver;
    const esp_bms_idf_runtime_controller_ble_driver_t *controller_ble_driver;
    const esp_bms_idf_runtime_network_driver_t *network_driver;
};

static inline bool esp_bms_idf_runtime_flag_get(const esp_bms_idf_runtime_t *runtime,
                                                uint64_t flag)
{
    return runtime && (runtime->flags & flag) != 0ULL;
}

static inline void esp_bms_idf_runtime_flag_set(esp_bms_idf_runtime_t *runtime,
                                                uint64_t flag,
                                                bool enabled)
{
    if (!runtime) {
        return;
    }
    if (enabled) {
        runtime->flags |= flag;
    } else {
        runtime->flags &= ~flag;
    }
}

void esp_bms_idf_runtime_init(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_load_display_settings(esp_bms_idf_runtime_t *runtime, bool *loaded);
esp_err_t esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_http_server(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_stop_setup_services(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_ensure_ble_host(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_register_bms_frame_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_bms_frame_handler_t handler);
void esp_bms_idf_runtime_register_bms_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_bms_ble_driver_t *driver);
void esp_bms_idf_runtime_register_controller_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_controller_ble_driver_t *driver);
void esp_bms_idf_runtime_register_network_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_network_driver_t *driver);
esp_err_t esp_bms_idf_runtime_http_api_handler(httpd_req_t *req);
esp_err_t esp_bms_idf_runtime_http_cast_ws_handler(httpd_req_t *req);
void esp_bms_idf_runtime_stop_cast(esp_bms_idf_runtime_t *runtime, const char *reason);
esp_err_t esp_bms_idf_runtime_load_bms_binding(esp_bms_idf_runtime_t *runtime);
bool esp_bms_idf_runtime_bms_scan_project_snapshot(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_bms_scan_clear_candidates(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_bms_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
                                                   const char *mac,
                                                   const char *name,
                                                   int8_t rssi);
esp_err_t esp_bms_idf_runtime_start_bluetooth_advertising(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_controller_ble_if_enabled(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_controller_scan(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_stop_controller_ble(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_project_controller_snapshot(esp_bms_idf_runtime_t *runtime);
void esp_bms_idf_runtime_set_active_data_source(esp_bms_idf_runtime_t *runtime,
                                                esp_bms_lvgl_data_source_t source);
bool esp_bms_idf_runtime_apply_pending_http_config(esp_bms_idf_runtime_t *runtime);
bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
void esp_bms_idf_runtime_register_optional_http_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_optional_http_handler_t handler,
    void *context);
bool esp_bms_idf_runtime_set_gps_module_state(esp_bms_idf_runtime_t *runtime,
                                              esp_bms_gps_module_state_t state,
                                              const char *reason);
bool esp_bms_idf_runtime_publish_gps_sample(esp_bms_idf_runtime_t *runtime,
                                            bool fix_valid,
                                            uint32_t speed_knots_milli);
void esp_bms_idf_runtime_publish_gps_datetime(esp_bms_idf_runtime_t *runtime,
                                              uint16_t year,
                                              uint8_t month,
                                              uint8_t day,
                                              uint8_t hour,
                                              uint8_t minute,
                                              bool valid);
bool esp_bms_idf_runtime_timeout_gps(esp_bms_idf_runtime_t *runtime);
uint8_t esp_bms_idf_runtime_take_connection_audio_events(esp_bms_idf_runtime_t *runtime);
bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event);
bool esp_bms_idf_runtime_apply_action(esp_bms_idf_runtime_t *runtime, esp_bms_lvgl_action_t action);
const char *esp_bms_idf_runtime_action_name(esp_bms_lvgl_action_t action);

#ifdef __cplusplus
}
#endif
