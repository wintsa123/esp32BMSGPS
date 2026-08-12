#include "esp_fardriver_protocol.h"

#include <math.h>
#include <string.h>

/* FFE0 controllers push low-bit CRC frames; Nordic UART controllers answer
 * five-byte read requests with additive compact or high-bit CRC frames. */
static const uint8_t FLASH_READ_ADDR[] = {
    0xE2, 0xE8, 0xEE, 0xE4, 0x06, 0x0C, 0x12,
    0xE2, 0xE8, 0xEE, 0x18, 0x1E, 0x24, 0x2A,
    0xE2, 0xE8, 0xEE, 0x30, 0x5D, 0x63, 0x69,
    0xE2, 0xE8, 0xEE, 0x7C, 0x82, 0x88, 0x8E,
    0xE2, 0xE8, 0xEE, 0x94, 0x9A, 0xA0, 0xA6,
    0xE2, 0xE8, 0xEE, 0xAC, 0xB2, 0xB8, 0xBE,
    0xE2, 0xE8, 0xEE, 0xC4, 0xCA, 0xD0,
    0xE2, 0xE8, 0xEE, 0xD6, 0xDC, 0xF4, 0xFA,
};

static const uint8_t NUS_FLASH_READ_ADDR[] = {
    0xE2, 0xE8, 0xEE, 0x00, 0x06, 0x0C, 0x12,
    0xE2, 0xE8, 0xEE, 0x18, 0x1E, 0x24, 0x2A,
    0xE2, 0xE8, 0xEE, 0x30, 0x5D, 0x63, 0x69,
    0xE2, 0xE8, 0xEE, 0x7C, 0x82, 0x88, 0x8E,
    0xE2, 0xE8, 0xEE, 0x94, 0x9A, 0xA0, 0xA6,
    0xE2, 0xE8, 0xEE, 0xAC, 0xB2, 0xB8, 0xBE,
    0xE2, 0xE8, 0xEE, 0xC4, 0xCA, 0xD0,
    0xE2, 0xE8, 0xEE, 0xD6, 0xDC, 0xF4, 0xFA,
};

static const uint8_t POLL_READ_ADDR[] = {
    0xE2, 0xE8, 0xEE, 0xF4, 0xFA, 0xD6, 0x24, 0x2A, 0x30, 0x18, 0x69, 0x7C, 0xD0,
    0xA0, 0xA6, 0x63, 0x69, 0x12, 0xD0, 0x24, 0x18, 0x1E, 0x2A, 0x30, 0xBE, 0xC4,
    0x06, 0x0C, 0x9A, 0x94, 0x7C, 0xF4, 0x88, 0x8E, 0x00, 0x82, 0xB8, 0xCA, 0x22,
    0xAC,
};

static uint16_t crc_table_entry(uint8_t index)
{
    uint16_t crc = index;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
        crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
    }
    return crc;
}

uint16_t esp_fardriver_crc(const uint8_t *data, size_t len)
{
    uint8_t a = 0x3CU;
    uint8_t b = 0x7FU;
    if (!data) {
        return 0U;
    }
    for (size_t pos = 0; pos < len; ++pos) {
        const uint16_t entry = crc_table_entry((uint8_t)(a ^ data[pos]));
        a = (uint8_t)(b ^ (uint8_t)entry);
        b = (uint8_t)(entry >> 8U);
    }
    return (uint16_t)(((uint16_t)a << 8U) | b);
}

size_t esp_fardriver_poll_address_count(void)
{
    return sizeof(POLL_READ_ADDR);
}

bool esp_fardriver_poll_address(size_t poll_index, uint8_t *address)
{
    if (!address || poll_index >= sizeof(POLL_READ_ADDR)) {
        return false;
    }
    *address = POLL_READ_ADDR[poll_index];
    return true;
}

bool esp_fardriver_build_read_request(uint8_t address,
                                      uint8_t out[ESP_FARDRIVER_READ_REQUEST_LEN])
{
    if (!out) {
        return false;
    }
    out[0] = address;
    out[1] = address;
    out[2] = 0x80U;
    const uint16_t crc = esp_fardriver_crc(out, 3U);
    out[3] = (uint8_t)(crc >> 8U);
    out[4] = (uint8_t)crc;
    return true;
}

static bool build_command(uint8_t cmd, uint8_t sub, uint8_t v1, uint8_t v2,
                          uint8_t out[ESP_FARDRIVER_COMMAND_LEN])
{
    if (!out) {
        return false;
    }
    out[0] = 0xAAU;
    out[1] = cmd;
    out[2] = (uint8_t)~cmd;
    out[3] = sub;
    out[4] = v1;
    out[5] = v2;
    const uint8_t sum = (uint8_t)((uint8_t)(out[0] + out[1] + out[2] + out[3] + out[4]) + out[5]);
    out[6] = sum;
    out[7] = (uint8_t)~sum;
    return true;
}

