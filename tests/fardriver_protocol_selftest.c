#include "esp_fardriver_protocol.h"

#include <assert.h>
#include <string.h>

static void finish_compact_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    uint16_t checksum = 0U;
    for (size_t index = 0U; index < ESP_FARDRIVER_FRAME_LEN - 2U; ++index) {
        checksum = (uint16_t)(checksum + frame[index]);
    }
    frame[14] = (uint8_t)(checksum >> 8U);
    frame[15] = (uint8_t)checksum;
}

static void finish_extended_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    frame[14] = (uint8_t)(crc >> 8U);
    frame[15] = (uint8_t)crc;
}

static void make_extended_speed_params_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    memset(frame, 0, ESP_FARDRIVER_FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = 0x80U | 47U; /* FLASH_READ_ADDR[47] is D0, which includes D2-D4. */
    frame[6] = 70U;
    frame[7] = 12U;
    frame[8] = 90U;
    frame[9] = 0U;
    frame[10] = 60U;
    frame[11] = 0U;
    finish_extended_frame(frame);
}

int main(void)
{
    esp_fardriver_state_t state = { .fallback_wheel_circumference_mm = 1350U,
                                    .fallback_gear_ratio_centi = 400U };
    uint8_t frame[ESP_FARDRIVER_FRAME_LEN] = { 0 };

    frame[0] = 0xAAU;
    frame[1] = 0U;
    frame[4] = 0x08U;
    frame[6] = 0x12U;
    frame[7] = 0xC0U;
    finish_compact_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.rpm_valid && state.rpm == 4800U);
    assert(state.gear_valid && state.gear == 2U);
    assert(state.speed_valid && state.speed_deci_kmh == 972U);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 1U;
    frame[2] = 0x03U;
    frame[3] = 0x84U;
    frame[4] = 0xFFU;
    frame[5] = 0xF8U;
    finish_compact_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.voltage_deci_v == 900U && state.power_valid && state.power_w == -180);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 4U;
    frame[4] = 72U;
    finish_compact_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_temp_valid && state.controller_temp_c == 72);

    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 13U;
    frame[2] = 61U;
    finish_compact_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.motor_temp_valid && state.motor_temp_c == 61);

    make_extended_speed_params_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_speed_params_valid);
    assert(state.tire_aspect_percent == 70U && state.tire_rim_inch == 12U &&
           state.tire_width_mm == 90U && state.wheel_circumference_mm == 1353U &&
           state.gear_ratio_centi == 100U && state.speed_deci_kmh == 3896U);

    uint8_t request[ESP_FARDRIVER_READ_REQUEST_LEN] = { 0 };
    assert(esp_fardriver_poll_address_count() == 40U);
    assert(esp_fardriver_poll_address(0U, &request[0]) && request[0] == 0xE2U);
    assert(esp_fardriver_poll_address(12U, &request[0]) && request[0] == 0xD0U);
    assert(esp_fardriver_poll_address(13U, &request[0]) && request[0] == 0xA0U);
    assert(esp_fardriver_poll_address(39U, &request[0]) && request[0] == 0xACU);
    assert(!esp_fardriver_poll_address(40U, &request[0]));
    assert(esp_fardriver_build_read_request(0xE2U, request));
    assert(request[0] == 0xE2U && request[1] == 0xE2U && request[2] == 0x80U &&
           request[3] == 0x09U && request[4] == 0x0AU);

    const esp_fardriver_state_t before_invalid_frame = state;
    frame[15] ^= 1U;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(memcmp(&state, &before_invalid_frame, sizeof(state)) == 0);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame) - 1U));
    frame[0] = 0xABU;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    return 0;
}
