#include "esp_bms_mingtang_protocol.h"

#include <string.h>

/* UUIDs are stored in NimBLE's little-endian byte order. */
const uint8_t esp_bms_mingtang_service_uuid[ESP_BMS_MINGTANG_UUID_LEN] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0xFA, 0x00, 0x00,
};

#define MINGTANG_FRAME_STATE 0xE1U
#define MINGTANG_FRAME_SETTINGS 0xE2U
#define MINGTANG_FRAME_HISTORY_FIRST 0xF1U
#define MINGTANG_FRAME_HISTORY_LAST 0xF3U
#define MINGTANG_HEADER_LEN 6U
#define MINGTANG_END_MARKER "[END]"
#define MINGTANG_END_MARKER_LEN 5U
/* Vendor special values for tags 27..31, in 0.01 degC. */
#define MINGTANG_TEMP_INVALID_LOW (-27300)
#define MINGTANG_TEMP_INVALID_HIGH 30000

static const char *const s_poll_commands[] = {
    "stateread",
    "settingread",
    "readhis",
    "readevent",
};

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

bool esp_bms_mingtang_poll_request(uint8_t poll_index, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    if (poll_index >= sizeof(s_poll_commands) / sizeof(s_poll_commands[0])) {
        return false;
    }
    const char *command = s_poll_commands[poll_index];
    const size_t command_len = strlen(command);
    if (command_len > ESP_BMS_MINGTANG_REQUEST_MAX_LEN) {
        return false;
    }
    memcpy(out, command, command_len);
    *out_len = command_len;
    return true;
}

/* Live state, settings and the 0xF1..0xF3 history/event payloads share the header and
   length layout; only 0xE1 carries the telemetry tags decoded here. */
static bool is_frame_head(uint8_t value)
{
    return value == MINGTANG_FRAME_STATE || value == MINGTANG_FRAME_SETTINGS ||
           (value >= MINGTANG_FRAME_HISTORY_FIRST && value <= MINGTANG_FRAME_HISTORY_LAST);
}

/* Validates that the TLV stream tiles [6, tlv_end) exactly, as the vendor parser does. */
static bool tlv_tiling_valid(const uint8_t *frame, size_t tlv_end)
{
    size_t position = MINGTANG_HEADER_LEN;
    while (position < tlv_end) {
        if (position + 3U > tlv_end) {
            return false;
        }
        const size_t value_len = frame[position + 2U];
        position += 3U;
        if (position + value_len > tlv_end) {
            return false;
        }
        position += value_len;
    }
    return position == tlv_end;
}

static void record_cell(const uint8_t *value,
                        size_t value_len,
                        bool *any,
                        uint32_t *sum,
                        uint16_t *count,
                        uint16_t *minimum,
                        uint16_t *maximum)
{
    for (size_t offset = 0U; offset + 1U < value_len; offset += 2U) {
        const uint16_t millivolts = read_u16_be(value, offset);
        if (millivolts == 0U) {
            continue;
        }
        *any = true;
        *sum += millivolts;
        ++*count;
        *minimum = millivolts < *minimum ? millivolts : *minimum;
        *maximum = millivolts > *maximum ? millivolts : *maximum;
    }
}

static void record_temperature(esp_bms_bms_telemetry_t *telemetry,
                               uint8_t slot,
                               const uint8_t *value,
                               size_t value_len)
{
    if (slot >= ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT || value_len < 2U) {
        return;
    }
    const int16_t centi = (int16_t)read_u16_be(value, 0U);
    if (centi == MINGTANG_TEMP_INVALID_LOW || centi == MINGTANG_TEMP_INVALID_HIGH) {
        return;
    }
    telemetry->temperatures_celsius[slot] = (int16_t)(centi / 100);
    telemetry->temperature_valid[slot] = true;
}

