#include "esp_bms_jk_protocol.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static esp_bms_jk_protocol_t s_protocol = ESP_BMS_JK_PROTOCOL_UNKNOWN;

static uint16_t read_u16_le(const uint8_t *data, size_t offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data, size_t offset)
{
    return (uint32_t)read_u16_le(data, offset) | ((uint32_t)read_u16_le(data, offset + 2U) << 16U);
}

static float read_f32_le(const uint8_t *data, size_t offset)
{
    const uint32_t bits = read_u32_le(data, offset);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
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

void esp_bms_jk_reset(void)
{
    s_protocol = ESP_BMS_JK_PROTOCOL_UNKNOWN;
}

esp_bms_jk_protocol_t esp_bms_jk_protocol(void)
{
    return s_protocol;
}

static void copy_fixed_text(char *out, size_t out_len, const uint8_t *data, size_t len)
{
    size_t copied = 0U;
    if (!out || out_len == 0U || !data) {
        return;
    }
    while (copied + 1U < out_len && copied < len && data[copied] != 0U) {
        const uint8_t value = data[copied];
        out[copied] = value >= 0x20U && value <= 0x7EU ? (char)value : '?';
        copied++;
    }
    out[copied] = '\0';
}

bool esp_bms_jk_decode_device_info(const uint8_t *frame,
                                   size_t frame_len,
                                   esp_bms_jk_device_info_t *info)
{
    if (!frame || frame_len < ESP_BMS_JK_FRAME_LEN || !info ||
        frame[0] != 0x55U || frame[1] != 0xAAU || frame[2] != 0xEBU || frame[3] != 0x90U ||
        frame[4] != 0x03U || sum8(frame, ESP_BMS_JK_FRAME_LEN - 1U) != frame[299]) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    copy_fixed_text(info->vendor, sizeof(info->vendor), frame + 6U, 16U);
    copy_fixed_text(info->hardware_version, sizeof(info->hardware_version), frame + 22U, 8U);
    copy_fixed_text(info->software_version, sizeof(info->software_version), frame + 30U, 8U);
    copy_fixed_text(info->device_name, sizeof(info->device_name), frame + 46U, 16U);
    return true;
}

static bool decode_jk04_cell_info(const uint8_t frame[ESP_BMS_JK_FRAME_LEN],
                                  esp_bms_bms_telemetry_t *telemetry)
{
    memset(telemetry, 0, sizeof(*telemetry));
    uint32_t total_mv = 0U;
    uint16_t minimum_mv = UINT16_MAX;
    uint16_t maximum_mv = 0U;
    uint8_t cells = 0U;
    for (uint8_t index = 0U; index < 24U; ++index) {
        const float volts = read_f32_le(frame, 6U + (size_t)index * 4U);
        if (volts == 0.0f) {
            continue;
        }
        if (!isfinite(volts) || !(volts >= 1.0f && volts <= 6.0f)) {
            return false;
        }
        const uint16_t millivolts = (uint16_t)(volts * 1000.0f + 0.5f);
        total_mv += millivolts;
        minimum_mv = millivolts < minimum_mv ? millivolts : minimum_mv;
        maximum_mv = millivolts > maximum_mv ? millivolts : maximum_mv;
        ++cells;
    }
    if (cells == 0U) {
        return false;
    }
    telemetry->pack_voltage_mv = total_mv;
    telemetry->min_cell_voltage_mv = minimum_mv;
    telemetry->max_cell_voltage_mv = maximum_mv;
    telemetry->delta_cell_voltage_mv = maximum_mv - minimum_mv;
    telemetry->average_cell_voltage_mv = (uint16_t)(total_mv / cells);
    telemetry->partial = true;
    return true;
}

static bool decode_jk02_cell_info(const uint8_t frame[ESP_BMS_JK_FRAME_LEN],
                                  uint8_t cell_slots,
                                  esp_bms_bms_telemetry_t *telemetry)
{
    const size_t dynamic = cell_slots == 32U ? 32U : 0U;
    uint32_t total_mv = 0U;
    uint16_t minimum_mv = UINT16_MAX;
    uint16_t maximum_mv = 0U;
    uint8_t cells = 0U;
    for (uint8_t index = 0U; index < cell_slots; ++index) {
        const uint16_t millivolts = read_u16_le(frame, 6U + (size_t)index * 2U);
        if (millivolts == 0U) {
            continue;
        }
        if (millivolts < 1000U || millivolts > 6000U) {
            return false;
        }
        total_mv += millivolts;
        minimum_mv = millivolts < minimum_mv ? millivolts : minimum_mv;
        maximum_mv = millivolts > maximum_mv ? millivolts : maximum_mv;
        ++cells;
    }
    const uint32_t pack_voltage_mv = read_u32_le(frame, 118U + dynamic);
    const uint32_t tolerance_mv = total_mv / 10U + 1000U;
    if (cells == 0U || pack_voltage_mv == 0U ||
        (pack_voltage_mv > total_mv ? pack_voltage_mv - total_mv : total_mv - pack_voltage_mv) > tolerance_mv ||
        frame[141U + dynamic] > 100U) {
        return false;
    }

    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->pack_voltage_mv = pack_voltage_mv;
    telemetry->current_deci_amps = (int16_t)((int32_t)read_u32_le(frame, 126U + dynamic) / 100);
    telemetry->soc_percent = frame[141U + dynamic];
    telemetry->capacity_remaining_mah = read_u32_le(frame, 142U + dynamic);
    telemetry->total_capacity_mah = read_u32_le(frame, 146U + dynamic);
    const uint32_t total_charge_deci_ah = read_u32_le(frame, 154U + dynamic);
    if (total_charge_deci_ah > UINT32_MAX / 100U) {
        return false;
    }
    telemetry->total_cycle_mah = total_charge_deci_ah * 100U;
    telemetry->total_cycle_valid = true;
    telemetry->protection_mask = cell_slots == 32U ? read_u32_le(frame, 166U) : read_u16_le(frame, 136U);
    telemetry->balancing_supported = true;
    telemetry->balancing_active = frame[140U + dynamic] != 0U;
    telemetry->min_cell_voltage_mv = minimum_mv;
    telemetry->max_cell_voltage_mv = maximum_mv;
    telemetry->delta_cell_voltage_mv = maximum_mv - minimum_mv;
    telemetry->average_cell_voltage_mv = (uint16_t)(total_mv / cells);
    if (cell_slots == 32U) {
        telemetry->temperatures_celsius[0] = (int16_t)read_u16_le(frame, 144U) / 10;
        telemetry->temperatures_celsius[1] = (int16_t)read_u16_le(frame, 162U) / 10;
        telemetry->temperatures_celsius[2] = (int16_t)read_u16_le(frame, 164U) / 10;
        telemetry->temperature_valid[0] = true;
        telemetry->temperature_valid[1] = true;
        telemetry->temperature_valid[2] = true;
    } else {
        telemetry->temperatures_celsius[0] = (int16_t)read_u16_le(frame, 130U) / 10;
        telemetry->temperatures_celsius[1] = (int16_t)read_u16_le(frame, 132U) / 10;
        telemetry->temperatures_celsius[2] = (int16_t)read_u16_le(frame, 134U) / 10;
        telemetry->temperature_valid[0] = true;
        telemetry->temperature_valid[1] = true;
        telemetry->temperature_valid[2] = true;
    }
    return true;
}

static bool decode_cell_info(const uint8_t frame[ESP_BMS_JK_FRAME_LEN],
                             esp_bms_bms_telemetry_t *telemetry)
{
    if (!telemetry || frame[4] != 0x02U || sum8(frame, ESP_BMS_JK_FRAME_LEN - 1U) != frame[299]) {
        return false;
    }
    if (s_protocol == ESP_BMS_JK_PROTOCOL_JK04) {
        return decode_jk04_cell_info(frame, telemetry);
    }
    if (s_protocol == ESP_BMS_JK_PROTOCOL_JK02_24S) {
        return decode_jk02_cell_info(frame, 24U, telemetry);
    }
    if (s_protocol == ESP_BMS_JK_PROTOCOL_JK02_32S) {
        return decode_jk02_cell_info(frame, 32U, telemetry);
    }
    esp_bms_bms_telemetry_t candidate = { 0 };
    const bool decoded_jk04 = decode_jk04_cell_info(frame, &candidate);
    const bool decoded_24s = decode_jk02_cell_info(frame, 24U, &candidate);
    esp_bms_bms_telemetry_t candidate_32s = { 0 };
    const bool decoded_32s = decode_jk02_cell_info(frame, 32U, &candidate_32s);
    const unsigned matches = (unsigned)decoded_jk04 + (unsigned)decoded_24s + (unsigned)decoded_32s;
    if (matches != 1U) {
        return false;
    }
    if (decoded_jk04) {
        s_protocol = ESP_BMS_JK_PROTOCOL_JK04;
    } else if (decoded_32s) {
        s_protocol = ESP_BMS_JK_PROTOCOL_JK02_32S;
        *telemetry = candidate_32s;
    } else {
        s_protocol = ESP_BMS_JK_PROTOCOL_JK02_24S;
    }
    if (decoded_jk04 || decoded_24s) {
        *telemetry = candidate;
    }
    return true;
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
        const bool decoded = stream[4] == 0x02U && decode_cell_info(stream, telemetry);
        memmove(stream, stream + ESP_BMS_JK_FRAME_LEN, *stream_len - ESP_BMS_JK_FRAME_LEN);
        *stream_len -= ESP_BMS_JK_FRAME_LEN;
        if (decoded) {
            return true;
        }
    }
    return false;
}
