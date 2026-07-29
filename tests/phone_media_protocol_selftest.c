#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "esp_bms_phone_media_protocol.h"

int main(void)
{
    const uint8_t valid[] = { 1U, ESP_BMS_PHONE_MEDIA_STATE_READY | ESP_BMS_PHONE_MEDIA_STATE_ACTIVE,
                              'T', 'e', 's', 't', ' ', 0xE4U, 0xB8U, 0xADU };
    esp_bms_phone_media_state_t state = { 0 };
    assert(esp_bms_phone_media_state_decode(valid, sizeof(valid), &state));
    assert(state.flags == (ESP_BMS_PHONE_MEDIA_STATE_READY | ESP_BMS_PHONE_MEDIA_STATE_ACTIVE));
    assert(strcmp(state.title, "Test \xE4\xB8\xAD") == 0);

    const uint8_t bad_version[] = { 2U, 0U };
    const uint8_t malformed_utf8[] = { 1U, 0U, 0xE4U, 0xB8U };
    assert(!esp_bms_phone_media_state_decode(bad_version, sizeof(bad_version), &state));
    assert(!esp_bms_phone_media_state_decode(malformed_utf8, sizeof(malformed_utf8), &state));
    return 0;
}