static bool decode_state_frame(const uint8_t *frame, size_t tlv_end, esp_bms_bms_telemetry_t *telemetry)
{
    memset(telemetry, 0, sizeof(*telemetry));

    bool have_voltage = false;
    bool have_current = false;
    bool have_soc = false;
    int32_t charge_ma = 0;
    int32_t discharge_ma = 0;
    bool have_capacity = false;
    bool have_cell_array = false;
    uint32_t cell_sum = 0U;
    uint16_t cell_count = 0U;
    uint16_t cell_minimum = UINT16_MAX;
    uint16_t cell_maximum = 0U;
    bool have_cell_maximum = false;
    bool have_cell_minimum = false;
    bool have_cell_delta = false;
    bool have_balancer_state = false;
    bool have_balancer_info = false;
    uint32_t balancer_info = 0U;

    size_t position = MINGTANG_HEADER_LEN;
    while (position + 3U <= tlv_end) {
        const uint16_t tag = read_u16_be(frame, position);
        const size_t value_len = frame[position + 2U];
        const uint8_t *value = &frame[position + 3U];
        position += 3U + value_len;

        switch (tag) {
        case 4U:
            record_cell(value, value_len, &have_cell_array, &cell_sum, &cell_count,
                        &cell_minimum, &cell_maximum);
            break;
        case 5U:
            if (value_len >= 2U) {
                cell_maximum = read_u16_be(value, 0U);
                have_cell_maximum = true;
            }
            break;
        case 6U:
            if (value_len >= 2U) {
                cell_minimum = read_u16_be(value, 0U);
                have_cell_minimum = true;
            }
            break;
        case 7U:
            if (value_len >= 2U) {
                telemetry->delta_cell_voltage_mv = read_u16_be(value, 0U);
                have_cell_delta = true;
            }
            break;
        case 10U:
            if (value_len >= 2U) {
                /* 0.001 V per count. */
                telemetry->pack_voltage_mv = (uint32_t)read_u16_be(value, 0U) * 10U;
                have_voltage = telemetry->pack_voltage_mv != 0U;
            }
            break;
        case 13U:
            if (value_len >= 4U) {
                charge_ma = read_s32_be(value, 0U);
                have_current = true;
            }
            break;
        case 15U:
            if (value_len >= 4U) {
                discharge_ma = read_s32_be(value, 0U);
                have_current = true;
            }
            break;
        case 17U:
            if (value_len >= 1U && value[0] != 0U) {
                telemetry->protection_mask |= UINT64_C(1) << 0U;
            }
            break;
        case 19U:
            if (value_len >= 1U && value[0] != 0U) {
                telemetry->protection_mask |= UINT64_C(1) << 1U;
            }
            break;
        case 21U:
            if (value_len >= 1U && value[0] != 0U) {
                telemetry->protection_mask |= UINT64_C(1) << 2U;
            }
            break;
        case 24U:
            if (value_len >= 1U) {
                telemetry->balancing_active = value[0] == 1U;
                have_balancer_state = true;
            }
            break;
        case 25U:
            if (value_len >= 4U) {
                balancer_info = read_u32_be(value, 0U);
                have_balancer_info = true;
                if (balancer_info != 0U) {
                    telemetry->balancing_active = true;
                }
            }
            break;
        case 27U:
        case 28U:
        case 29U:
        case 30U:
        case 31U:
            record_temperature(telemetry, (uint8_t)(tag - 27U), value, value_len);
            break;
        case 36U:
            if (value_len >= 4U) {
                /* 0.001 Ah per count, which is one mAh per count. */
                const int32_t capacity = read_s32_be(value, 0U);
                if (capacity > 0) {
                    telemetry->total_capacity_mah = (uint32_t)capacity;
                    have_capacity = true;
                }
            }
            break;
        case 37U:
            if (value_len >= 4U) {
                const int32_t capacity = read_s32_be(value, 0U);
                if (capacity >= 0) {
                    telemetry->capacity_remaining_mah = (uint32_t)capacity;
                    have_capacity = true;
                }
            }
            break;
        case 38U:
            if (value_len >= 1U) {
                telemetry->soc_percent = value[0];
                have_soc = true;
            }
            break;
        case 170U:
            if (value_len >= 2U) {
                uint32_t mask = 0U;
                for (size_t offset = 0U; offset + 1U < value_len && offset < 4U; offset += 2U) {
                    mask = (mask << 16U) | read_u16_be(value, offset);
                }
                telemetry->warning_mask = mask;
            }
            break;
        default:
            break;
        }
    }

    if (have_current) {
        const int32_t milli_amps = charge_ma - discharge_ma;
        int32_t deci_amps = milli_amps / 100;
        if (deci_amps > INT16_MAX) {
            deci_amps = INT16_MAX;
        } else if (deci_amps < INT16_MIN) {
            deci_amps = INT16_MIN;
        }
        telemetry->current_deci_amps = (int16_t)deci_amps;
    }

    if (have_cell_maximum) {
        telemetry->max_cell_voltage_mv = cell_maximum;
    }
    if (have_cell_minimum) {
        telemetry->min_cell_voltage_mv = cell_minimum;
    }
    if (have_cell_array && cell_count > 0U) {
        if (!have_cell_maximum) {
            telemetry->max_cell_voltage_mv = cell_maximum;
        }
        if (!have_cell_minimum) {
            telemetry->min_cell_voltage_mv = cell_minimum;
        }
        telemetry->average_cell_voltage_mv = (uint16_t)(cell_sum / cell_count);
    }
    if (!have_cell_delta && telemetry->max_cell_voltage_mv >= telemetry->min_cell_voltage_mv &&
        telemetry->max_cell_voltage_mv != 0U) {
        telemetry->delta_cell_voltage_mv =
            (uint16_t)(telemetry->max_cell_voltage_mv - telemetry->min_cell_voltage_mv);
    }

    telemetry->balancing_supported = have_balancer_state || have_balancer_info;
    if (!have_voltage || !have_soc) {
        return false;
    }
    /* Tags 3/4/10/36/37 are required for a complete snapshot. */
    telemetry->partial = !have_capacity || !have_cell_array;
    return true;
}

