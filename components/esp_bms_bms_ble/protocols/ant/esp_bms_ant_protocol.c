#include "esp_bms_ant_protocol.h"

#include <limits.h>
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
#define ANT_OLD_FRAME_START_1 0xAAU
#define ANT_OLD_FRAME_START_2 0x55U
#define ANT_OLD_FRAME_START_3 0xAAU

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

static bool read_u16_be(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || index > len || len - index < 2U) {
        return false;
    }
    *out = ((uint16_t)data[index] << 8U) | data[index + 1U];
    return true;
}

static bool read_u32_be(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || index > len || len - index < 4U) {
        return false;
    }
    *out = ((uint32_t)data[index] << 24U) | ((uint32_t)data[index + 1U] << 16U) |
           ((uint32_t)data[index + 2U] << 8U) | data[index + 3U];
    return true;
}

/* Returns 0 for incomplete input and SIZE_MAX for invalid input. */
static size_t frame_length(const uint8_t *data, size_t len)
{
    if (len == 0U) {
        return 0U;
    }
    if (data[0] != ANT_FRAME_START_1 || (len >= 2U && data[1] != ANT_FRAME_START_2)) {
        return SIZE_MAX;
    }
    if (len < 6U) {
        return 0U;
    }
    size_t segment_start = 1U;
    size_t crc_offset = 6U + data[5U];
    for (;;) {
        if (crc_offset + 2U > len) {
            return 0U;
        }
        const uint16_t remote_crc = (uint16_t)data[crc_offset] |
                                    ((uint16_t)data[crc_offset + 1U] << 8U);
        if (crc16_modbus(data + segment_start, crc_offset - segment_start) != remote_crc) {
            return SIZE_MAX;
        }
        const size_t next = crc_offset + 2U;
        if (len - next < 2U) {
            return 0U;
        }
        if (data[next] == ANT_FRAME_END_1 && data[next + 1U] == ANT_FRAME_END_2) {
            return next + 2U;
        }
        if (data[next] == ANT_FRAME_START_1 && data[next + 1U] == ANT_FRAME_START_2) {
            return SIZE_MAX;
        }
        if (len - next < 4U) {
            return 0U;
        }
        segment_start = next;
        crc_offset = next + 4U + data[next + 3U];
    }
}

static bool validate_frame(const uint8_t *data, size_t len, uint8_t *function, size_t *payload_end)
{
    if (!data || !function || !payload_end || len < ANT_FRAME_MIN_LEN ||
        frame_length(data, len) != len) {
        return false;
    }
    *function = data[2U];
    *payload_end = 6U + data[5U];
    return true;
}

static bool decode_status(const uint8_t *data, size_t len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!data || !telemetry || len < 10U) {
        return false;
    }
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
    uint32_t total_cycle_mah = 0U;
    uint32_t running_time_seconds = 0U;
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
        !read_u32_le(data, len, 58U + dynamic_offset, &total_cycle_mah) ||
        !read_u32_le(data, len, 66U + dynamic_offset, &running_time_seconds) ||
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
    telemetry->total_cycle_mah = total_cycle_mah;
    telemetry->total_cycle_valid = true;
    telemetry->running_time_seconds = running_time_seconds;
    telemetry->running_time_valid = true;
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
    esp_bms_bms_ant_diagnostics_t *const diagnostics = &telemetry->ant_diagnostics;
    diagnostics->cell_count = cell_count;
    for (uint8_t index = 0U; index < cell_count; ++index) {
        uint16_t voltage = 0U;
        if (!read_u16_le(data, len, 34U + (size_t)index * 2U, &voltage)) {
            return false;
        }
        diagnostics->cell_voltage_mv[index] = voltage & 0x1FFFU;
    }
    uint32_t power = 0U;
    if (!read_u16_le(data, len, 44U + dynamic_offset, &diagnostics->soh_percent) ||
        !read_u32_le(data, len, 62U + dynamic_offset, &power) ||
        !read_u32_le(data, len, 70U + dynamic_offset, &diagnostics->balance_mask)) {
        return false;
    }
    diagnostics->power_w = (int32_t)power;
    diagnostics->discharge_mos_state = data[46U + dynamic_offset];
    diagnostics->charge_mos_state = data[47U + dynamic_offset];
    diagnostics->balance_state = data[48U + dynamic_offset];
    diagnostics->valid = true;
    telemetry->balancing_supported = true;
    /* State 3 is a thermal stop, not active balancing. */
    telemetry->balancing_active = diagnostics->balance_mask != 0U ||
                                  diagnostics->balance_state == 1U ||
                                  diagnostics->balance_state == 2U ||
                                  diagnostics->balance_state == 4U ||
                                  diagnostics->balance_state == 5U;
    const size_t ext_offset = 112U + dynamic_offset;
    diagnostics->charge_time_valid = read_u32_le(data, len, ext_offset,
                                                 &diagnostics->charge_time_seconds);
    diagnostics->last_charge_gap_valid = read_u32_le(data, len, ext_offset + 4U,
                                                     &diagnostics->last_charge_gap_seconds);
    diagnostics->remaining_charge_valid = read_u16_le(data, len, ext_offset + 8U,
                                                      &diagnostics->remaining_charge_minutes);
    diagnostics->remaining_discharge_valid = read_u16_le(data, len, ext_offset + 10U,
                                                         &diagnostics->remaining_discharge_minutes);
    return true;
}

