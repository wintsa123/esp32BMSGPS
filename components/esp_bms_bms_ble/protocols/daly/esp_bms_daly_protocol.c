#include "esp_bms_daly_protocol.h"

#include <string.h>

static uint16_t read_u16_be(const uint8_t *data, size_t offset)
{
    return ((uint16_t)data[offset] << 8U) | data[offset + 1U];
}

static uint16_t crc16_modbus(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < len; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

bool esp_bms_daly_poll_request(uint8_t poll_index, uint8_t out[8])
{
    if (!out) {
        return false;
    }
    const bool p81 = (poll_index & 2U) != 0U;
    out[0] = p81 ? 0x81U : 0xD2U;
    out[1] = 0x03U;
    if (p81) {
        const bool status = (poll_index & 1U) != 0U;
        out[2] = status ? 0x00U : 0x00U;
        out[3] = status ? 0x41U : 0x00U;
        out[4] = 0U;
        out[5] = status ? 62U : 64U;
    } else {
        out[2] = 0U;
        out[3] = 0U;
        out[4] = 0U;
        out[5] = poll_index == 1U ? 80U : 62U;
    }
    const uint16_t crc = crc16_modbus(out, 6U);
    out[6] = (uint8_t)crc;
    out[7] = (uint8_t)(crc >> 8U);
    return true;
}

static bool decode_d2_status(const uint8_t *frame, size_t frame_len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!telemetry || (frame_len != 129U && frame_len != 165U)) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    const uint8_t cells = frame[102U] > 32U ? 32U : frame[102U];
    if (cells == 0U) {
        return false;
    }
    uint32_t cell_sum = 0U;
    uint16_t minimum = UINT16_MAX;
    uint16_t maximum = 0U;
    for (uint8_t index = 0U; index < cells; ++index) {
        const uint16_t value = read_u16_be(frame, 3U + (size_t)index * 2U);
        if (value == 0U) {
            continue;
        }
        cell_sum += value;
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
    }
    if (maximum == 0U) {
        return false;
    }
    telemetry->pack_voltage_mv = (uint32_t)read_u16_be(frame, 83U) * 100U;
    telemetry->current_deci_amps = (int16_t)((int32_t)read_u16_be(frame, 85U) - 30000);
    telemetry->soc_percent = read_u16_be(frame, 87U) / 10U;
    telemetry->max_cell_voltage_mv = read_u16_be(frame, 89U);
    telemetry->min_cell_voltage_mv = read_u16_be(frame, 91U);
    telemetry->average_cell_voltage_mv = read_u16_be(frame, 113U);
    telemetry->delta_cell_voltage_mv = read_u16_be(frame, 115U);
    telemetry->capacity_remaining_mah = (uint32_t)read_u16_be(frame, 99U) * 100U;
    telemetry->protection_mask = ((uint64_t)read_u16_be(frame, 119U) << 48U) |
                                 ((uint64_t)read_u16_be(frame, 121U) << 32U) |
                                 ((uint64_t)read_u16_be(frame, 123U) << 16U) |
                                 read_u16_be(frame, 125U);
    const uint8_t temperatures = frame[104U] > ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                     ? ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                     : frame[104U];
    for (uint8_t index = 0U; index < temperatures; ++index) {
        telemetry->temperatures_celsius[index] = (int16_t)read_u16_be(frame, 67U + (size_t)index * 2U) - 40;
        telemetry->temperature_valid[index] = true;
    }
    return telemetry->pack_voltage_mv != 0U;
}

static bool decode_p81_cells(const uint8_t *frame, size_t frame_len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!telemetry || frame_len != 133U) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    const uint8_t cells = frame[123U] > 32U ? 32U : frame[123U];
    if (cells == 0U) {
        return false;
    }
    uint32_t sum = 0U;
    uint16_t minimum = UINT16_MAX;
    uint16_t maximum = 0U;
    for (uint8_t index = 0U; index < cells; ++index) {
        const uint16_t value = read_u16_be(frame, 3U + (size_t)index * 2U);
        if (value == 0U) {
            continue;
        }
        sum += value;
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
    }
    if (maximum == 0U) {
        return false;
    }
    telemetry->pack_voltage_mv = (uint32_t)read_u16_be(frame, 115U) * 100U;
    telemetry->current_deci_amps = (int16_t)((int32_t)read_u16_be(frame, 117U) - 30000);
    telemetry->soc_percent = read_u16_be(frame, 119U) / 10U;
    telemetry->min_cell_voltage_mv = minimum;
    telemetry->max_cell_voltage_mv = maximum;
    telemetry->delta_cell_voltage_mv = maximum - minimum;
    telemetry->average_cell_voltage_mv = (uint16_t)(sum / cells);
    const uint8_t temperatures = frame[125U] > ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                     ? ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT
                                     : frame[125U];
    for (uint8_t index = 0U; index < temperatures; ++index) {
        telemetry->temperatures_celsius[index] = (int16_t)read_u16_be(frame, 99U + (size_t)index * 2U) - 40;
        telemetry->temperature_valid[index] = true;
    }
    return telemetry->pack_voltage_mv != 0U;
}

bool esp_bms_daly_feed(uint8_t *stream,
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
    while (*stream_len >= 3U) {
        size_t start = 0U;
        while (start < *stream_len && stream[start] != 0xD2U && stream[start] != 0x51U) {
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
        if (frame_len > stream_capacity || *stream_len < frame_len) {
            break;
        }
        const uint16_t remote_crc = (uint16_t)stream[frame_len - 2U] |
                                    ((uint16_t)stream[frame_len - 1U] << 8U);
        const bool valid = stream[1U] == 0x03U && crc16_modbus(stream, frame_len - 2U) == remote_crc;
        const bool decoded = valid && (stream[0U] == 0xD2U
                                            ? decode_d2_status(stream, frame_len, telemetry)
                                            : decode_p81_cells(stream, frame_len, telemetry));
        memmove(stream, stream + frame_len, *stream_len - frame_len);
        *stream_len -= frame_len;
        if (decoded) {
            return true;
        }
    }
    return false;
}
