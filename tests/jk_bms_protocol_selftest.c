#include "esp_bms_jk_protocol.h"

#include <assert.h>
#include <string.h>

static void put_u32_le(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
    data[offset + 2U] = (uint8_t)(value >> 16U);
    data[offset + 3U] = (uint8_t)(value >> 24U);
}

int main(void)
{
    uint8_t request[20] = { 0 };
    assert(esp_bms_jk_poll_request(0U, request));
    assert(request[0] == 0xAAU && request[4] == 0x96U);

    uint8_t frame[ESP_BMS_JK_FRAME_LEN] = { 0 };
    frame[0] = 0x55U;
    frame[1] = 0xAAU;
    frame[2] = 0xEBU;
    frame[3] = 0x90U;
    frame[4] = 0x02U;
    frame[6] = 0xD0U;
    frame[7] = 0x0FU;
    frame[8] = 0xE0U;
    frame[9] = 0x0FU;
    put_u32_le(frame, 150U, 52000U);
    put_u32_le(frame, 158U, 1230U);
    frame[173] = 76U;
    put_u32_le(frame, 174U, 320000U);
    put_u32_le(frame, 178U, 400000U);
    frame[144] = 0x2CU;
    frame[145] = 0x01U;
    uint8_t checksum = 0U;
    for (size_t index = 0U; index < sizeof(frame) - 1U; ++index) {
        checksum = (uint8_t)(checksum + frame[index]);
    }
    frame[sizeof(frame) - 1U] = checksum;

    uint8_t stream[320] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    assert(!esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, 100U, &telemetry));
    assert(esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame + 100U, sizeof(frame) - 100U, &telemetry));
    assert(telemetry.pack_voltage_mv == 52000U && telemetry.soc_percent == 76U);
    frame[299] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    return 0;
}
