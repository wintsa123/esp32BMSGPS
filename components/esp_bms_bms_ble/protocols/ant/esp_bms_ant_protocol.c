#include "esp_bms_ant_protocol.h"

#include <string.h>

#define ANT_FRAME_MIN_LEN 10U
#define ANT_FRAME_START_1 0x7EU
#define ANT_FRAME_START_2 0xA1U
#define ANT_FRAME_END_1 0xAAU
#define ANT_FRAME_END_2 0x55U
#define ANT_FRAME_TYPE_STATUS 0x11U
#define ANT_FRAME_TYPE_DEVICE_INFO 0x12U
#define ANT_MAX_CELLS 32U
#define ANT_MAX_TEMPERATURE_SENSORS 4U

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

static bool read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || index > len || len - index < 2U) {
        return false;
    }
    *out = (uint16_t)data[index] | ((uint16_t)data[index + 1U] << 8U);
    return true;
}

static bool read_i16_le(const uint8_t *data, size_t len, size_t index, int16_t *out)
{
    uint16_t value = 0U;
    if (!read_u16_le(data, len, index, &value)) {
        return false;
    }
    *out = (int16_t)value;
    return true;
}

static bool read_u32_le(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || index > len || len - index < 4U) {
        return false;
    }
    *out = (uint32_t)data[index] | ((uint32_t)data[index + 1U] << 8U) |
           ((uint32_t)data[index + 2U] << 16U) | ((uint32_t)data[index + 3U] << 24U);
    return true;
}

static bool read_u64_le(const uint8_t *data, size_t len, size_t index, uint64_t *out)
{
    if (!data || !out || index > len || len - index < 8U) {
        return false;
    }
    uint64_t value = 0ULL;
    for (uint8_t offset = 0U; offset < 8U; ++offset) {
        value |= (uint64_t)data[index + offset] << (offset * 8U);
    }
    *out = value;
    return true;
}

static bool validate_frame(const uint8_t *data, size_t len, uint8_t *function, size_t *protocol_len)
{
    if (!data || !function || !protocol_len || len < ANT_FRAME_MIN_LEN ||
        data[0] != ANT_FRAME_START_1 || data[1] != ANT_FRAME_START_2 ||
        data[len - 2U] != ANT_FRAME_END_1 || data[len - 1U] != ANT_FRAME_END_2) {
        return false;
    }
    *function = data[2U];
    *protocol_len = 10U + data[5U];
    if (*protocol_len > len || *protocol_len < ANT_FRAME_MIN_LEN ||
        (*function != ANT_FRAME_TYPE_DEVICE_INFO && *protocol_len != len)) {
        return false;
    }
    const size_t crc_offset = *protocol_len - 4U;
    const uint16_t remote_crc = (uint16_t)data[crc_offset] | ((uint16_t)data[crc_offset + 1U] << 8U);
    return crc16_modbus(data + 1U, crc_offset - 1U) == remote_crc;
}

static bool decode_status(const uint8_t *data, size_t len, esp_bms_bms_telemetry_t *telemetry)
{
    const uint8_t temperature_sensor_count = data[8U];
    const uint8_t cell_count = data[9U];
    if (!telemetry || cell_count > ANT_MAX_CELLS || temperature_sensor_count > ANT_MAX_TEMPERATURE_SENSORS) {
        return false;
    }
    const size_t dynamic_offset = (size_t)cell_count * 2U + (size_t)temperature_sensor_count * 2U;
    uint16_t pack_voltage_dv = 0U;
    int16_t current_deci_amps = 0;
    uint16_t soc_percent = 0U;
    uint32_t total_capacity_uah = 0U;
    uint32_t capacity_remaining_uah = 0U;
    uint16_t max_cell_mv = 0U;
    uint16_t min_cell_mv = 0U;
    uint16_t delta_cell_mv = 0U;
    uint16_t average_cell_mv = 0U;
    uint64_t protection_mask = 0ULL;
    uint64_t warning_mask = 0ULL;
    if (!read_u64_le(data, len, 10U, &protection_mask) ||
        !read_u64_le(data, len, 18U, &warning_mask) ||
        !read_u16_le(data, len, 38U + dynamic_offset, &pack_voltage_dv) ||
        !read_i16_le(data, len, 40U + dynamic_offset, &current_deci_amps) ||
        !read_u16_le(data, len, 42U + dynamic_offset, &soc_percent) ||
        !read_u32_le(data, len, 50U + dynamic_offset, &total_capacity_uah) ||
        !read_u32_le(data, len, 54U + dynamic_offset, &capacity_remaining_uah) ||
        !read_u16_le(data, len, 74U + dynamic_offset, &max_cell_mv) ||
        !read_u16_le(data, len, 78U + dynamic_offset, &min_cell_mv) ||
        !read_u16_le(data, len, 82U + dynamic_offset, &delta_cell_mv) ||
        !read_u16_le(data, len, 84U + dynamic_offset, &average_cell_mv)) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = (uint32_t)pack_voltage_dv * 10U;
    telemetry->current_deci_amps = current_deci_amps;
    telemetry->soc_percent = soc_percent;
    telemetry->max_cell_voltage_mv = max_cell_mv;
    telemetry->min_cell_voltage_mv = min_cell_mv;
    telemetry->delta_cell_voltage_mv = delta_cell_mv;
    telemetry->average_cell_voltage_mv = average_cell_mv;
    telemetry->total_capacity_mah = total_capacity_uah / 1000U;
    telemetry->capacity_remaining_mah = capacity_remaining_uah / 1000U;
    telemetry->protection_mask = protection_mask;
    telemetry->warning_mask = warning_mask;
    const size_t temperature_offset = 34U + (size_t)cell_count * 2U;
    const uint8_t temperature_count = temperature_sensor_count > 4U ? 4U : temperature_sensor_count;
    for (uint8_t index = 0U; index < temperature_count; ++index) {
        if (!read_i16_le(data, len, temperature_offset + (size_t)index * 2U,
                         &telemetry->temperatures_celsius[index])) {
            return false;
        }
        telemetry->temperature_valid[index] = true;
    }
    if (!read_i16_le(data, len, 34U + dynamic_offset, &telemetry->temperatures_celsius[4U]) ||
        !read_i16_le(data, len, 36U + dynamic_offset, &telemetry->temperatures_celsius[5U])) {
        return false;
    }
    telemetry->temperature_valid[4U] = true;
    telemetry->temperature_valid[5U] = true;
    return true;
}

bool esp_bms_ant_protocol_decode(const uint8_t *data,
                                 size_t len,
                                 esp_bms_bms_telemetry_t *telemetry,
                                 bool *device_info)
{
    uint8_t function = 0U;
    size_t protocol_len = 0U;
    if (device_info) {
        *device_info = false;
    }
    if (!validate_frame(data, len, &function, &protocol_len)) {
        return false;
    }
    if (function == ANT_FRAME_TYPE_STATUS) {
        return decode_status(data, protocol_len, telemetry);
    }
    if (function == ANT_FRAME_TYPE_DEVICE_INFO) {
        if (device_info) {
            *device_info = true;
        }
        return true;
    }
    return false;
}