bool esp_fardriver_build_open_command(uint8_t out[ESP_FARDRIVER_COMMAND_LEN])
{
    /* AA 13 EC 07 01 F1 A2 5D: 开启数据流，控制器开始推送遥测帧 */
    return build_command(0x13U, 0x07U, 0x01U, 0xF1U, out);
}

bool esp_fardriver_build_keepalive_command(uint8_t out[ESP_FARDRIVER_COMMAND_LEN])
{
    /* AA 13 EC 07 5F 5F 6E 91: 保活心跳 */
    return build_command(0x13U, 0x07U, 0x5FU, 0x5FU, out);
}

static uint16_t be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint16_t le16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[1] << 8U) | data[0]);
}

static void block_valid_set(esp_fardriver_state_t *state, uint8_t address)
{
    state->block_valid[address >> 3U] |= (uint8_t)(1U << (address & 7U));
}

static bool block_valid(const esp_fardriver_state_t *state, uint8_t address)
{
    return (state->block_valid[address >> 3U] & (uint8_t)(1U << (address & 7U))) != 0U;
}

bool esp_fardriver_tire_circumference_mm(uint8_t rim_inch,
                                         uint8_t aspect_percent,
                                         uint16_t width_mm,
                                         uint16_t *circumference_mm)
{
    if (!circumference_mm || rim_inch == 0U || aspect_percent == 0U || width_mm == 0U) {
        return false;
    }
    const float diameter_mm = (float)rim_inch * 25.4f +
                              2.0f * (float)width_mm * (float)aspect_percent / 100.0f;
    const long rounded_mm = lroundf(diameter_mm * 3.14159265f);
    if (rounded_mm <= 0L || rounded_mm > UINT16_MAX) {
        return false;
    }
    *circumference_mm = (uint16_t)rounded_mm;
    return true;
}

static void store_extended_block(esp_fardriver_state_t *state, uint8_t base, const uint8_t *data)
{
    for (uint8_t offset = 0; offset < 6U; ++offset) {
        const uint8_t address = (uint8_t)(base + offset);
        state->blocks[address][0] = data[offset * 2U];
        state->blocks[address][1] = data[offset * 2U + 1U];
        block_valid_set(state, address);
    }
}

static void parse_compact(esp_fardriver_state_t *state, uint8_t index, const uint8_t *data)
{
    if (index == 0U) {
        state->rpm = be16(data + 4U);
        state->rpm_valid = true;
        state->gear = (uint8_t)(data[2] & 0x03U);
        state->gear_valid = true;
    } else if (index == 1U) {
        state->voltage_deci_v = be16(data);
        state->current_centi_a = (int32_t)(int16_t)be16(data + 2U) * 25;
        state->current_valid = true;
    } else if (index == 4U) {
        state->controller_temp_c = (int8_t)data[2];
        state->controller_temp_valid = true;
    } else if (index == 13U) {
        state->motor_temp_c = (int8_t)data[0];
        state->motor_temp_valid = true;
    }
}

static void parse_live_telemetry(esp_fardriver_state_t *state, uint8_t id, const uint8_t *data)
{
    if (id == 0U) {
        /* RPM/档位/故障/相电流帧 */
        const uint16_t raw_rpm = be16(data + 4U);
        state->rpm = raw_rpm;
        state->rpm_valid = true;
        /* 档位位于 fault_byte4 的 bit2-3（bit0-1 为霍尔/油门故障位） */
        state->gear = (uint8_t)((data[2] >> 2U) & 0x03U);
        state->gear_valid = true;
        /* 相电流 iq/id（单位 0.01A），线电流为两者合成 */
        const int32_t iq = (int16_t)be16(data + 8U);
        const int32_t idq = (int16_t)be16(data + 10U);
        state->current_centi_a = (int32_t)lroundf(sqrtf((float)(iq * iq + idq * idq)));
        state->current_valid = true;
    } else if (id == 1U) {
        /* 电压帧（单位 0.1V） */
        state->voltage_deci_v = be16(data);
    } else if (id == 4U) {
        state->controller_temp_c = (int8_t)data[2];
        state->controller_temp_valid = true;
    } else if (id == 13U) {
        state->motor_temp_c = (int8_t)data[0];
        state->motor_temp_valid = true;
    }
}

