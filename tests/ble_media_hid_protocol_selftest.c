#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bms_ble_media_hid.h"

int main(void)
{
    uint8_t report = UINT8_C(0xFF);
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK, &report));
    assert(report == (UINT8_C(1) << 0));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK, &report));
    assert(report == (UINT8_C(1) << 1));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE, &report));
    assert(report == (UINT8_C(1) << 2));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT, &report));
    assert(report == (UINT8_C(1) << 3));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT, &report));
    assert(report == (UINT8_C(1) << 4));
    assert(!esp_bms_ble_media_hid_report_from_usage((esp_bms_ble_media_hid_usage_t)0U, &report));
    assert(!esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK, NULL));
    assert(ESP_BMS_BLE_MEDIA_HID_REPORT_RELEASE == 0U);
    return 0;
}