static bool decode_old_status(const uint8_t *data, size_t len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!data || !telemetry || len != ESP_BMS_ANT_OLD_FRAME_LEN ||
        data[0] != ANT_OLD_FRAME_START_1 || data[1] != ANT_OLD_FRAME_START_2 ||
        data[2] != ANT_OLD_FRAME_START_3) {
        return false;
    }
    uint16_t checksum = 0U;
    for (size_t index = 4U; index < ESP_BMS_ANT_OLD_FRAME_LEN - 2U; ++index) {
        checksum = (uint16_t)(checksum + data[index]);
    }
    uint16_t remote_checksum = 0U;
    uint16_t pack_voltage_dv = 0U;
    uint32_t current_raw = 0U;
    uint32_t total_capacity_uah = 0U;
    uint32_t capacity_remaining_uah = 0U;
    uint32_t total_cycle_mah = 0U;
    uint32_t running_time_seconds = 0U;
    uint16_t max_cell_mv = 0U;
    uint16_t min_cell_mv = 0U;
    uint16_t average_cell_mv = 0U;
    if (!read_u16_be(data, len, ESP_BMS_ANT_OLD_FRAME_LEN - 2U, &remote_checksum) ||
        checksum != remote_checksum || !read_u16_be(data, len, 4U, &pack_voltage_dv) ||
        !read_u32_be(data, len, 70U, &current_raw) || !read_u32_be(data, len, 75U, &total_capacity_uah) ||
        !read_u32_be(data, len, 79U, &capacity_remaining_uah) || !read_u32_be(data, len, 83U, &total_cycle_mah) ||
        !read_u32_be(data, len, 87U, &running_time_seconds) ||
        !read_u16_be(data, len, 116U, &max_cell_mv) ||
        !read_u16_be(data, len, 119U, &min_cell_mv) || !read_u16_be(data, len, 121U, &average_cell_mv) ||
        data[74U] > 100U || data[123U] == 0U || data[123U] > ANT_MAX_CELLS ||
        max_cell_mv < min_cell_mv ||
        (int32_t)current_raw < INT16_MIN || (int32_t)current_raw > INT16_MAX) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = (uint32_t)pack_voltage_dv * 100U;
    /* 有意保留原始符号、不做取反：Motexa 的 k.s() 用 l(bArr,70)/(-10.0d) 把方向翻过来，
     * 但本产品的用户反馈「按原始符号显示（充电为负）更直观」，这是产品要求，
     * 不是协议错误。交叉验证时不要按参照实现「修正」成 +107。 */
    telemetry->current_deci_amps = (int16_t)(int32_t)current_raw;
    telemetry->soc_percent = data[74U];
    telemetry->max_cell_voltage_mv = max_cell_mv;
    telemetry->min_cell_voltage_mv = min_cell_mv;
    telemetry->delta_cell_voltage_mv = (uint16_t)(max_cell_mv - min_cell_mv);
    telemetry->average_cell_voltage_mv = average_cell_mv;
    telemetry->total_capacity_mah = total_capacity_uah / 1000U;
    telemetry->capacity_remaining_mah = capacity_remaining_uah / 1000U;
    telemetry->total_cycle_mah = total_cycle_mah;
    telemetry->total_cycle_valid = true;
    telemetry->running_time_seconds = running_time_seconds;
    telemetry->running_time_valid = true;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT; ++index) {
        uint16_t temperature = 0U;
        if (!read_u16_be(data, len, 91U + (size_t)index * 2U, &temperature)) {
            return false;
        }
        telemetry->temperatures_celsius[index] = (int16_t)temperature;
        telemetry->temperature_valid[index] = true;
    }
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

