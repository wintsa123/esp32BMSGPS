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
    ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING = 17,
    ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION = 18,
    ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION = 19,
    ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE = 20,
    ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION = 21,
} esp_bms_lvgl_action_t;

#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_COMMITTED (UINT8_C(1) << 0)
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_BRIGHTNESS_PERCENT_VALID (UINT8_C(1) << 1)
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_VOLUME_PERCENT_VALID (UINT8_C(1) << 2)
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_VOLUME_FEEDBACK_VALID (UINT8_C(1) << 3)
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_BMS_MAC_VALID (UINT8_C(1) << 4)
#define ESP_BMS_LVGL_ROTATE_SAVE_DELAY_MS 2000U

typedef struct {
    esp_bms_lvgl_action_t action;
    uint8_t flags;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t volume_feedback_percent;
    char bms_mac[18];
    uint16_t touch_observed_x;
    uint16_t touch_observed_y;
    uint16_t touch_target_x;
    uint16_t touch_target_y;
    uint8_t touch_target_index;
} esp_bms_lvgl_action_event_t;

static inline bool esp_bms_lvgl_action_event_flag_get(const esp_bms_lvgl_action_event_t *event,
                                                       uint8_t flag)
{
    return event && (event->flags & flag) != 0U;
}

static inline void esp_bms_lvgl_action_event_flag_set(esp_bms_lvgl_action_event_t *event,
                                                       uint8_t flag,
                                                       bool enabled)
{
    if (!event) {
        return;
    }
    if (enabled) {
        event->flags |= flag;
    } else {
        event->flags &= (uint8_t)~flag;
    }
}

typedef enum {
    ESP_BMS_SPEED_UNIT_KMH = 0,
    ESP_BMS_SPEED_UNIT_MPH = 1,
} esp_bms_speed_unit_t;

typedef enum {
    ESP_BMS_WIFI_SETUP_AP = 0,
    ESP_BMS_WIFI_OFFLINE = 1,
} esp_bms_wifi_state_t;

#define ESP_BMS_BMS_CODE_MAX_COUNT 6U
#define ESP_BMS_BMS_CODE_TEXT_LEN 8U
#define ESP_BMS_BMS_TEMP_MAX_COUNT 6U
#define ESP_BMS_BMS_SCAN_MAX_CANDIDATES 6U
#define ESP_BMS_BMS_SCAN_NAME_LEN 24U

#define ESP_BMS_DASHBOARD_FLAG_SPEED_VALID (UINT32_C(1) << 0)
#define ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID (UINT32_C(1) << 1)
#define ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE (UINT32_C(1) << 2)
#define ESP_BMS_DASHBOARD_FLAG_PACK_VOLTAGE_VALID (UINT32_C(1) << 3)
#define ESP_BMS_DASHBOARD_FLAG_CURRENT_VALID (UINT32_C(1) << 4)
#define ESP_BMS_DASHBOARD_FLAG_SOC_VALID (UINT32_C(1) << 5)
#define ESP_BMS_DASHBOARD_FLAG_MIN_CELL_VALID (UINT32_C(1) << 6)
#define ESP_BMS_DASHBOARD_FLAG_AVERAGE_CELL_VALID (UINT32_C(1) << 7)
#define ESP_BMS_DASHBOARD_FLAG_MAX_CELL_VALID (UINT32_C(1) << 8)
#define ESP_BMS_DASHBOARD_FLAG_DELTA_CELL_VALID (UINT32_C(1) << 9)
#define ESP_BMS_DASHBOARD_FLAG_TOTAL_CAPACITY_VALID (UINT32_C(1) << 10)
#define ESP_BMS_DASHBOARD_FLAG_CAPACITY_REMAINING_VALID (UINT32_C(1) << 11)
#define ESP_BMS_DASHBOARD_FLAG_LOCAL_BATTERY_VALID (UINT32_C(1) << 12)
#define ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED (UINT32_C(1) << 13)
#define ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ADVERTISING (UINT32_C(1) << 14)
#define ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_CONNECTED (UINT32_C(1) << 15)
#define ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED (UINT32_C(1) << 16)
#define ESP_BMS_DASHBOARD_FLAG_BMS_TEMPERATURE_VALID_SHIFT 17U

