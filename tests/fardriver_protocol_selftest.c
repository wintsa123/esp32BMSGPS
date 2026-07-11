#include "esp_fardriver_protocol.h"

#include <assert.h>
#include <string.h>

static void finish_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    frame[14] = (uint8_t)(crc >> 8U);
    frame[15] = (uint8_t)crc;
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

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA;
    frame[1] = 47;
    frame[2] = 17;
    frame[3] = 60;
    frame[4] = 0x2C;
    frame[5] = 0x01;
    frame[6] = 0x64;
    frame[7] = 0x00;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(state.controller_speed_params_valid);

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

    frame[15] ^= 1U;
    assert(!esp_fardriver_parse_frame(&extended, frame, sizeof(frame), ESP_FARDRIVER_LAYOUT_EXTENDED));
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame) - 1U, ESP_FARDRIVER_LAYOUT_EXTENDED));
    return 0;
}
