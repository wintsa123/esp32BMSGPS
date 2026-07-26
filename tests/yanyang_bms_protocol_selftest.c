#include "esp_bms_yanyang_protocol.h"

#include <assert.h>
#include <string.h>

static void put_u16_be(uint8_t *data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value >> 8U);
    data[offset + 1U] = (uint8_t)value;
}

static void put_u32_be(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value >> 24U);
    data[offset + 1U] = (uint8_t)(value >> 16U);
    data[offset + 2U] = (uint8_t)(value >> 8U);
    data[offset + 3U] = (uint8_t)value;
}

static void make_main_page(uint8_t frame[99])
{
    memset(frame, 0, 99U);
    frame[0] = 0x01U;
    frame[1] = 0x03U;
    frame[2] = 94U;
    frame[15] = 3U;
    frame[16] = 4U;
    put_u32_be(frame, 25U, 52500U);
    put_u16_be(frame, 29U, (uint16_t)-1234);
    put_u16_be(frame, 37U, 4200U);
    put_u16_be(frame, 39U, 4100U);
    put_u16_be(frame, 41U, 213U);
    put_u16_be(frame, 43U, 4567U);
    put_u16_be(frame, 45U, 8000U);
    put_u16_be(frame, 47U, 4100U);
    put_u16_be(frame, 49U, 4150U);
    put_u16_be(frame, 51U, 4200U);
    frame[83] = 68U;
    frame[84] = 72U;
    frame[85] = 69U;
    frame[86] = 70U;
    const uint16_t crc = esp_bms_yanyang_crc16(frame, 97U);
    frame[97] = (uint8_t)(crc >> 8U);
    frame[98] = (uint8_t)crc;
}

int main(void)
{
    const uint8_t requests[4][8] = {
        { 0x01U, 0x03U, 0x00U, 0x3FU, 0x00U, 0x04U, 0x05U, 0x74U },
        { 0x01U, 0x03U, 0x00U, 0x01U, 0x00U, 0x2FU, 0xD6U, 0x55U },
        { 0x01U, 0x03U, 0x00U, 0x30U, 0x00U, 0x2AU, 0x1AU, 0xC4U },
        { 0x01U, 0x03U, 0x00U, 0x5AU, 0x00U, 0x16U, 0x17U, 0xE4U },
    };
    for (uint8_t index = 0U; index < 4U; ++index) {
        uint8_t request[8] = { 0 };
        assert(esp_bms_yanyang_poll_request(index, request));
        assert(memcmp(request, requests[index], sizeof(request)) == 0);
    }
    uint8_t frame[99];
    make_main_page(frame);
    uint8_t stream[256] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_yanyang_feed(stream, &stream_len, sizeof(stream), frame, 31U, &telemetry));
    assert(stream_len == 31U);
    assert(esp_bms_yanyang_feed(stream, &stream_len, sizeof(stream), frame + 31U, 68U, &telemetry));
    assert(stream_len == 0U);
    assert(telemetry.pack_voltage_mv == 52500U);
    assert(telemetry.current_deci_amps == -123);
    assert(telemetry.soc_percent == 85U);
    assert(telemetry.capacity_remaining_mah == 456700U);
    assert(telemetry.total_capacity_mah == 800000U);
    assert(telemetry.min_cell_voltage_mv == 4100U && telemetry.max_cell_voltage_mv == 4200U);
    assert(telemetry.delta_cell_voltage_mv == 100U && telemetry.average_cell_voltage_mv == 4150U);
    assert(telemetry.temperature_valid[0U] && telemetry.temperatures_celsius[0U] == 28);
    assert(telemetry.temperature_valid[1U] && telemetry.temperatures_celsius[1U] == 30);
    assert(telemetry.temperature_valid[4U] && telemetry.temperatures_celsius[4U] == 29);
    assert(telemetry.temperature_valid[5U] && telemetry.temperatures_celsius[5U] == 32);
    assert(!telemetry.temperature_valid[2U] && !telemetry.temperature_valid[3U]);

    uint8_t joined[198] = { 0 };
    memcpy(joined, frame, sizeof(frame));
    memcpy(joined + sizeof(frame), frame, sizeof(frame));
    assert(esp_bms_yanyang_feed(stream, &stream_len, sizeof(stream), joined, sizeof(joined), &telemetry));
    assert(stream_len == sizeof(frame));
    assert(esp_bms_yanyang_feed(stream, &stream_len, sizeof(stream), joined, 1U, &telemetry));
    assert(stream_len == 1U);

    make_main_page(frame);
    frame[98] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_yanyang_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    assert(stream_len == 0U);
    return 0;
}
