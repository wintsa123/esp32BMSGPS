#include "esp_bms_em02_protocol.h"

#include <limits.h>
#include <string.h>

static const uint8_t s_requests[4][ESP_BMS_EM02_REQUEST_LEN] = {
    { 0x01, 0x03, 0x0F, 0xA0, 0x00, 0x59, 0x86, 0xC6 },
    { 0x01, 0x03, 0x0F, 0xFC, 0x00, 0x36, 0x06, 0xF8 },
    { 0x01, 0x03, 0x10, 0x68, 0x00, 0x2D, 0x00, 0xCB },
    { 0x01, 0x03, 0x10, 0x38, 0x00, 0x30, 0xC0, 0xD3 },
};
static const uint8_t s_byte_counts[4] = { 178U, 108U, 90U, 96U };

static uint16_t crc16(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                                   : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

/* Motexa v6.w0.e/f and the EM02 v70 reference both read little endian. */
static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

bool esp_bms_em02_poll_request(uint8_t poll_index, uint8_t out[ESP_BMS_EM02_REQUEST_LEN])
{
    if (!out || poll_index >= 4U) {
        return false;
    }
    memcpy(out, s_requests[poll_index], ESP_BMS_EM02_REQUEST_LEN);
    return true;
}

static bool model_is_em02(const uint8_t *frame, size_t frame_len)
{
    if (frame_len < 93U) {
        return false;
    }
    char model[27] = { 0 };
    size_t len = 0U;
    while (len < 26U && frame[67U + len] != 0U) {
        model[len] = (char)frame[67U + len];
        ++len;
    }
    return strstr(model, "EM02") != NULL || strstr(model, "LX-EM02") != NULL;
}

static bool decode_realtime(const uint8_t *frame, size_t frame_len,
                            esp_bms_bms_telemetry_t *telemetry)
{
    const uint8_t cell_count = frame[3U] > 30U ? 30U : frame[3U];
    const uint8_t temperature_count = frame[10U] > 6U ? 6U : frame[10U];
    if (cell_count == 0U || 33U + (size_t)cell_count * 2U > frame_len - 2U) {
        return false;
    }
    const int32_t current_deci_amps = (int32_t)u32le(frame + 29U) / 10;
    if (current_deci_amps < INT16_MIN || current_deci_amps > INT16_MAX || frame[109U] > 100U) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = u32le(frame + 25U);
    telemetry->current_deci_amps = (int16_t)current_deci_amps;
    telemetry->total_capacity_mah = (uint32_t)u16le(frame + 105U) * 100U;
    telemetry->soc_percent = frame[109U];
    telemetry->capacity_remaining_mah =
        telemetry->total_capacity_mah * telemetry->soc_percent / 100U;
    uint32_t sum = 0U;
    uint16_t min = UINT16_MAX;
    uint16_t max = 0U;
    for (uint8_t i = 0U; i < cell_count; ++i) {
        const uint16_t value = u16le(frame + 33U + (size_t)i * 2U);
        sum += value;
        min = value < min ? value : min;
        max = value > max ? value : max;
    }
    telemetry->min_cell_voltage_mv = min;
    telemetry->max_cell_voltage_mv = max;
    telemetry->average_cell_voltage_mv = (uint16_t)(sum / cell_count);
    telemetry->delta_cell_voltage_mv = max - min;
    /* Motexa maps the two flagged sensors to BAL/MOS and the counted sensors
     * start at offset 11. The public telemetry has four cell-temperature slots
     * followed by BAL (4) and MOS (5). */
    const bool mos_valid = frame[5U] != 0U;
    const bool balancer_valid = frame[7U] != 0U;
    if (mos_valid) {
        telemetry->temperatures_celsius[5U] = (int16_t)frame[6U] - 40;
        telemetry->temperature_valid[5U] = true;
    }
    if (balancer_valid) {
        telemetry->temperatures_celsius[4U] = (int16_t)frame[8U] - 40;
        telemetry->temperature_valid[4U] = true;
    }
    const uint8_t cell_temperature_count = temperature_count > 4U ? 4U : temperature_count;
    for (uint8_t i = 0U; i < cell_temperature_count; ++i) {
        telemetry->temperatures_celsius[i] = (int16_t)frame[11U + i] - 40;
        telemetry->temperature_valid[i] = true;
    }
    return true;
}

static bool decode_frame(const uint8_t *frame, size_t frame_len, uint8_t poll_index,
                         bool *identity_known, esp_bms_bms_telemetry_t *telemetry)
{
    if (frame_len < 5U || poll_index >= 4U || frame_len != (size_t)frame[2] + 5U || frame[0] != 0x01U ||
        frame[1] != 0x03U || frame[2] != s_byte_counts[poll_index]) {
        return false;
    }
    const uint16_t expected = crc16(frame, frame_len - 2U);
    const uint16_t actual = (uint16_t)frame[frame_len - 2U] |
                            ((uint16_t)frame[frame_len - 1U] << 8U);
    if (expected != actual) {
        return false;
    }
    if (poll_index == 0U) {
        *identity_known = model_is_em02(frame, frame_len);
        return true;
    }
    if (poll_index == 1U) {
        return decode_realtime(frame, frame_len, telemetry);
    }
    /* The remaining Motexa reads are valid pages that this implementation does
     * not project into public telemetry yet. Validate and consume their frames. */
    return true;
}

bool esp_bms_em02_feed(uint8_t *stream, size_t *stream_len, size_t stream_capacity,
                       const uint8_t *chunk, size_t chunk_len, uint8_t *response_index,
                       bool *identity_known, esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || (!chunk && chunk_len != 0U) || !response_index ||
        !identity_known || !telemetry || stream_capacity == 0U) {
        return false;
    }
    if (*stream_len > stream_capacity || chunk_len > stream_capacity - *stream_len) {
        *stream_len = 0U;
        return false;
    }
    if (chunk_len != 0U) {
        memcpy(stream + *stream_len, chunk, chunk_len);
        *stream_len += chunk_len;
    }
    while (*stream_len >= 3U) {
        uint8_t page = 0U;
        while (page < 4U && stream[2U] != s_byte_counts[page]) {
            ++page;
        }
        if (stream[0U] != 0x01U || stream[1U] != 0x03U || page == 4U) {
            memmove(stream, stream + 1U, --*stream_len);
            continue;
        }
        const size_t frame_len = (size_t)stream[2U] + 5U;
        if (frame_len > stream_capacity) {
            memmove(stream, stream + 1U, --*stream_len);
            continue;
        }
        if (*stream_len < frame_len) {
            break;
        }
        if (!decode_frame(stream, frame_len, page, identity_known, telemetry)) {
            memmove(stream, stream + 1U, --*stream_len);
            continue;
        }
        memmove(stream, stream + frame_len, *stream_len - frame_len);
        *stream_len -= frame_len;
        *response_index = page;
        return true;
    }
    return false;
}
