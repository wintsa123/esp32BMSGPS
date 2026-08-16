#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_bms_lvgl_bridge.h"
#include "esp_bms_lvgl_ui.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_DISPLAY_SERVICE_STATUS_TEXT_MAX 32U

typedef enum {
    ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_UPDATE = 0,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_FINISH,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_BRIGHTNESS,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_ROTATION,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_SHOW_DASHBOARD,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_PAGE,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_TOUCH_CALIBRATION_RESULT,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_RESET_TOUCH_CALIBRATION,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_PRESENT_JPEG,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_UPDATE,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_FINISH,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_ENTER_CAST,
    ESP_BMS_DISPLAY_SERVICE_COMMAND_EXIT_CAST,
} esp_bms_display_service_command_kind_t;

typedef struct {
    esp_bms_display_service_command_kind_t kind;
    union {
        struct {
            uint8_t progress_percent;
            char status_text[ESP_BMS_DISPLAY_SERVICE_STATUS_TEXT_MAX];
        } boot_update;
        struct {
            const esp_bms_dashboard_snapshot_t *snapshot;
        } boot_finish;
        struct {
            uint8_t percent;
        } brightness;
        struct {
            esp_bms_display_rotation_t rotation;
        } rotation;
        struct {
            esp_bms_lvgl_page_t page;
            bool animated;
        } page;
        struct {
            bool success;
        } touch_calibration_result;
        struct {
            uint32_t sequence;
            esp_bms_display_rotation_t rotation;
            const uint8_t *bytes;
            size_t byte_count;
            esp_bms_lvgl_bridge_cast_metrics_t *metrics;
        } jpeg;
        struct {
            uint8_t progress_percent;
            bool failed;
            char status_text[ESP_BMS_DISPLAY_SERVICE_STATUS_TEXT_MAX];
        } ota_update;
    } data;
} esp_bms_display_service_command_t;

esp_err_t esp_bms_display_service_start(const esp_bms_lvgl_bridge_config_t *config,
                                        uint8_t brightness_percent,
                                        const esp_bms_dashboard_snapshot_t *snapshot);
esp_err_t esp_bms_display_service_publish_snapshot(const esp_bms_dashboard_snapshot_t *snapshot);
esp_err_t esp_bms_display_service_submit_command(
    const esp_bms_display_service_command_t *command,
    uint32_t timeout_ms);
esp_err_t esp_bms_display_service_take_action_event(esp_bms_lvgl_action_event_t *event);
esp_bms_lvgl_data_source_t esp_bms_display_service_stable_data_source(void);
bool esp_bms_display_service_speed_dashboard_style_available(esp_bms_speed_dashboard_style_t style);
esp_bms_speed_dashboard_style_t esp_bms_display_service_default_speed_dashboard_style(void);
esp_err_t esp_bms_display_service_get_logical_resolution(
    esp_bms_display_rotation_t rotation,
    uint16_t *width,
    uint16_t *height);

#ifdef __cplusplus
}
#endif