void esp_fardriver_refresh_derived(esp_fardriver_state_t *state)
{
    if (!state) {
        return;
    }
    /* 功率 = 电压 × 线电流（电压 0.1V、电流 0.01A → /1000 得 W） */
    state->power_valid = state->voltage_deci_v > 0U && state->current_valid;
    if (state->power_valid) {
        state->power_w = ((int32_t)state->voltage_deci_v * state->current_centi_a) / 1000;
    } else {
        state->power_w = 0;
    }
    state->controller_speed_params_valid = false;
    state->tire_rim_inch = 0U;
    state->tire_aspect_percent = 0U;
    state->tire_width_mm = 0U;
    state->wheel_circumference_mm = 0U;
    state->gear_ratio_centi = 0U;
    if (block_valid(state, 0xD2U) && block_valid(state, 0xD3U) && block_valid(state, 0xD4U)) {
        const uint8_t aspect_percent = state->blocks[0xD2U][0];
        const uint8_t rim_inch = state->blocks[0xD2U][1];
        const uint16_t width_mm = le16(state->blocks[0xD3U]);
        const uint16_t rate_ratio = le16(state->blocks[0xD4U]);
        const uint32_t ratio_centi = ((uint32_t)rate_ratio * 100U + 30U) / 60U;
        uint16_t circumference_mm = 0U;
        if (ratio_centi > 0U && ratio_centi <= UINT16_MAX &&
            esp_fardriver_tire_circumference_mm(rim_inch,
                                                aspect_percent,
                                                width_mm,
                                                &circumference_mm)) {
            state->tire_rim_inch = rim_inch;
            state->tire_aspect_percent = aspect_percent;
            state->tire_width_mm = width_mm;
            state->wheel_circumference_mm = circumference_mm;
            state->gear_ratio_centi = (uint16_t)ratio_centi;
            state->controller_speed_params_valid = true;
        }
    }
    const uint16_t circumference = state->controller_speed_params_valid
                                       ? state->wheel_circumference_mm
                                       : state->fallback_wheel_circumference_mm;
    const uint16_t ratio = state->controller_speed_params_valid
                               ? state->gear_ratio_centi
                               : state->fallback_gear_ratio_centi;
    state->speed_deci_kmh = 0U;
    state->speed_valid = state->rpm_valid && circumference > 0U && ratio > 0U;
    if (state->speed_valid) {
        const uint64_t speed_deci_kmh =
            ((uint64_t)state->rpm * circumference * 60000ULL) /
            ((uint64_t)ratio * 1000000ULL);
        state->speed_valid = speed_deci_kmh <= UINT16_MAX;
        if (state->speed_valid) {
            state->speed_deci_kmh = (uint16_t)speed_deci_kmh;
        }
    }
}

bool esp_fardriver_parse_frame(esp_fardriver_state_t *state,
                               const uint8_t *frame,
                               size_t len)
{
    if (!state || !frame || len != ESP_FARDRIVER_FRAME_LEN || frame[0] != 0xAAU) {
        return false;
    }
    const uint16_t wire_checksum = (uint16_t)(((uint16_t)frame[14] << 8U) | frame[15]);
    if ((frame[1] & 0x80U) != 0U) {
        const uint8_t index = (uint8_t)(frame[1] & 0x7FU);
        if (index >= sizeof(NUS_FLASH_READ_ADDR) ||
            wire_checksum != esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U)) {
            return false;
        }
        store_extended_block(state, NUS_FLASH_READ_ADDR[index], frame + 2U);
    } else {
        uint16_t additive_checksum = 0U;
        for (size_t index = 0U; index < ESP_FARDRIVER_FRAME_LEN - 2U; ++index) {
            additive_checksum = (uint16_t)(additive_checksum + frame[index]);
        }
        if (wire_checksum == additive_checksum) {
            const uint8_t index = (uint8_t)(frame[1] & 0x7FU);
            if (index > 29U) {
                return false;
            }
            parse_compact(state, index, frame + 2U);
        } else {
            const uint8_t id = (uint8_t)(frame[1] & 0x3FU);
            if (id >= sizeof(FLASH_READ_ADDR) ||
                wire_checksum != esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U)) {
                return false;
            }
            store_extended_block(state, FLASH_READ_ADDR[id], frame + 2U);
            parse_live_telemetry(state, id, frame + 2U);
        }
    }
    esp_fardriver_refresh_derived(state);
    return true;
}

bool esp_fardriver_has_instrument_telemetry(const esp_fardriver_state_t *state)
{
    return state && (state->rpm_valid || state->current_valid ||
                     state->controller_temp_valid || state->motor_temp_valid);
}
