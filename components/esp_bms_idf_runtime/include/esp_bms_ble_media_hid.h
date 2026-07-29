#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ESP_BMS_BLE_MEDIA_HID_REPORT_ID 1U
#define ESP_BMS_BLE_MEDIA_HID_REPORT_RELEASE 0U

typedef enum {
    ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK = 0x00B5U,
    ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK = 0x00B6U,
    ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE = 0x00CDU,
    ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT = 0x00EAU,
    ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT = 0x00E9U,
} esp_bms_ble_media_hid_usage_t;

static inline bool esp_bms_ble_media_hid_report_from_usage(
    esp_bms_ble_media_hid_usage_t usage,
    uint8_t *report)
{
    if (!report) {
        return false;
    }

    switch (usage) {
    case ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK:
        *report = UINT8_C(1) << 0;
        return true;
    case ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK:
        *report = UINT8_C(1) << 1;
        return true;
    case ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE:
        *report = UINT8_C(1) << 2;
        return true;
    case ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT:
        *report = UINT8_C(1) << 3;
        return true;
    case ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT:
        *report = UINT8_C(1) << 4;
        return true;
    default:
        return false;
    }
}
