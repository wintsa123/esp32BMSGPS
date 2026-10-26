#include "esp_bms_ctrl_suniu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 速牛帧：28 | len | cmd | data... | xor | 29
 * 整帧长度 = len + 4，xor = byte[1] ^ ... ^ byte[total-3]（v6/d1.java 的 G() 判据）。 */

static void put_be16(uint8_t *out, size_t offset, uint16_t value)
{
    out[offset] = (uint8_t)((value >> 8U) & 0xFFU);
    out[offset + 1U] = (uint8_t)(value & 0xFFU);
}

static void finalize(uint8_t *frame, size_t frame_len)
{
    uint8_t value = 0U;
    for (size_t index = 1U; index + 2U < frame_len; ++index) {
        value = (uint8_t)(value ^ frame[index]);
    }
    frame[frame_len - 2U] = value;
    frame[frame_len - 1U] = 0x29U;
}

/* 0x93 F3 主遥测帧：20 字节 */
static void build_realtime(uint8_t *frame, int16_t voltage, int16_t current, int16_t rpm, int16_t throttle)
{
    memset(frame, 0, 20U);
    frame[0] = 0x28U;
    frame[1] = 0x10U;
    frame[2] = 0x93U;
    frame[3] = 0xF3U;
    put_be16(frame, 4U, (uint16_t)voltage);
    put_be16(frame, 6U, (uint16_t)current);
    put_be16(frame, 8U, 999U);  /* 相电流：契约无字段 */
    put_be16(frame, 10U, 415U); /* 控制器温度 41.5 °C */
    put_be16(frame, 12U, 330U); /* 电机温度 33.0 °C */
    put_be16(frame, 14U, (uint16_t)rpm);
    put_be16(frame, 16U, (uint16_t)throttle);
    finalize(frame, 20U);
}

static const uint8_t REALTIME_FRAME[20] = { 0x28U, 0x10U, 0x93U, 0xF3U, 0x02U, 0xD0U, 0x04U,
                                            0xD2U, 0x03U, 0xE7U, 0x01U, 0x9FU, 0x01U, 0x4AU,
                                            0x02U, 0x58U, 0x02U, 0x26U, 0x3BU, 0x29U };

static const uint8_t FAULT_FRAME[10] = { 0x28U, 0x06U, 0x93U, 0xF0U, 0x00U,
                                         0x00U, 0x00U, 0x05U, 0x60U, 0x29U };

static const uint8_t FUNCTION_FRAME[10] = { 0x28U, 0x06U, 0x93U, 0xF1U, 0x00U,
                                            0x03U, 0x00U, 0x00U, 0x67U, 0x29U };

/* 0x94 参数帧：150 周长 1400mm、151 校正 2048、152 传动比 2048 */
static const uint8_t PARAM_FRAME[17] = { 0x28U, 0x0DU, 0x94U, 0x00U, 0x96U, 0x05U, 0x78U, 0x00U, 0x97U,
                                         0x08U, 0x00U, 0x00U, 0x98U, 0x08U, 0x00U, 0x7DU, 0x29U };

static void expect_invalid(const esp_bms_ctrl_telemetry_t *telemetry)
{
    assert(!telemetry->rpm_valid);
    assert(!telemetry->speed_valid);
    assert(!telemetry->gear_valid);
    assert(!telemetry->current_valid);
    assert(!telemetry->power_valid);
    assert(!telemetry->voltage_valid);
    assert(!telemetry->controller_temp_valid);
    assert(!telemetry->motor_temp_valid);
    assert(!telemetry->throttle_valid);
    assert(!telemetry->fault_valid);
    assert(!telemetry->controller_speed_params_valid);
}

