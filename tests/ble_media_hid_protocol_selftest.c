#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bms_ble_media_hid.h"

int main(void)
{
    uint8_t report[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN] = { UINT8_C(0xFF), UINT8_C(0xFF) };
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK, report));
    assert(report[0] == UINT8_C(0xB5) && report[1] == UINT8_C(0x00));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK, report));
    assert(report[0] == UINT8_C(0xB6) && report[1] == UINT8_C(0x00));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE, report));
    assert(report[0] == UINT8_C(0xCD) && report[1] == UINT8_C(0x00));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT, report));
    assert(report[0] == UINT8_C(0xEA) && report[1] == UINT8_C(0x00));
    assert(esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT, report));
    assert(report[0] == UINT8_C(0xE9) && report[1] == UINT8_C(0x00));
    assert(!esp_bms_ble_media_hid_report_from_usage((esp_bms_ble_media_hid_usage_t)0U, report));
    assert(report[0] == UINT8_C(0x00) && report[1] == UINT8_C(0x00));
    assert(!esp_bms_ble_media_hid_report_from_usage(ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK, NULL));
    assert(ESP_BMS_BLE_MEDIA_HID_REPORT_LEN == 2U);
    return 0;
}
