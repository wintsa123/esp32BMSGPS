#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_bms_lvgl_ui.h"
#include "esp_netif_types.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
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
#define ESP_BMS_IDF_RUNTIME_FLAG_GPS_UART_READY (UINT64_C(1) << 1)
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
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_READY (UINT64_C(1) << 17)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_SYNCED (UINT64_C(1) << 18)
#define ESP_BMS_IDF_RUNTIME_FLAG_BMS_BLE_HOST_STARTED (UINT64_C(1) << 19)
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

typedef esp_bms_bms_scan_candidate_t esp_bms_idf_bms_scan_candidate_t;

typedef struct {
    esp_bms_dashboard_snapshot_t snapshot;
    adc_oneshot_unit_handle_t battery_adc;
    adc_channel_t battery_adc_channel;
    uart_port_t gps_uart;
    uint8_t gps_line[96];
    uint32_t tick_count;
    uint32_t elapsed_ms;
    uint32_t battery_sample_elapsed_ms;
    uint32_t battery_samples_seen;
    uint32_t battery_read_failures;
    uint32_t gps_bytes_seen;
    uint32_t gps_parse_errors;
    uint32_t gps_speed_knots_milli;
    uint32_t bms_status_poll_elapsed_ms;
    uint16_t gps_line_len;
    uint16_t bms_frame_len;
    uint16_t bms_conn_handle;
    uint16_t bluetooth_conn_handle;
    uint16_t bms_service_start_handle;
    uint16_t bms_service_end_handle;
    uint16_t bms_char_val_handle;
    uint16_t bms_cccd_handle;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t bms_type;
    uint8_t bms_own_addr_type;
    uint8_t bluetooth_own_addr_type;
    uint8_t bms_ble_phase;
    uint8_t bms_frame[ESP_BMS_IDF_BMS_FRAME_MAX_LEN];
    char setup_ap_ssid[32];
    char setup_ap_password[9];
    char bms_bound_mac[18];
    char bluetooth_name[32];
    esp_netif_t *setup_ap_netif;
    httpd_handle_t http_server;
    SemaphoreHandle_t http_pending_lock;
    SemaphoreHandle_t bms_scan_lock;
    esp_bms_idf_bms_scan_candidate_t bms_scan_candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
    uint8_t setup_ap_clients;
    uint8_t bms_scan_candidate_count;
    uint8_t http_pending_brightness_percent;
    uint8_t http_pending_volume_percent;
    uint8_t http_pending_bms_type;
    char http_pending_setup_ap_password[9];
    char http_pending_bms_bound_mac[18];
    uint64_t flags;
    esp_bms_idf_display_rotation_t display_rotation;
    esp_bms_idf_display_rotation_t http_pending_display_rotation;
    esp_bms_speed_unit_t http_pending_speed_unit;
} esp_bms_idf_runtime_t;

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
esp_err_t esp_bms_idf_runtime_start_bms_ble_if_bound(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_bms_ble_for_bind(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_bluetooth_advertising(esp_bms_idf_runtime_t *runtime);
bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event);
bool esp_bms_idf_runtime_apply_action(esp_bms_idf_runtime_t *runtime, esp_bms_lvgl_action_t action);
const char *esp_bms_idf_runtime_action_name(esp_bms_lvgl_action_t action);

#ifdef __cplusplus
}
#endif
