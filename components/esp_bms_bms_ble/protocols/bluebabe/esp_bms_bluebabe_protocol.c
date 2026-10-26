#include "esp_bms_bluebabe_protocol.h"

#include <string.h>

/* UUIDs are stored in NimBLE's little-endian byte order. */
const uint8_t esp_bms_bluebabe_service_uuid[ESP_BMS_BLUEBABE_UUID_LEN] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00,
};
const uint8_t esp_bms_bluebabe_notify_uuid[ESP_BMS_BLUEBABE_UUID_LEN] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE1, 0xFF, 0x00, 0x00,
};
const uint8_t esp_bms_bluebabe_write_uuid[ESP_BMS_BLUEBABE_UUID_LEN] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xE2, 0xFF, 0x00, 0x00,
};

/* Table index 1 carries live telemetry, table index 2 carries identity and capacity. */
static const uint8_t s_table_index[2] = { ESP_BMS_BLUEBABE_TABLE_STATE,
                                          ESP_BMS_BLUEBABE_TABLE_INFO };
static const uint8_t s_table_length[2] = { ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN,
                                           ESP_BMS_BLUEBABE_INFO_PAYLOAD_LEN };

static uint8_t s_matching_code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN];
static bool s_matching_code_valid;

static uint16_t read_u16_be(const uint8_t *data, size_t offset)
{
    return (uint16_t)(((uint16_t)data[offset] << 8U) | data[offset + 1U]);
}

static uint32_t read_u32_be(const uint8_t *data, size_t offset)
{
    return ((uint32_t)data[offset] << 24U) | ((uint32_t)data[offset + 1U] << 16U) |
           ((uint32_t)data[offset + 2U] << 8U) | (uint32_t)data[offset + 3U];
}

static int32_t read_s32_be(const uint8_t *data, size_t offset)
{
    return (int32_t)read_u32_be(data, offset);
}

/* The meter uses a reflected CRC16 with the vendor polynomial 0xC328, seed 0xFFFF. */
uint16_t esp_bms_bluebabe_crc16(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    if (!bytes) {
        return 0U;
    }
    for (size_t index = 0U; index < len; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xC328U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

bool esp_bms_bluebabe_matching_code_valid(const uint8_t code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN])
{
    if (!code) {
        return false;
    }
    for (size_t index = 0U; index < ESP_BMS_BLUEBABE_MATCHING_CODE_LEN; ++index) {
        if (code[index] > 9U) {
            return false;
        }
    }
    return true;
}

bool esp_bms_bluebabe_set_matching_code(const uint8_t code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN])
{
    if (!code) {
        memset(s_matching_code, 0, sizeof(s_matching_code));
        s_matching_code_valid = false;
        return true;
    }
    if (!esp_bms_bluebabe_matching_code_valid(code)) {
        return false;
    }
    memcpy(s_matching_code, code, sizeof(s_matching_code));
    s_matching_code_valid = true;
    return true;
}

bool esp_bms_bluebabe_matching_code_configured(void)
{
    return s_matching_code_valid;
}

bool esp_bms_bluebabe_build_request(uint8_t poll_index,
                                    const uint8_t matching_code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN],
                                    uint8_t out[ESP_BMS_BLUEBABE_REQUEST_LEN])
{
    if (!out || poll_index > 1U) {
        return false;
    }
    if (matching_code && !esp_bms_bluebabe_matching_code_valid(matching_code)) {
        return false;
    }
    memset(out, 0, ESP_BMS_BLUEBABE_REQUEST_LEN);
    out[0] = 0xFAU;
    out[1] = 0x50U;
    out[2] = 0x28U;
    out[3] = 0xA0U;
    out[4] = 0x15U;
    /* Length field: six code bytes plus table index, table offset and table length. */
    out[5] = 0x00U;
    out[6] = 0x09U;
    if (matching_code) {
        memcpy(&out[7], matching_code, ESP_BMS_BLUEBABE_MATCHING_CODE_LEN);
    }
    out[13] = s_table_index[poll_index];
    out[14] = 0x00U;
    out[15] = s_table_length[poll_index];
    const uint16_t crc = esp_bms_bluebabe_crc16(out, 16U);
    out[16] = (uint8_t)(crc >> 8U);
    out[17] = (uint8_t)(crc & 0xFFU);
    out[18] = 0xE5U;
    return true;
}

bool esp_bms_bluebabe_poll_request(uint8_t poll_index, uint8_t out[ESP_BMS_BLUEBABE_REQUEST_LEN])
{
    if (!out || !s_matching_code_valid) {
        return false;
    }
    return esp_bms_bluebabe_build_request(poll_index, s_matching_code, out);
}

