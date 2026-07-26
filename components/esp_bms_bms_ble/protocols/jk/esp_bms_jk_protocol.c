#include "esp_bms_jk_protocol.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *data, size_t offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data, size_t offset)
{
    return (uint32_t)read_u16_le(data, offset) | ((uint32_t)read_u16_le(data, offset + 2U) << 16U);
}

static uint8_t sum8(const uint8_t *data, size_t len)
{
    uint8_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

bool esp_bms_jk_poll_request(uint8_t poll_index, uint8_t out[20])
{
    if (!out) {
        return false;
    }
    memset(out, 0, 20U);
    out[0] = 0xAAU;
    out[1] = 0x55U;
    out[2] = 0x90U;
    out[3] = 0xEBU;
    out[4] = poll_index == 0U ? 0x96U : 0x97U;
    out[19] = sum8(out, 19U);
    return true;
}

static bool decode_cell_info(const uint8_t frame[ESP_BMS_JK_FRAME_LEN],
                             esp_bms_bms_telemetry_t *telemetry)
{
    if (!telemetry || frame[4] != 0x02U || sum8(frame, ESP_BMS_JK_FRAME_LEN - 1U) != frame[299]) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    uint32_t total_mv = 0U;
    uint16_t minimum_mv = UINT16_MAX;
    uint16_t maximum_mv = 0U;
    uint8_t cells = 0U;
    for (uint8_t index = 0U; index < 32U; ++index) {
        const uint16_t millivolts = read_u16_le(frame, 6U + (size_t)index * 2U);
        if (millivolts == 0U) {
            continue;
        }
        total_mv += millivolts;
        minimum_mv = millivolts < minimum_mv ? millivolts : minimum_mv;
        maximum_mv = millivolts > maximum_mv ? millivolts : maximum_mv;
        ++cells;
    }
    if (cells == 0U) {
        return false;
    }
    const size_t dynamic = 32U;
    telemetry->pack_voltage_mv = read_u32_le(frame, 118U + dynamic);
    telemetry->current_deci_amps = (int16_t)(int32_t)(read_u32_le(frame, 126U + dynamic) / 100U);
    telemetry->soc_percent = frame[141U + dynamic];
    telemetry->capacity_remaining_mah = read_u32_le(frame, 142U + dynamic);
    telemetry->total_capacity_mah = read_u32_le(frame, 146U + dynamic);
    telemetry->protection_mask = read_u16_le(frame, 134U + dynamic);
    telemetry->min_cell_voltage_mv = minimum_mv;
    telemetry->max_cell_voltage_mv = maximum_mv;
    telemetry->delta_cell_voltage_mv = maximum_mv - minimum_mv;
    telemetry->average_cell_voltage_mv = (uint16_t)(total_mv / cells);
    telemetry->temperatures_celsius[0] = (int16_t)read_u16_le(frame, 112U + dynamic) / 10;
    telemetry->temperature_valid[0] = true;
    return telemetry->pack_voltage_mv != 0U;
}

bool esp_bms_jk_feed(uint8_t *stream,
                     size_t *stream_len,
                     size_t stream_capacity,
                     const uint8_t *chunk,
                     size_t chunk_len,
                     esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || !telemetry || chunk_len == 0U ||
        stream_capacity < ESP_BMS_JK_FRAME_LEN) {
        return false;
    }
    if (*stream_len + chunk_len > stream_capacity) {
        *stream_len = 0U;
    }
    memcpy(stream + *stream_len, chunk, chunk_len);
    *stream_len += chunk_len;
    while (*stream_len >= 4U) {
        size_t start = 0U;
        while (start + 3U < *stream_len &&
               !(stream[start] == 0x55U && stream[start + 1U] == 0xAAU &&
                 stream[start + 2U] == 0xEBU && stream[start + 3U] == 0x90U)) {
            ++start;
        }
        if (start != 0U) {
            memmove(stream, stream + start, *stream_len - start);
            *stream_len -= start;
        }
        if (*stream_len < ESP_BMS_JK_FRAME_LEN) {
            return false;
        }
        const bool decoded = decode_cell_info(stream, telemetry);
        memmove(stream, stream + ESP_BMS_JK_FRAME_LEN, *stream_len - ESP_BMS_JK_FRAME_LEN);
        *stream_len -= ESP_BMS_JK_FRAME_LEN;
        if (decoded) {
            return true;
        }
    }
    return false;
}
