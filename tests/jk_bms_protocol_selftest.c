#include "esp_bms_jk_protocol.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void put_u32_le(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
    data[offset + 2U] = (uint8_t)(value >> 16U);
    data[offset + 3U] = (uint8_t)(value >> 24U);
}

static void put_f32_le(uint8_t *data, size_t offset, float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    put_u32_le(data, offset, bits);
}

static void set_checksum(uint8_t frame[ESP_BMS_JK_FRAME_LEN])
{
    uint8_t checksum = 0U;
    for (size_t index = 0U; index < ESP_BMS_JK_FRAME_LEN - 1U; ++index) {
        checksum = (uint8_t)(checksum + frame[index]);
    }
    frame[ESP_BMS_JK_FRAME_LEN - 1U] = checksum;
}

static void init_frame(uint8_t frame[ESP_BMS_JK_FRAME_LEN])
{
    memset(frame, 0, ESP_BMS_JK_FRAME_LEN);
    frame[0] = 0x55U;
    frame[1] = 0xAAU;
    frame[2] = 0xEBU;
    frame[3] = 0x90U;
    frame[4] = 0x02U;
}

static void build_jk02_frame(uint8_t frame[ESP_BMS_JK_FRAME_LEN], uint8_t cells, size_t dynamic)
{
    init_frame(frame);
    for (uint8_t index = 0U; index < cells; ++index) {
        frame[6U + (size_t)index * 2U] = 0xE4U;
        frame[7U + (size_t)index * 2U] = 0x0CU;
    }
    put_u32_le(frame, 118U + dynamic, (uint32_t)cells * 3300U);
    put_u32_le(frame, 126U + dynamic, (uint32_t)-1230);
    frame[141U + dynamic] = 76U;
    put_u32_le(frame, 142U + dynamic, 320000U);
    put_u32_le(frame, 146U + dynamic, 400000U);
    put_u32_le(frame, 154U + dynamic, 1234U);
    if (dynamic == 0U) {
        frame[130] = 0x2CU;
        frame[131] = 0x01U;
        frame[136] = 0x34U;
        frame[137] = 0x12U;
    } else {
        frame[144] = 0x2CU;
        frame[145] = 0x01U;
        put_u32_le(frame, 166U, 0x12345678U);
    }
    set_checksum(frame);
}

int main(void)
{
    uint8_t request[20] = { 0 };
    assert(esp_bms_jk_poll_request(0U, request));
    assert(request[0] == 0xAAU && request[4] == 0x96U);

    uint8_t stream[320] = { 0 };
    size_t stream_len = 0U;
    esp_bms_bms_telemetry_t telemetry = { 0 };
    uint8_t frame[ESP_BMS_JK_FRAME_LEN] = { 0 };
    build_jk02_frame(frame, 16U, 32U);
    frame[172U] = 1U;
    set_checksum(frame);
    assert(!esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, 100U, &telemetry));
    assert(esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame + 100U, sizeof(frame) - 100U, &telemetry));
    assert(telemetry.pack_voltage_mv == 52800U && telemetry.current_deci_amps == -12 &&
           telemetry.protection_mask == 0x12345678U && telemetry.soc_percent == 76U &&
           telemetry.total_cycle_valid && telemetry.total_cycle_mah == 123400U &&
           telemetry.balancing_supported && telemetry.balancing_active);

    stream_len = 0U;
    build_jk02_frame(frame, 16U, 0U);
    assert(esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    assert(telemetry.pack_voltage_mv == 52800U && telemetry.current_deci_amps == -12 &&
           telemetry.protection_mask == 0x1234U && telemetry.soc_percent == 76U &&
           telemetry.total_cycle_valid && telemetry.total_cycle_mah == 123400U &&
           telemetry.balancing_supported && !telemetry.balancing_active);

    stream_len = 0U;
    put_u32_le(frame, 154U, UINT32_MAX);
    set_checksum(frame);
    assert(!esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));

    stream_len = 0U;
    init_frame(frame);
    for (uint8_t index = 0U; index < 16U; ++index) {
        put_f32_le(frame, 6U + (size_t)index * 4U, 3.3f);
    }
    set_checksum(frame);
    assert(esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    assert(telemetry.partial && telemetry.pack_voltage_mv == 52800U &&
           telemetry.min_cell_voltage_mv == 3300U && telemetry.max_cell_voltage_mv == 3300U);

    frame[299] ^= 1U;
    stream_len = 0U;
    assert(!esp_bms_jk_feed(stream, &stream_len, sizeof(stream), frame, sizeof(frame), &telemetry));
    return 0;
}