static bool decode_state_table(const uint8_t *payload,
                               size_t payload_len,
                               esp_bms_bms_telemetry_t *telemetry)
{
    if (payload_len != ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    /* 0..1 pack voltage, 0.01 V per count. */
    telemetry->pack_voltage_mv = (uint32_t)read_u16_be(payload, 0U) * 10U;
    /* 2..5 pack current, 0.01 A per count, two's complement. */
    telemetry->current_deci_amps = (int16_t)(read_s32_be(payload, 2U) / 10);
    /* 6..7 state of charge, 0.1 percent per count. */
    telemetry->soc_percent = (uint16_t)(read_u16_be(payload, 6U) / 10U);
    /* 15..18 remaining capacity, 0.00001 Ah per count. */
    telemetry->capacity_remaining_mah = read_u32_be(payload, 15U) / 100U;
    /* 82/84/86 highest, lowest and delta cell voltage in millivolts. */
    telemetry->max_cell_voltage_mv = read_u16_be(payload, 82U);
    telemetry->min_cell_voltage_mv = read_u16_be(payload, 84U);
    telemetry->delta_cell_voltage_mv = read_u16_be(payload, 86U);
    /* 14 meter temperature, one signed degree Celsius byte. */
    telemetry->temperatures_celsius[0] = (int16_t)(int8_t)payload[14];
    telemetry->temperature_valid[0] = true;
    if (telemetry->max_cell_voltage_mv == 0U || telemetry->pack_voltage_mv == 0U) {
        return false;
    }
    /* The state table has no pack average, balancer or mask fields. */
    telemetry->balancing_supported = false;
    telemetry->balancing_active = false;
    telemetry->partial = true;
    return true;
}

static bool decode_info_table(const uint8_t *payload,
                             size_t payload_len,
                             esp_bms_bms_telemetry_t *telemetry)
{
    if (payload_len != ESP_BMS_BLUEBABE_INFO_PAYLOAD_LEN) {
        return false;
    }
    memset(telemetry, 0, sizeof(*telemetry));
    /* 48..51 design capacity, 0.001 Ah per count, which is one mAh per count. */
    telemetry->total_capacity_mah = read_u32_be(payload, 48U);
    /* 16..23 hardware version and 24..31 software version are ASCII identity strings
       that the telemetry contract does not carry. */
    telemetry->partial = true;
    return telemetry->total_capacity_mah != 0U;
}

static bool decode_frame(const uint8_t *frame, size_t frame_len, esp_bms_bms_telemetry_t *telemetry)
{
    if (!frame || !telemetry || frame_len < 17U) {
        return false;
    }
    if (frame[0] != 0xFAU || frame[1] != 0x50U || frame[2] != 0xA0U || frame[3] != 0x28U ||
        frame[4] != 0x15U || frame[frame_len - 1U] != 0xE5U) {
        return false;
    }
    const uint16_t remote_crc =
        (uint16_t)(((uint16_t)frame[frame_len - 3U] << 8U) | frame[frame_len - 2U]);
    if (esp_bms_bluebabe_crc16(frame, frame_len - 3U) != remote_crc) {
        return false;
    }
    if (s_matching_code_valid &&
        memcmp(&frame[7], s_matching_code, ESP_BMS_BLUEBABE_MATCHING_CODE_LEN) != 0) {
        return false;
    }
    const size_t length_field = read_u16_be(frame, 5U);
    /* A length field of 7 is a device error frame carrying its code in byte 13. */
    if (length_field == 7U) {
        return false;
    }
    if (frame[14] != 0U || length_field != (size_t)frame[15] + 9U) {
        return false;
    }
    /* Payload sits between the 16 byte header and the three byte CRC/trailer. */
    const uint8_t *payload = &frame[16];
    const size_t payload_len = frame_len - 19U;
    if (frame[13] == ESP_BMS_BLUEBABE_TABLE_STATE) {
        return decode_state_table(payload, payload_len, telemetry);
    }
    if (frame[13] == ESP_BMS_BLUEBABE_TABLE_INFO) {
        return decode_info_table(payload, payload_len, telemetry);
    }
    return false;
}

bool esp_bms_bluebabe_feed(uint8_t *stream,
                           size_t *stream_len,
                           size_t stream_capacity,
                           const uint8_t *chunk,
                           size_t chunk_len,
                           esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || !telemetry || chunk_len == 0U) {
        return false;
    }
    if (stream_capacity < ESP_BMS_BLUEBABE_REQUEST_LEN) {
        return false;
    }
    /* Append the chunk, dropping the oldest bytes rather than overflowing. */
    if (chunk_len >= stream_capacity) {
        chunk += chunk_len - stream_capacity;
        chunk_len = stream_capacity;
        *stream_len = 0U;
    } else if (*stream_len > stream_capacity - chunk_len) {
        const size_t drop = *stream_len - (stream_capacity - chunk_len);
        memmove(stream, &stream[drop], *stream_len - drop);
        *stream_len -= drop;
    }
    memcpy(&stream[*stream_len], chunk, chunk_len);
    *stream_len += chunk_len;

    while (*stream_len >= 2U) {
        if (stream[0] != 0xFAU || stream[1] != 0x50U) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        if (*stream_len < 7U) {
            break;
        }
        const size_t length_field = read_u16_be(stream, 5U);
        if (length_field < 7U || length_field > 1024U) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        const size_t frame_len = length_field + 10U;
        if (frame_len > stream_capacity) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        if (*stream_len < frame_len) {
            break;
        }
        const bool decoded = decode_frame(stream, frame_len, telemetry);
        memmove(stream, &stream[frame_len], *stream_len - frame_len);
        *stream_len -= frame_len;
        if (decoded) {
            return true;
        }
    }
    return false;
}
