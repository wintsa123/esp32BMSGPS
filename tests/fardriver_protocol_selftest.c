#include "esp_fardriver_protocol.h"

#include <assert.h>
#include <string.h>

static void finish_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    frame[14] = (uint8_t)(crc >> 8U);
    frame[15] = (uint8_t)crc;
}

static void make_speed_params_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    memset(frame, 0, ESP_FARDRIVER_FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = 47U; /* FLASH_READ_ADDR[47] is D0, which includes D2-D4. */
    frame[6] = 70U;
    frame[7] = 12U;
    frame[8] = 90U;
    frame[9] = 0U;
    frame[10] = 60U;
    frame[11] = 0U;
    finish_frame(frame);
}

int main(void)
{
    esp_fardriver_state_t state = { .fallback_wheel_circumference_mm = 1350U,
                                    .fallback_gear_ratio_centi = 400U };
    uint8_t frame[ESP_FARDRIVER_FRAME_LEN] = { 0 };

    /* id 0 帧: RPM + 档位（档位在 fault_byte4 的 bit2-3）+ 相电流 iq/id */
    for (uint8_t gear = 0U; gear < 4U; ++gear) {
        memset(frame, 0, sizeof(frame));
        frame[0] = 0xAAU;
        frame[1] = 0U;
        frame[4] = (uint8_t)(gear << 2U);
        frame[6] = 0x12U;
        frame[7] = 0xC0U;
        finish_frame(frame);
        assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
        assert(state.rpm_valid && state.rpm == 4800U);
        assert(state.gear_valid && state.gear == gear);
        assert(state.speed_valid && state.speed_deci_kmh == 972U);
    }

    /* id 0 帧: iq=4.00A, id=0 -> 线电流 400 (0.01A) */
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 0U;
    frame[6] = 0x12U;
    frame[7] = 0xC0U; /* 保持 rpm=4800，避免影响后续速度断言 */
    frame[10] = 0x01U;
    frame[11] = 0x90U;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.current_valid && state.current_centi_a == 400);

    /* id 1 帧: 电压 90.0V -> 功率 = 90.0V * 4.00A = 360W */
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 1U;
    frame[2] = 0x03U;
    frame[3] = 0x84U;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.voltage_deci_v == 900U && state.power_valid && state.power_w == 360);

    /* id 4 帧: 控制器温度 */
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 4U;
    frame[4] = 72U;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_temp_valid && state.controller_temp_c == 72);

    /* id 13 帧: 电机温度 */
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAAU;
    frame[1] = 13U;
    frame[2] = 61U;
    finish_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.motor_temp_valid && state.motor_temp_c == 61);

    /* id 47 帧: 轮胎/传动比参数块（0xD0 起，含 D2-D4） */
    make_speed_params_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_speed_params_valid);
    assert(state.tire_aspect_percent == 70U && state.tire_rim_inch == 12U &&
           state.tire_width_mm == 90U && state.wheel_circumference_mm == 1353U &&
           state.gear_ratio_centi == 100U && state.speed_deci_kmh == 3896U);

    /* open / keepalive 命令字节（与已验证可用的参考实现一致） */
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN] = { 0 };
    assert(esp_fardriver_build_open_command(command));
    assert(command[0] == 0xAAU && command[1] == 0x13U && command[2] == 0xECU &&
           command[3] == 0x07U && command[4] == 0x01U && command[5] == 0xF1U &&
           command[6] == 0xA2U && command[7] == 0x5DU);
    assert(esp_fardriver_build_keepalive_command(command));
    assert(command[0] == 0xAAU && command[1] == 0x13U && command[2] == 0xECU &&
           command[3] == 0x07U && command[4] == 0x5FU && command[5] == 0x5FU &&
           command[6] == 0x6EU && command[7] == 0x91U);

    /* 无效帧 */
    const esp_fardriver_state_t before_invalid_frame = state;
    frame[15] ^= 1U;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(memcmp(&state, &before_invalid_frame, sizeof(state)) == 0);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame) - 1U));
    frame[0] = 0xABU;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    return 0;
}
