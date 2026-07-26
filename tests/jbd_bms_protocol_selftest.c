#include "esp_bms_jbd_protocol.h"

#include <assert.h>
#include <string.h>

static uint16_t checksum(const uint8_t *data, size_t len)
{
    uint16_t value = 0U;
    for (size_t index = 0U; index < len; ++index) {
        value = (uint16_t)(value - data[index]);
    }
    return value;
}

int main(void)
{
    uint8_t request[7] = { 0 };
    assert(esp_bms_jbd_poll_request(0U, request));
    assert(request[0] == 0xDDU && request[1] == 0xA5U && request[2] == 0x03U);

    uint8_t frame[32] = { 0 };
    frame[0] = 0xDDU;
    frame[1] = 0x03U;
    frame[2] = 0x03U;
    frame[3] = 25U;
    frame[4] = 0x14U;
    frame[5] = 0x50U;
    frame[6] = 0xFFU;
    frame[7] = 0x9CU;
    frame[8] = 0x01U;
    frame[9] = 0x2CU;
    frame[10] = 0x01U;
    frame[11] = 0x90U;
    frame[23] = 85U;
    frame[26] = 1U;
    frame[27] = 0x0BU;
    frame[28] = 0xB8U;
    const uint16_t crc = checksum(frame + 2U, 27U);
    frame[29] = (uint8_t)(crc >> 8U);
    frame[30] = (uint8_t)crc;
    frame[31] = 0x77U;

    uint8_t stream[128] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame, 8U, &telemetry));
    assert(esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame + 8U, sizeof(frame) - 8U, &telemetry));
    assert(telemetry.pack_voltage_mv == 52000U && telemetry.soc_percent == 85U);
    frame[30] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_jbd_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    return 0;
}