static void stream_drop(uint8_t *stream, size_t *stream_len, size_t count)
{
    *stream_len -= count;
    memmove(stream, stream + count, *stream_len);
}

static bool stream_decode(uint8_t *stream,
                           size_t *stream_len,
                           esp_bms_bms_telemetry_t *telemetry,
                           bool *device_info)
{
    bool have_status = false;
    while (*stream_len > 0U) {
        const size_t length = frame_length(stream, *stream_len);
        if (length == 0U) {
            /* A corrupt length must not hide a later complete, CRC-valid frame. */
            size_t next = 1U;
            for (; next < *stream_len; ++next) {
                if (stream[next] == ANT_FRAME_START_1) {
                    const size_t candidate = frame_length(stream + next, *stream_len - next);
                    if (candidate != 0U && candidate != SIZE_MAX) {
                        break;
                    }
                }
            }
            if (next == *stream_len) {
                break;
            }
            stream_drop(stream, stream_len, next);
            continue;
        }
        if (length == SIZE_MAX) {
            stream_drop(stream, stream_len, 1U);
            continue;
        }
        bool info = false;
        esp_bms_bms_telemetry_t candidate = { 0 };
        if (esp_bms_ant_protocol_decode(stream, length, &candidate, &info)) {
            if (info) {
                if (device_info) {
                    *device_info = true;
                }
            } else {
                *telemetry = candidate;
                have_status = true;
            }
        }
        stream_drop(stream, stream_len, length);
    }
    return have_status;
}

bool esp_bms_ant_protocol_feed(uint8_t *stream,
                               size_t *stream_len,
                               size_t stream_capacity,
                               const uint8_t *chunk,
                               size_t chunk_len,
                               esp_bms_bms_telemetry_t *telemetry,
                               bool *device_info)
{
    if (device_info) {
        *device_info = false;
    }
    if (!stream || !stream_len || !telemetry || (!chunk && chunk_len > 0U) ||
        stream_capacity < ANT_FRAME_MIN_LEN || *stream_len > stream_capacity) {
        return false;
    }
    bool have_status = stream_decode(stream, stream_len, telemetry, device_info);
    for (size_t index = 0U; index < chunk_len; ++index) {
        if (*stream_len == stream_capacity) {
            stream_drop(stream, stream_len, 1U);
        }
        stream[(*stream_len)++] = chunk[index];
        have_status = stream_decode(stream, stream_len, telemetry, device_info) || have_status;
    }
    return have_status;
}

bool esp_bms_ant_protocol_old_poll_request(uint8_t out[6])
{
    static const uint8_t request[6] = { 0xDBU, 0xDBU, 0x00U, 0x00U, 0x00U, 0x00U };
    if (!out) {
        return false;
    }
    memcpy(out, request, sizeof(request));
    return true;
}

bool esp_bms_ant_protocol_old_feed(uint8_t *stream,
                                   size_t *stream_len,
                                   size_t stream_capacity,
                                   const uint8_t *chunk,
                                   size_t chunk_len,
                                   esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || chunk_len == 0U || !telemetry) {
        return false;
    }
    if (chunk_len >= 3U && chunk[0] == ANT_OLD_FRAME_START_1 &&
        chunk[1] == ANT_OLD_FRAME_START_2 && chunk[2] == ANT_OLD_FRAME_START_3) {
        *stream_len = 0U;
    } else if (*stream_len == 0U) {
        return false;
    }
    if (*stream_len > stream_capacity || chunk_len > stream_capacity - *stream_len) {
        *stream_len = 0U;
        return false;
    }
    memcpy(&stream[*stream_len], chunk, chunk_len);
    *stream_len += chunk_len;
    if (*stream_len < ESP_BMS_ANT_OLD_FRAME_LEN) {
        return false;
    }
    const bool decoded = decode_old_status(stream, ESP_BMS_ANT_OLD_FRAME_LEN, telemetry);
    const size_t remaining = *stream_len - ESP_BMS_ANT_OLD_FRAME_LEN;
    if (remaining > 0U) {
        memmove(stream, &stream[ESP_BMS_ANT_OLD_FRAME_LEN], remaining);
    }
    *stream_len = remaining;
    return decoded;
}
