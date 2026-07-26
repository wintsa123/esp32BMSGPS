#include "esp_bms_daly_protocol.h"

#include <assert.h>

static uint16_t crc16(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < len; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static void put_u16_be(uint8_t *data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value >> 8U);
    data[offset + 1U] = (uint8_t)value;
}

int main(void)
{
    uint8_t request[8] = { 0 };
    assert(esp_bms_daly_poll_request(0U, request));
    assert(request[0] == 0xD2U && request[1] == 0x03U);
    assert(esp_bms_daly_poll_request(2U, request));
    assert(request[0] == 0x81U);

    uint8_t frame[129] = { 0 };
    frame[0] = 0xD2U;
    frame[1] = 0x03U;
    frame[2] = 124U;
    put_u16_be(frame, 3U, 4100U);
    put_u16_be(frame, 5U, 4200U);
    put_u16_be(frame, 83U, 520U);
    put_u16_be(frame, 85U, 30123U);
    put_u16_be(frame, 87U, 785U);
    put_u16_be(frame, 89U, 4200U);
    put_u16_be(frame, 91U, 4100U);
    put_u16_be(frame, 99U, 1234U);
    frame[102U] = 2U;
    frame[104U] = 1U;
    put_u16_be(frame, 67U, 65U);
    const uint16_t crc = crc16(frame, sizeof(frame) - 2U);
    frame[127U] = (uint8_t)crc;
    frame[128U] = (uint8_t)(crc >> 8U);

    uint8_t stream[192] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_daly_feed(stream, &stream_len, sizeof(stream), frame, 64U, &telemetry));
    assert(esp_bms_daly_feed(stream, &stream_len, sizeof(stream), frame + 64U, sizeof(frame) - 64U, &telemetry));
    assert(telemetry.pack_voltage_mv == 52000U && telemetry.soc_percent == 78U);
    frame[128U] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_daly_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    return 0;
}
