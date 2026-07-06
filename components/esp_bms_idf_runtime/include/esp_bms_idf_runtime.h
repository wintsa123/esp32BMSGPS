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

#define ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES 6U
#define ESP_BMS_IDF_BMS_SCAN_NAME_LEN 24U
#define ESP_BMS_IDF_BMS_FRAME_MAX_LEN 192U

typedef struct {
    char mac[18];
    char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
    int8_t rssi;
    bool has_name;
} esp_bms_idf_bms_scan_candidate_t;

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
    uint16_t bms_service_start_handle;
    uint16_t bms_service_end_handle;
    uint16_t bms_char_val_handle;
    uint16_t bms_cccd_handle;
    uint8_t brightness_percent;
    uint8_t bms_own_addr_type;
    uint8_t bms_ble_phase;
    uint8_t bms_frame[ESP_BMS_IDF_BMS_FRAME_MAX_LEN];
    char setup_ap_ssid[32];
    char setup_ap_password[9];
    char external_ssid[33];
    char external_password[65];
    char bms_bound_mac[18];
    esp_netif_t *setup_ap_netif;
    esp_netif_t *station_netif;
    httpd_handle_t http_server;
    SemaphoreHandle_t http_pending_lock;
    SemaphoreHandle_t bms_scan_lock;
    esp_bms_idf_bms_scan_candidate_t bms_scan_candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
    uint8_t setup_ap_clients;
    uint8_t station_retry_count;
    uint8_t bms_scan_candidate_count;
    uint8_t http_pending_brightness_percent;
    char http_pending_setup_ap_password[9];
    char http_pending_external_ssid[33];
    char http_pending_external_password[65];
    char http_pending_bms_bound_mac[18];
    bool battery_adc_ready;
    bool gps_uart_ready;
    bool nvs_ready;
    bool wifi_stack_ready;
    bool wifi_driver_ready;
    bool wifi_handlers_registered;
    bool setup_ap_started;
    bool station_started;
    bool station_connected;
    bool station_has_ip;
    bool station_connect_requested;
    bool http_config_pending;
    bool http_setup_ap_password_pending;
    bool http_external_wifi_pending;
    bool http_bms_scan_pending;
    bool http_bms_bind_pending;
    bool http_server_started;
    bool bms_ble_ready;
    bool bms_ble_synced;
    bool bms_ble_host_started;
    bool bms_scan_requested;
    bool bms_scan_active;
    bool bms_write_in_flight;
    bool bms_device_info_requested;
    bool bms_device_info_known;
    bool http_pending_language_zh;
    esp_bms_idf_display_rotation_t display_rotation;
    esp_bms_idf_display_rotation_t http_pending_display_rotation;
    esp_bms_speed_unit_t http_pending_speed_unit;
    bool language_zh;
    bool bms_bind_active;
} esp_bms_idf_runtime_t;

void esp_bms_idf_runtime_init(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_load_display_settings(esp_bms_idf_runtime_t *runtime, bool *loaded);
esp_err_t esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime);
bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
bool esp_bms_idf_runtime_apply_action(esp_bms_idf_runtime_t *runtime, esp_bms_lvgl_action_t action);
const char *esp_bms_idf_runtime_action_name(esp_bms_lvgl_action_t action);

#ifdef __cplusplus
}
#endif