int main(void)
{
    uint8_t buffer[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t out_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    /* 1) 开启帧按 联机 D0 → 版本 E0 → 实时 F3 轮换；轮询按 8 拍一轮 */
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_build_start(0U, buffer, &out_len));
    assert(out_len == 5U && memcmp(buffer, (const uint8_t[]){ 0x28U, 0x01U, 0xD0U, 0xD1U, 0x29U }, 5U) == 0);
    assert(esp_bms_ctrl_suniu_build_start(1U, buffer, &out_len));
    assert(out_len == 5U && memcmp(buffer, (const uint8_t[]){ 0x28U, 0x01U, 0xE0U, 0xE1U, 0x29U }, 5U) == 0);
    assert(esp_bms_ctrl_suniu_build_start(2U, buffer, &out_len));
    assert(out_len == 6U &&
           memcmp(buffer, (const uint8_t[]){ 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U }, 6U) == 0);
    assert(esp_bms_ctrl_suniu_build_start(3U, buffer, &out_len));
    assert(buffer[2] == 0xD0U);

    const uint8_t expected_poll[8][11] = {
        { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U },                              /* 0 实时 */
        { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U },                              /* 1 实时 */
        { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U },                              /* 2 实时 */
        { 0x28U, 0x02U, 0xD3U, 0xF0U, 0x21U, 0x29U },                              /* 3 故障位图 */
        { 0x28U, 0x02U, 0xD3U, 0xF1U, 0x20U, 0x29U },                              /* 4 功能位图 + 挡位 */
        { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U },                              /* 5 实时 */
        { 0x28U, 0x07U, 0xD4U, 0x00U, 0x96U, 0x00U, 0x97U, 0x00U, 0x98U, 0x4AU, 0x29U }, /* 6 轮胎参数 */
        { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U },                              /* 7 实时 */
    };
    const size_t expected_poll_len[8] = { 6U, 6U, 6U, 6U, 6U, 6U, 11U, 6U };
    for (uint16_t step = 0U; step < 8U; ++step) {
        assert(esp_bms_ctrl_suniu_build_poll(step, step * 200U, buffer, &out_len));
        assert(out_len == expected_poll_len[step]);
        assert(memcmp(buffer, expected_poll[step], out_len) == 0);
    }
    assert(esp_bms_ctrl_suniu_build_poll(8U, 0U, buffer, &out_len) && buffer[3] == 0xF3U);
    assert(!esp_bms_ctrl_suniu_build_start(0U, NULL, &out_len));
    assert(!esp_bms_ctrl_suniu_build_poll(0U, 0U, buffer, NULL));

    /* 2) 正例：0x93 F3 实时帧字段与单位换算 */
    printf("suniu F3 frame: ");
    for (size_t index = 0U; index < sizeof(REALTIME_FRAME); ++index) {
        printf("%02X%s", REALTIME_FRAME[index], (index + 1U == sizeof(REALTIME_FRAME)) ? "\n" : " ");
    }
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_feed(REALTIME_FRAME, sizeof(REALTIME_FRAME)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 720U);    /* 72.0 V */
    assert(telemetry.current_valid && telemetry.current_centi_a == 12340);  /* 123.40 A */
    assert(telemetry.power_valid && telemetry.power_w == 8884);             /* 720*12340/1000 */
    assert(telemetry.rpm_valid && telemetry.rpm == 600U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 42); /* 41.5 → 42 */
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 33);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 55U);
    assert(!telemetry.gear_valid && !telemetry.fault_valid);
    assert(!telemetry.speed_valid); /* 还缺 0x94 轮胎参数 */
    assert(!telemetry.controller_speed_params_valid);

    /* 3) XOR 校验错误：被拒，不产生假有效位 */
    uint8_t corrupt[20];
    memcpy(corrupt, REALTIME_FRAME, sizeof(corrupt));
    corrupt[18U] ^= 0x01U; /* 改 xor 字节 */
    esp_bms_ctrl_suniu_reset();
    assert(!esp_bms_ctrl_suniu_feed(corrupt, sizeof(corrupt)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    expect_invalid(&telemetry);

    /* 帧尾不是 0x29 也要拒绝 */
    memcpy(corrupt, REALTIME_FRAME, sizeof(corrupt));
    corrupt[19U] = 0x28U;
    esp_bms_ctrl_suniu_reset();
    assert(!esp_bms_ctrl_suniu_feed(corrupt, sizeof(corrupt)));

    /* 4) 分片到达：任意切分点都不能提前解析 */
    for (size_t split = 1U; split < sizeof(REALTIME_FRAME); ++split) {
        esp_bms_ctrl_suniu_reset();
        const size_t head = (split > 2U) ? 2U : split; /* 前一片切在长度字节里 */
        assert(!esp_bms_ctrl_suniu_feed(REALTIME_FRAME, head));
        assert(!esp_bms_ctrl_suniu_feed(REALTIME_FRAME + head, split - head));
        esp_bms_ctrl_suniu_telemetry(&telemetry);
        assert(!telemetry.voltage_valid);
        assert(esp_bms_ctrl_suniu_feed(REALTIME_FRAME + split, sizeof(REALTIME_FRAME) - split));
        esp_bms_ctrl_suniu_telemetry(&telemetry);
        assert(telemetry.voltage_deci_v == 720U && telemetry.rpm == 600U);
    }

    /* 5) 噪声前缀 + 粘包：F3 + F0 + F1 + 参数帧连发 */
    uint8_t joined[4U + 20U + 10U + 10U + 17U];
    joined[0] = 0x11U;
    joined[1] = 0x22U;
    joined[2] = 0x33U;
    joined[3] = 0x44U;
    memcpy(joined + 4U, REALTIME_FRAME, 20U);
    memcpy(joined + 24U, FAULT_FRAME, 10U);
    memcpy(joined + 34U, FUNCTION_FRAME, 10U);
    memcpy(joined + 44U, PARAM_FRAME, 17U);
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_feed(joined, sizeof(joined)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.controller_speed_params_valid);
    assert(telemetry.wheel_circumference_mm == 1400U);
    assert(telemetry.gear_ratio_centi == 100U); /* 2048 / 2048 = 1.00 */
    /* 车速 = rpm × 周长 × 600 / 1e6 × 校正/2048 × 传动比/2048（v6.d1.L） */
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 504U); /* 50.4 km/h */

    /* 6) 单独 0xF0 / 0xF1 帧：只更新对应字段，不清掉已有遥测 */
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_feed(FAULT_FRAME, sizeof(FAULT_FRAME)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    assert(!telemetry.voltage_valid && !telemetry.rpm_valid);

    /* 7) 越界字段丢弃：油门 1200（120%）与负转速都不产生有效位 */
    uint8_t out_of_range[20];
    build_realtime(out_of_range, 720, 1234, -5, 1200);
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_feed(out_of_range, sizeof(out_of_range)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.throttle_valid);
    assert(telemetry.voltage_valid && telemetry.current_valid);

    /* 8) 超长帧（len + 4 > ESP_BMS_CTRL_FRAME_MAX_LEN）：丢弃并重新同步到下一帧 */
    uint8_t oversize[1U + 2U + 20U];
    oversize[0] = 0x28U;
    oversize[1] = (uint8_t)(ESP_BMS_CTRL_FRAME_MAX_LEN - 3U); /* 整帧 = MAX + 1 */
    oversize[2] = 0x93U;
    memcpy(oversize + 3U, REALTIME_FRAME, 20U);
    esp_bms_ctrl_suniu_reset();
    assert(esp_bms_ctrl_suniu_feed(oversize, sizeof(oversize)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 600U && telemetry.voltage_deci_v == 720U);

    /* 9) 只有噪声：不产出任何有效位 */
    esp_bms_ctrl_suniu_reset();
    const uint8_t noise[6] = { 0x00U, 0x55U, 0xAAU, 0x28U, 0x00U, 0x99U };
    assert(!esp_bms_ctrl_suniu_feed(noise, sizeof(noise)));
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    expect_invalid(&telemetry);

    /* 10) reset 清空 */
    esp_bms_ctrl_suniu_reset();
    esp_bms_ctrl_suniu_telemetry(&telemetry);
    expect_invalid(&telemetry);
    assert(telemetry.rpm == 0U && telemetry.power_w == 0 && telemetry.wheel_circumference_mm == 0U);

    assert(!esp_bms_ctrl_suniu_feed(NULL, 0U));
    printf("suniu ctrl protocol selftest: OK\n");
    return 0;
}