typedef struct {
    char mac[18];
    char name[ESP_BMS_BMS_SCAN_NAME_LEN + 1U];
    int8_t rssi;
    bool has_name;
} esp_bms_bms_scan_candidate_t;

typedef struct {
    uint32_t flags;
    uint32_t gps_sentences_seen;
    uint32_t pack_voltage_mv;
    uint32_t total_capacity_mah;
    uint32_t capacity_remaining_mah;
    uint32_t local_battery_mv;
    esp_bms_speed_unit_t speed_unit;
    esp_bms_wifi_state_t wifi;
    uint16_t speed_deci_units;
    int16_t current_deci_amps;
    uint16_t soc_percent;
    uint16_t min_cell_voltage_mv;
    uint16_t average_cell_voltage_mv;
    uint16_t max_cell_voltage_mv;
    uint16_t delta_cell_voltage_mv;
    int16_t bms_temperature_celsius[ESP_BMS_BMS_TEMP_MAX_COUNT];
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t bms_type;
    uint8_t bms_protection_count;
    char bms_protection_codes[ESP_BMS_BMS_CODE_MAX_COUNT][ESP_BMS_BMS_CODE_TEXT_LEN];
    uint8_t bms_warning_count;
    char bms_warning_codes[ESP_BMS_BMS_CODE_MAX_COUNT][ESP_BMS_BMS_CODE_TEXT_LEN];
    char bluetooth_name[32];
    char bms_info_text[16];
    char bms_error_text[32];
    char setup_ap_ssid[32];
    char setup_ap_password[9];
    char setup_ap_qr_payload[96];
    uint8_t bms_scan_candidate_count;
    esp_bms_bms_scan_candidate_t bms_scan_candidates[ESP_BMS_BMS_SCAN_MAX_CANDIDATES];
    char bms_bound_name[ESP_BMS_BMS_SCAN_NAME_LEN + 1U];
} esp_bms_dashboard_snapshot_t;

static inline bool esp_bms_dashboard_snapshot_flag_get(const esp_bms_dashboard_snapshot_t *snapshot,
                                                        uint32_t flag)
{
    return snapshot && (snapshot->flags & flag) != 0U;
}

static inline void esp_bms_dashboard_snapshot_flag_set(esp_bms_dashboard_snapshot_t *snapshot,
                                                        uint32_t flag,
                                                        bool enabled)
{
    if (!snapshot) {
        return;
    }
    if (enabled) {
        snapshot->flags |= flag;
    } else {
        snapshot->flags &= ~flag;
    }
}

static inline uint32_t esp_bms_dashboard_snapshot_temperature_flag(uint8_t index)
{
    return (uint32_t)(UINT32_C(1) << (ESP_BMS_DASHBOARD_FLAG_BMS_TEMPERATURE_VALID_SHIFT + index));
}

static inline bool esp_bms_dashboard_snapshot_temperature_valid(const esp_bms_dashboard_snapshot_t *snapshot,
                                                                uint8_t index)
{
    return index < ESP_BMS_BMS_TEMP_MAX_COUNT &&
           esp_bms_dashboard_snapshot_flag_get(snapshot,
                                               esp_bms_dashboard_snapshot_temperature_flag(index));
}

static inline void esp_bms_dashboard_snapshot_temperature_valid_set(esp_bms_dashboard_snapshot_t *snapshot,
                                                                    uint8_t index,
                                                                    bool enabled)
{
    if (index < ESP_BMS_BMS_TEMP_MAX_COUNT) {
        esp_bms_dashboard_snapshot_flag_set(snapshot,
                                            esp_bms_dashboard_snapshot_temperature_flag(index),
                                            enabled);
    }
}

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display);
esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot);
esp_err_t esp_bms_lvgl_ui_show_dashboard(void);
esp_err_t esp_bms_lvgl_ui_touch_calibration_result(bool success);
esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated);
esp_err_t esp_bms_lvgl_ui_take_action_event(esp_bms_lvgl_action_event_t *event);
esp_err_t esp_bms_lvgl_ui_take_action(esp_bms_lvgl_action_t *action);

#ifdef __cplusplus
}
#endif
