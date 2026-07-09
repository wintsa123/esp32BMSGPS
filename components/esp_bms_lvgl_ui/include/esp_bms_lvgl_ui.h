#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_BMS_LVGL_PAGE_BATTERY = 0,
    ESP_BMS_LVGL_PAGE_GPS = 1,
} esp_bms_lvgl_page_t;

typedef enum {
    ESP_BMS_LVGL_ACTION_NONE = 0,
    ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD = 1,
    ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU = 2,
    ESP_BMS_LVGL_ACTION_SHOW_SETTINGS = 3,
    ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING = 4,
    ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS = 5,
    ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY = 6,
    ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT = 7,
    ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE = 8,
    ESP_BMS_LVGL_ACTION_START_BMS_BIND = 9,
    ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS = 10,
    ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS = 11,
    ESP_BMS_LVGL_ACTION_SET_VOLUME = 12,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT = 13,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JK = 14,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD = 15,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY = 16,
    ESP_BMS_LVGL_ACTION_SCAN_WIFI = 17,
    ESP_BMS_LVGL_ACTION_CONNECT_WIFI = 18,
    ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING = 19,
} esp_bms_lvgl_action_t;

typedef struct {
    esp_bms_lvgl_action_t action;
    bool committed;
    bool brightness_percent_valid;
    uint8_t brightness_percent;
    bool volume_percent_valid;
    uint8_t volume_percent;
    bool volume_feedback_valid;
    uint8_t volume_feedback_percent;
    bool wifi_ssid_valid;
    char wifi_ssid[33];
    bool wifi_password_valid;
    char wifi_password[65];
} esp_bms_lvgl_action_event_t;

typedef enum {
    ESP_BMS_SPEED_UNIT_KMH = 0,
    ESP_BMS_SPEED_UNIT_MPH = 1,
} esp_bms_speed_unit_t;

typedef enum {
    ESP_BMS_WIFI_SETUP_AP = 0,
    ESP_BMS_WIFI_CONNECTING = 1,
    ESP_BMS_WIFI_CONNECTED = 2,
    ESP_BMS_WIFI_OFFLINE = 3,
} esp_bms_wifi_state_t;

typedef enum {
    ESP_BMS_OTA_IDLE = 0,
    ESP_BMS_OTA_CHECKING = 1,
    ESP_BMS_OTA_AVAILABLE = 2,
    ESP_BMS_OTA_DOWNLOADING = 3,
    ESP_BMS_OTA_VERIFYING = 4,
    ESP_BMS_OTA_READY = 5,
    ESP_BMS_OTA_FAILED = 6,
} esp_bms_ota_state_t;

#define ESP_BMS_BMS_CODE_MAX_COUNT 6U
#define ESP_BMS_BMS_CODE_TEXT_LEN 8U
#define ESP_BMS_BMS_TEMP_MAX_COUNT 6U
#define ESP_BMS_WIFI_SCAN_MAX_CANDIDATES 6U
#define ESP_BMS_WIFI_SCAN_SSID_LEN 32U

typedef struct {
    char ssid[ESP_BMS_WIFI_SCAN_SSID_LEN + 1U];
    int8_t rssi;
    uint8_t auth_mode;
    bool open;
} esp_bms_wifi_scan_candidate_t;

typedef struct {
    bool speed_valid;
    uint16_t speed_deci_units;
    esp_bms_speed_unit_t speed_unit;
    bool gps_fix_valid;
    uint32_t gps_sentences_seen;

    bool bms_online;
    bool pack_voltage_valid;
    uint32_t pack_voltage_mv;
    bool current_valid;
    int16_t current_deci_amps;
    bool soc_valid;
    uint16_t soc_percent;

    bool min_cell_valid;
    uint16_t min_cell_voltage_mv;
    bool average_cell_valid;
    uint16_t average_cell_voltage_mv;
    bool max_cell_valid;
    uint16_t max_cell_voltage_mv;
    bool delta_cell_valid;
    uint16_t delta_cell_voltage_mv;

    bool total_capacity_valid;
    uint32_t total_capacity_mah;
    bool capacity_remaining_valid;
    uint32_t capacity_remaining_mah;
    bool local_battery_valid;
    uint32_t local_battery_mv;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t bms_type;
    bool bluetooth_enabled;
    bool bluetooth_advertising;
    bool bluetooth_connected;
    char bluetooth_name[32];

    char bms_info_text[16];
    uint8_t bms_protection_count;
    char bms_protection_codes[ESP_BMS_BMS_CODE_MAX_COUNT][ESP_BMS_BMS_CODE_TEXT_LEN];
    uint8_t bms_warning_count;
    char bms_warning_codes[ESP_BMS_BMS_CODE_MAX_COUNT][ESP_BMS_BMS_CODE_TEXT_LEN];
    bool bms_temperature_valid[ESP_BMS_BMS_TEMP_MAX_COUNT];
    int16_t bms_temperature_celsius[ESP_BMS_BMS_TEMP_MAX_COUNT];

    bool setup_ap_enabled;
    esp_bms_wifi_state_t wifi;
    esp_bms_ota_state_t ota;
    char bms_error_text[32];
    char setup_ap_ssid[32];
    char setup_ap_password[9];
    char setup_ap_qr_payload[96];
    bool wifi_scan_active;
    bool wifi_scan_complete;
    uint8_t wifi_scan_count;
    uint32_t wifi_scan_generation;
    esp_bms_wifi_scan_candidate_t wifi_scan_candidates[ESP_BMS_WIFI_SCAN_MAX_CANDIDATES];
} esp_bms_dashboard_snapshot_t;

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display);
esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot);
esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated);
esp_err_t esp_bms_lvgl_ui_take_action_event(esp_bms_lvgl_action_event_t *event);
esp_err_t esp_bms_lvgl_ui_take_action(esp_bms_lvgl_action_t *action);

#ifdef __cplusplus
}
#endif
