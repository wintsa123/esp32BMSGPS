#include "esp_fardriver_protocol.h"

#include <assert.h>
#include <string.h>

static void finish_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    frame[14] = (uint8_t)(crc >> 8U);
    frame[15] = (uint8_t)crc;
}

static void make_extended_speed_params_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN],
                                             uint8_t aspect_percent,
                                             uint8_t rim_inch,
                                             uint16_t width_mm,
                                             uint16_t rate_ratio)
{
    memset(frame, 0, ESP_FARDRIVER_FRAME_LEN);
    frame[0] = 0xAA;
    frame[1] = 47U; /* FLASH_READ_ADDR[47] == 0xD0, so D2/D3/D4 share this frame. */
    frame[2] = 17U; /* D0/D1 deliberately contain plausible legacy values. */
    frame[3] = 60U;
    frame[4] = 0x2CU;
    frame[5] = 0x01U;
    frame[6] = aspect_percent;
    frame[7] = rim_inch;
    frame[8] = (uint8_t)width_mm;
    frame[9] = (uint8_t)(width_mm >> 8U);
    frame[10] = (uint8_t)rate_ratio;
    frame[11] = (uint8_t)(rate_ratio >> 8U);
    finish_frame(frame);
}

int main(void)
{
    esp_fardriver_state_t state = { .fallback_wheel_circumference_mm = 1350,
                                    .fallback_gear_ratio_centi = 400 };
    uint8_t frame[ESP_FARDRIVER_FRAME_LEN] = { 0xAA, 0x01, 0x03, 0x84 };
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_COMPACT));

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 0x00;
    frame[4] = 0x08;
    frame[6] = 0x12;
    frame[7] = 0xC0;
    frame[10] = 0x03;
    frame[11] = 0xE8;
    frame[12] = 0x01;
    frame[13] = 0xF4;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_COMPACT));
    assert(state.rpm == 1200 && state.gear == 2 && state.power_valid && state.speed_valid);
    assert(state.speed_deci_kmh == 243U);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 0x04;
    frame[4] = 72;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_COMPACT));
    assert(state.controller_temp_c == 72);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 0x0D;
    frame[2] = 61;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_COMPACT));
    assert(state.motor_temp_c == 61);

    make_extended_speed_params_frame(frame, 70U, 12U, 90U, 60U);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(state.controller_speed_params_valid);
    assert(state.tire_rim_inch == 12U);
    assert(state.tire_aspect_percent == 70U);
    assert(state.tire_width_mm == 90U);
    assert(state.wheel_circumference_mm == 1353U);
    assert(state.gear_ratio_centi == 100U);
    assert(state.speed_deci_kmh == 974U);

    esp_fardriver_state_t zero_rate = { .rpm = 1200U,
                                        .rpm_valid = true,
                                        .fallback_wheel_circumference_mm = 1350U,
                                        .fallback_gear_ratio_centi = 400U };
    make_extended_speed_params_frame(frame, 70U, 12U, 90U, 0U);
    assert(esp_fardriver_parse_frame(&zero_rate, frame, sizeof(frame),
                                     ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(!zero_rate.controller_speed_params_valid);
    assert(zero_rate.speed_valid && zero_rate.speed_deci_kmh == 243U);

    esp_fardriver_state_t zero_divisor = { .rpm = 1200U,
                                           .rpm_valid = true,
                                           .fallback_wheel_circumference_mm = 1350U };
    esp_fardriver_refresh_derived(&zero_divisor);
    assert(!zero_divisor.speed_valid && zero_divisor.speed_deci_kmh == 0U);

    esp_fardriver_state_t speed_overflow = { .rpm = UINT16_MAX,
                                             .rpm_valid = true,
                                             .fallback_wheel_circumference_mm = 4000U,
                                             .fallback_gear_ratio_centi = 50U };
    esp_fardriver_refresh_derived(&speed_overflow);
    assert(!speed_overflow.speed_valid && speed_overflow.speed_deci_kmh == 0U);

    esp_fardriver_state_t outside_roller = { .rpm = 1200U,
                                             .rpm_valid = true,
                                             .fallback_wheel_circumference_mm = 1350U,
                                             .fallback_gear_ratio_centi = 400U };
    make_extended_speed_params_frame(frame, 110U, 26U, 210U, 60U);
    assert(esp_fardriver_parse_frame(&outside_roller, frame, sizeof(frame),
                                     ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(outside_roller.controller_speed_params_valid);
    assert(outside_roller.tire_rim_inch == 26U);
    assert(outside_roller.tire_aspect_percent == 110U);
    assert(outside_roller.tire_width_mm == 210U);

    esp_fardriver_state_t overflow = { .rpm = 1200U,
                                       .rpm_valid = true,
                                       .fallback_wheel_circumference_mm = 1350U,
                                       .fallback_gear_ratio_centi = 400U };
    make_extended_speed_params_frame(frame, UINT8_MAX, UINT8_MAX, UINT16_MAX, 60U);
    assert(esp_fardriver_parse_frame(&overflow, frame, sizeof(frame),
                                     ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(!overflow.controller_speed_params_valid);
    assert(overflow.speed_deci_kmh == 243U);

    make_extended_speed_params_frame(frame, 70U, 12U, 90U, UINT16_MAX);
    assert(esp_fardriver_parse_frame(&overflow, frame, sizeof(frame),
                                     ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(!overflow.controller_speed_params_valid);
    assert(overflow.speed_deci_kmh == 243U);

    esp_fardriver_state_t extended = { .fallback_wheel_circumference_mm = 1350,
                                       .fallback_gear_ratio_centi = 400 };
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 0;
    frame[2] = 0x08;
    frame[8] = 0xC0;
    frame[9] = 0x12;
    frame[10] = 0xE8;
    frame[11] = 0x03;
    frame[12] = 0xF4;
    frame[13] = 0x01;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&extended, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(extended.rpm == 1200 && extended.gear == 2);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 1;
    frame[2] = 0x84;
    frame[3] = 0x03;
    frame[4] = 52;
    frame[8] = 61;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&extended, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(extended.power_valid && extended.controller_temp_c == 52 && extended.motor_temp_c == 61);

    const esp_fardriver_state_t before_invalid_frame = extended;
    frame[15] ^= 1U;
    assert(!esp_fardriver_parse_frame(&extended, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(memcmp(&extended, &before_invalid_frame, sizeof(extended)) == 0);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame) - 1U, ESP_FARDRIVER_LAYOUT_EXTENDED));
    frame[0] = 0xABU;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 63U;
    finish_frame(frame);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    return 0;
}