bool esp_bms_mingtang_feed(uint8_t *stream,
                           size_t *stream_len,
                           size_t stream_capacity,
                           const uint8_t *chunk,
                           size_t chunk_len,
                           esp_bms_bms_telemetry_t *telemetry)
{
    if (!stream || !stream_len || !chunk || !telemetry || chunk_len == 0U) {
        return false;
    }
    if (stream_capacity < (MINGTANG_HEADER_LEN + 2U)) {
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

    while (*stream_len > 0U) {
        if (!is_frame_head(stream[0])) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        if (*stream_len < MINGTANG_HEADER_LEN + 1U) {
            break;
        }
        if (stream[1] != 0x11U || stream[2] != 0x00U) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        const size_t last_index = read_u16_be(stream, 4U);
        if (last_index < MINGTANG_HEADER_LEN) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        const size_t frame_len = last_index + 1U;
        if (frame_len > stream_capacity) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }
        if (*stream_len < frame_len) {
            break;
        }
        if (!tlv_tiling_valid(stream, last_index)) {
            memmove(stream, &stream[1], *stream_len - 1U);
            --*stream_len;
            continue;
        }

        const bool decoded = stream[0] == MINGTANG_FRAME_STATE &&
                             decode_state_frame(stream, last_index, telemetry);
        memmove(stream, &stream[frame_len], *stream_len - frame_len);
        *stream_len -= frame_len;
        /* Settings frames are closed by the ASCII "[END]" marker. */
        if (*stream_len >= MINGTANG_END_MARKER_LEN &&
            memcmp(stream, MINGTANG_END_MARKER, MINGTANG_END_MARKER_LEN) == 0) {
            memmove(stream, &stream[MINGTANG_END_MARKER_LEN],
                    *stream_len - MINGTANG_END_MARKER_LEN);
            *stream_len -= MINGTANG_END_MARKER_LEN;
        }
        if (decoded) {
            return true;
        }
    }
    return false;
}
