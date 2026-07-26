#include "esp_bms_yanyang_protocol.h"

#include <string.h>

/* UUIDs are stored in NimBLE's little-endian byte order. */
const uint8_t esp_bms_yanyang_service_uuid[ESP_BMS_YANYANG_UUID_LEN] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x59, 0xFE, 0x00, 0x00,
};
const uint8_t esp_bms_yanyang_write_uuid[ESP_BMS_YANYANG_UUID_LEN] = {
    0x50, 0xEA, 0xDA, 0x30, 0x88, 0x83, 0xB8, 0x9F,
    0x60, 0x4F, 0x15, 0xF3, 0x01, 0x00, 0xC9, 0x8E,
};
const uint8_t esp_bms_yanyang_notify_uuid[ESP_BMS_YANYANG_UUID_LEN] = {
    0x50, 0xEA, 0xDA, 0x30, 0x88, 0x83, 0xB8, 0x9F,
    0x60, 0x4F, 0x15, 0xF3, 0x02, 0x00, 0xC9, 0x8E,
};

static const uint16_t s_poll_registers[][2] = {
    { 0x003FU, 4U }, { 0x0001U, 47U }, { 0x0030U, 42U }, { 0x005AU, 22U },
};

static uint16_t read_u16_be(const uint8_t *data, size_t offset)
{
    return ((uint16_t)data[offset] << 8U) | data[offset + 1U];
}

static uint32_t read_u32_be(const uint8_t *data, size_t offset)
{
    return ((uint32_t)data[offset] << 24U) | ((uint32_t)data[offset + 1U] << 16U) |
           ((uint32_t)data[offset + 2U] << 8U) | data[offset + 3U];
}

uint16_t esp_bms_yanyang_crc16(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    if (!bytes) {
        return 0U;
    }
    for (size_t index = 0U; index < len; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

bool esp_bms_yanyang_poll_request(uint8_t poll_index, uint8_t out[8])
{
    if (!out || poll_index >= sizeof(s_poll_registers) / sizeof(s_poll_registers[0])) {
        return false;
    }
    const uint16_t start = s_poll_registers[poll_index][0];
    const uint16_t count = s_poll_registers[poll_index][1];
    out[0] = 0x01U;
    out[1] = 0x03U;
    out[2] = (uint8_t)(start >> 8U);
    out[3] = (uint8_t)start;
    out[4] = (uint8_t)(count >> 8U);
    out[5] = (uint8_t)count;
    const uint16_t crc = esp_bms_yanyang_crc16(out, 6U);
    out[6] = (uint8_t)(crc >> 8U);
    out[7] = (uint8_t)crc;
    return true;
}

static bool decode_main_page(const uint8_t *frame,
                             size_t frame_len,
                             esp_bms_bms_telemetry_t *telemetry)
{
    const uint8_t cell_count = frame[15U];
    const uint8_t temperature_count = frame[16U];
    const size_t cells_end = 47U + (size_t)cell_count * 2U;
    if (!telemetry || cell_count == 0U || cell_count > 32U || temperature_count > 6U ||
        cells_end > frame_len - 2U || frame[2U] != 94U) {
        return false;
    }

    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = read_u32_be(frame, 25U);
    telemetry->current_deci_amps = (int16_t)((int16_t)read_u16_be(frame, 29U) / 10);
    telemetry->max_cell_voltage_mv = read_u16_be(frame, 37U);
    telemetry->min_cell_voltage_mv = read_u16_be(frame, 39U);
    telemetry->delta_cell_voltage_mv = telemetry->max_cell_voltage_mv - telemetry->min_cell_voltage_mv;
    telemetry->soc_percent = (uint16_t)((uint32_t)read_u16_be(frame, 41U) * 4U / 10U);
    telemetry->capacity_remaining_mah = (uint32_t)read_u16_be(frame, 43U) * 100U;
    telemetry->total_capacity_mah = (uint32_t)read_u16_be(frame, 45U) * 100U;

    uint32_t cell_total_mv = 0U;
    for (uint8_t index = 0U; index < cell_count; ++index) {
        cell_total_mv += read_u16_be(frame, 47U + (size_t)index * 2U);
    }
    telemetry->average_cell_voltage_mv = (uint16_t)(cell_total_mv / cell_count);

    /* APK v3.4.11 maps the four on-frame bytes as MOS, T1, T2, BAL. */
    if (temperature_count >= 2U) {
        telemetry->temperatures_celsius[0U] = (int16_t)frame[83U] - 40;
        telemetry->temperature_valid[0U] = true;
    }
    if (temperature_count >= 3U) {
        telemetry->temperatures_celsius[1U] = (int16_t)frame[86U] - 40;
        telemetry->temperature_valid[1U] = true;
    }
    if (temperature_count >= 4U) {
        telemetry->temperatures_celsius[4U] = (int16_t)frame[85U] - 40;
        telemetry->temperature_valid[4U] = true;
    }
    if (temperature_count >= 1U) {
        telemetry->temperatures_celsius[5U] = (int16_t)frame[84U] - 40;
        telemetry->temperature_valid[5U] = true;
    }
    return true;
}

static bool decode_frame(const uint8_t *frame, size_t frame_len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!frame || frame_len < 5U || frame[1U] != 0x03U || frame_len != (size_t)frame[2U] + 5U) {
        return false;
    }
    const uint16_t expected_crc = esp_bms_yanyang_crc16(frame, frame_len - 2U);
    const uint16_t remote_crc = ((uint16_t)frame[frame_len - 2U] << 8U) | frame[frame_len - 1U];
    if (expected_crc != remote_crc) {
        return false;
    }
    return frame[2U] == 94U && decode_main_page(frame, frame_len, telemetry);
}

bool esp_bms_yanyang_feed(uint8_t *stream,
                          size_t *stream_len,
                          size_t stream_capacity,
                          const uint8_t *chunk,
                          size_t chunk_len,
                          esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || !telemetry || chunk_len == 0U || chunk_len > stream_capacity) {
        return false;
    }
    if (*stream_len + chunk_len > stream_capacity) {
        *stream_len = 0U;
    }
    memcpy(stream + *stream_len, chunk, chunk_len);
    *stream_len += chunk_len;

    while (*stream_len >= 3U) {
        size_t start = 0U;
        while (start + 1U < *stream_len && stream[start + 1U] != 0x03U) {
            ++start;
        }
        if (start != 0U) {
            memmove(stream, stream + start, *stream_len - start);
            *stream_len -= start;
        }
        if (*stream_len < 3U) {
            break;
        }
        const size_t frame_len = (size_t)stream[2U] + 5U;
        if (frame_len > stream_capacity) {
            memmove(stream, stream + 1U, *stream_len - 1U);
            --*stream_len;
            continue;
        }
        if (*stream_len < frame_len) {
            break;
        }
        const bool decoded = decode_frame(stream, frame_len, telemetry);
        memmove(stream, stream + frame_len, *stream_len - frame_len);
        *stream_len -= frame_len;
        if (decoded) {
            return true;
        }
    }
    return false;
}
