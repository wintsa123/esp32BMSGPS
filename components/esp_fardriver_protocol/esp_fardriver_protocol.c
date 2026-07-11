#include "esp_fardriver_protocol.h"

#include <math.h>
#include <string.h>

static const uint8_t FLASH_READ_ADDR[] = {
    0xE2, 0xE8, 0xEE, 0x00, 0x06, 0x0C, 0x12,
    0xE2, 0xE8, 0xEE, 0x18, 0x1E, 0x24, 0x2A,
    0xE2, 0xE8, 0xEE, 0x30, 0x5D, 0x63, 0x69,
    0xE2, 0xE8, 0xEE, 0x7C, 0x82, 0x88, 0x8E,
    0xE2, 0xE8, 0xEE, 0x94, 0x9A, 0xA0, 0xA6,
    0xE2, 0xE8, 0xEE, 0xAC, 0xB2, 0xB8, 0xBE,
    0xE2, 0xE8, 0xEE, 0xC4, 0xCA, 0xD0,
    0xE2, 0xE8, 0xEE, 0xD6, 0xDC, 0xF4, 0xFA,
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
        const uint16_t raw_rpm = be16(data + 4U);
        const int16_t iq_centi_a = (int16_t)be16(data + 8U);
        const int16_t id_centi_a = (int16_t)be16(data + 10U);
        const float amps = sqrtf((float)iq_centi_a * iq_centi_a + (float)id_centi_a * id_centi_a) / 100.0f;
        state->rpm = raw_rpm / 4U;
        state->rpm_valid = true;
        state->gear = (uint8_t)((data[2] >> 2U) & 0x03U);
        state->gear = state->gear == 0U ? 3U : state->gear;
        state->gear_valid = true;
        if (state->voltage_deci_v > 0U) {
            state->power_w = (int32_t)lroundf(amps * ((float)state->voltage_deci_v / 10.0f));
            if (iq_centi_a < 0 || id_centi_a < 0) {
                state->power_w = -state->power_w;
            }
            state->power_valid = true;
        }
    } else if (index == 1U) {
        state->voltage_deci_v = be16(data);
    } else if (index == 4U) {
        state->controller_temp_c = (int8_t)data[2];
        state->controller_temp_valid = true;
    } else if (index == 13U) {
        state->motor_temp_c = (int8_t)data[0];
        state->motor_temp_valid = true;
    }
}

void esp_fardriver_refresh_derived(esp_fardriver_state_t *state)
{
    if (!state) {
        return;
    }
    if (block_valid(state, 0xE5U)) {
        state->rpm = le16(state->blocks[0xE5U]) / 4U;
        state->rpm_valid = true;
    }
    if (block_valid(state, 0xE2U)) {
        const uint8_t raw = state->blocks[0xE2U][0];
        state->gear = (uint8_t)((raw >> 2U) & 0x03U);
        state->gear = state->gear == 0U ? 3U : state->gear;
        state->gear_valid = true;
    }
    if (block_valid(state, 0xE8U)) {
        state->voltage_deci_v = le16(state->blocks[0xE8U]);
    }
    if (block_valid(state, 0xE9U)) {
        state->controller_temp_c = (int8_t)state->blocks[0xE9U][0];
        state->controller_temp_valid = true;
    }
    if (block_valid(state, 0xEBU)) {
        state->motor_temp_c = (int8_t)state->blocks[0xEBU][0];
        state->motor_temp_valid = true;
    }
    if (block_valid(state, 0xE6U) && block_valid(state, 0xE7U) && state->voltage_deci_v > 0U) {
        const int16_t iq = (int16_t)le16(state->blocks[0xE6U]);
        const int16_t id = (int16_t)le16(state->blocks[0xE7U]);
        const float amps = sqrtf((float)iq * iq + (float)id * id) / 100.0f;
        state->power_w = (int32_t)lroundf(amps * ((float)state->voltage_deci_v / 10.0f));
        if (iq < 0 || id < 0) {
            state->power_w = -state->power_w;
        }
        state->power_valid = true;
    }
    if (block_valid(state, 0xD0U) && block_valid(state, 0xD1U) && block_valid(state, 0xD2U)) {
        const uint8_t radius = state->blocks[0xD0U][0];
        const uint8_t width = state->blocks[0xD0U][1];
        const uint16_t wheel_ratio = le16(state->blocks[0xD1U]);
        const uint16_t rate_ratio = le16(state->blocks[0xD2U]);
        if (rate_ratio > 0U) {
            state->wheel_circumference_mm =
                (uint16_t)lroundf(3.76991136f * (radius * 1270.0f + width * wheel_ratio) / rate_ratio);
            state->gear_ratio_centi = 100U;
            state->controller_speed_params_valid = state->wheel_circumference_mm > 0U;
        }
    }
    const uint16_t circumference = state->controller_speed_params_valid
                                       ? state->wheel_circumference_mm
                                       : state->fallback_wheel_circumference_mm;
    const uint16_t ratio = state->controller_speed_params_valid
                               ? state->gear_ratio_centi
                               : state->fallback_gear_ratio_centi;
    state->speed_valid = state->rpm_valid && circumference > 0U && ratio > 0U;
    if (state->speed_valid) {
        state->speed_deci_kmh = (uint16_t)(((uint64_t)state->rpm * circumference * 6000ULL) /
                                          ((uint64_t)ratio * 1000000ULL));
    }
}

bool esp_fardriver_parse_frame(esp_fardriver_state_t *state,
                               const uint8_t *frame,
                               size_t len,
                               esp_fardriver_layout_t layout)
{
    if (!state || !frame || len != ESP_FARDRIVER_FRAME_LEN || frame[0] != 0xAAU) {
        return false;
    }
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    if (frame[14] != (uint8_t)(crc >> 8U) || frame[15] != (uint8_t)crc) {
        return false;
    }
    const uint8_t index = (uint8_t)(frame[1] & 0x3FU);
    if (layout == ESP_FARDRIVER_LAYOUT_COMPACT) {
        if (index > 29U) {
            return false;
        }
        parse_compact(state, index, frame + 2U);
    } else {
        if (index >= sizeof(FLASH_READ_ADDR)) {
            return false;
        }
        store_extended_block(state, FLASH_READ_ADDR[index], frame + 2U);
    }
    esp_fardriver_refresh_derived(state);
    return true;
}

