#include "esp_bms_jbd_protocol.h"

#include <string.h>

static uint16_t read_u16_be(const uint8_t *data, size_t offset)
{
    return ((uint16_t)data[offset] << 8U) | data[offset + 1U];
}

static uint16_t checksum(const uint8_t *data, size_t len)
{
    uint16_t value = 0U;
    for (size_t index = 0U; index < len; ++index) {
        value = (uint16_t)(value - data[index]);
    }
    return value;
}

bool esp_bms_jbd_poll_request(uint8_t poll_index, uint8_t out[7])
{
    static const uint8_t commands[] = { 0x03U, 0x04U };
    if (!out) {
        return false;
    }
    out[0] = 0xDDU;
    out[1] = 0xA5U;
    out[2] = commands[poll_index % (sizeof(commands) / sizeof(commands[0]))];
    out[3] = 0U;
    const uint16_t crc = checksum(out + 2U, 2U);
    out[4] = (uint8_t)(crc >> 8U);
    out[5] = (uint8_t)crc;
    out[6] = 0x77U;
    return true;
}

static bool decode_basic(const uint8_t *payload, size_t len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!payload || !telemetry || len < 23U) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = (uint32_t)read_u16_be(payload, 0U) * 10U;
    telemetry->current_deci_amps = (int16_t)read_u16_be(payload, 2U) / 10;
    telemetry->capacity_remaining_mah = (uint32_t)read_u16_be(payload, 4U) * 10U;
    telemetry->total_capacity_mah = (uint32_t)read_u16_be(payload, 6U) * 10U;
    telemetry->protection_mask = read_u16_be(payload, 16U);
    telemetry->soc_percent = payload[19U];
    const uint8_t temperature_count = payload[22U] > ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                          ? ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                          : payload[22U];
    if (len < 23U + (size_t)temperature_count * 2U) {
        return false;
    }
    for (uint8_t index = 0U; index < temperature_count; ++index) {
        telemetry->temperatures_celsius[index] = (int16_t)((int32_t)read_u16_be(payload, 23U + (size_t)index * 2U) - 2731) / 10;
        telemetry->temperature_valid[index] = true;
    }
    return telemetry->pack_voltage_mv != 0U;
}

bool esp_bms_jbd_feed(uint8_t *stream,
                      size_t *stream_len,
                      size_t stream_capacity,
                      const uint8_t *chunk,
                      size_t chunk_len,
                      esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || !telemetry || chunk_len == 0U) {
        return false;
    }
    if (*stream_len + chunk_len > stream_capacity) {
        *stream_len = 0U;
    }
    memcpy(stream + *stream_len, chunk, chunk_len);
    *stream_len += chunk_len;
    while (*stream_len >= 4U) {
        size_t start = 0U;
        while (start < *stream_len && stream[start] != 0xDDU && stream[start] != 0xFFU) {
            ++start;
        }
        if (start != 0U) {
            memmove(stream, stream + start, *stream_len - start);
            *stream_len -= start;
        }
        if (*stream_len < 4U) {
            break;
        }
        if (stream[0] == 0xFFU) { /* FF AA authentication response */
            const size_t frame_len = *stream_len >= 4U && stream[1] == 0xAAU ? 5U + stream[3] : 1U;
            if (*stream_len < frame_len) {
                break;
            }
            memmove(stream, stream + frame_len, *stream_len - frame_len);
            *stream_len -= frame_len;
            continue;
        }
        const size_t frame_len = 7U + stream[3];
        if (frame_len > stream_capacity || *stream_len < frame_len) {
            break;
        }
        const uint16_t remote_crc = read_u16_be(stream, frame_len - 3U);
        const bool valid = stream[frame_len - 1U] == 0x77U &&
                           checksum(stream + 2U, (size_t)stream[3] + 2U) == remote_crc;
        const bool decoded = valid && stream[1] == 0x03U && stream[2] == 0x03U &&
                             decode_basic(stream + 4U, stream[3], telemetry);
        memmove(stream, stream + frame_len, *stream_len - frame_len);
        *stream_len -= frame_len;
        if (decoded) {
            return true;
        }
    }
    return false;
}
