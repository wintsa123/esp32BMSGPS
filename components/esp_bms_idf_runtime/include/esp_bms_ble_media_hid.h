#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ESP_BMS_BLE_MEDIA_HID_REPORT_ID 1U
#define ESP_BMS_BLE_MEDIA_HID_REPORT_LEN 2U

typedef enum {
    ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK = 0x00B5U,
    ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK = 0x00B6U,
    ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE = 0x00CDU,
    ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT = 0x00EAU,
    ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT = 0x00E9U,
} esp_bms_ble_media_hid_usage_t;

static inline bool esp_bms_ble_media_hid_report_from_usage(
    esp_bms_ble_media_hid_usage_t usage,
    uint8_t report[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN])
{
    if (!report) {
        return false;
    }

    switch (usage) {
    case ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK:
    case ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK:
    case ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE:
    case ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT:
    case ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT:
        report[0] = (uint8_t)(usage & UINT8_C(0xFF));
        report[1] = (uint8_t)((uint16_t)usage >> 8U);
        return true;
    default:
        report[0] = 0U;
        report[1] = 0U;
        return false;
    }
}
