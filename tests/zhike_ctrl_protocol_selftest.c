#include "esp_bms_ctrl_zhike.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 智科实时帧构造：AA 13 00 + 31 个小端数据字 + 1 个小端校验字，
 * 使 32 个字之和 & 0xFFFF == 0xFFFF（v6/p1.java 的 w() 判据）。 */

#define ZHIKE_FRAME_LEN 67U
#define ZHIKE_WORD_COUNT 32U

static void put_word(uint8_t *frame, size_t index, uint16_t value)
{
    frame[3U + (index * 2U)] = (uint8_t)(value & 0xFFU);
    frame[4U + (index * 2U)] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void finalize_frame(uint8_t *frame)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index + 1U < ZHIKE_WORD_COUNT; ++index) {
        sum += (uint32_t)frame[3U + (index * 2U)] | ((uint32_t)frame[4U + (index * 2U)] << 8U);
    }
    put_word(frame, ZHIKE_WORD_COUNT - 1U, (uint16_t)(0xFFFFU - (sum & 0xFFFFU)));
}

static void build_reference_frame(uint8_t *frame)
{
    memset(frame, 0, ZHIKE_FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = 0x13U;
    frame[2] = 0x00U;
    put_word(frame, 2U, 2450U);                /* 油门电压 raw（本移植不产出百分比） */
    put_word(frame, 6U, 2730U);                /* 转速 raw：2730 / 5.46 = 500 rpm */
    put_word(frame, 8U, 16384U);               /* 母线电压 raw：16384 * 15 / 4096 = 60.0 V */
    put_word(frame, 9U, (uint16_t)(int16_t)-25); /* 母线电流 -25 A */
    put_word(frame, 18U, 4250U);               /* 控制器温度 42.50 °C → 43 */
    put_word(frame, 21U, 0x0118U);             /* 模式=1、挡位 high（bit4-5=1）、刹车 bit3 */
    put_word(frame, 22U, 0x0005U);             /* 故障位图 */
    finalize_frame(frame);
}

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

    /* 1) 开启帧 / 轮询帧与 Motexa 常量逐字一致 */
    esp_bms_ctrl_zhike_reset();
    for (uint8_t attempt = 0U; attempt < 4U; ++attempt) {
        assert(esp_bms_ctrl_zhike_build_start(attempt, buffer, &out_len));
        assert(out_len == 2U);
        assert(buffer[0] == 0x01U && buffer[1] == 0x02U);
    }
    for (uint16_t step = 0U; step < 4U; ++step) {
        assert(esp_bms_ctrl_zhike_build_poll(step, step * 200U, buffer, &out_len));
        assert(out_len == 8U);
        assert(memcmp(buffer, (const uint8_t[]){ 0xAAU, 0x13U, 0xFFU, 0x01U, 0xAAU, 0x13U, 0x00U, 0x01U },
                      out_len) == 0);
    }
    assert(!esp_bms_ctrl_zhike_build_start(0U, NULL, &out_len));
    assert(!esp_bms_ctrl_zhike_build_poll(0U, 0U, buffer, NULL));

    /* 2) 正例帧：一次投递即解析，并断言关键字段与单位换算 */
    uint8_t frame[ZHIKE_FRAME_LEN];
    build_reference_frame(frame);
    printf("zhike 67B frame: ");
    for (size_t index = 0U; index < ZHIKE_FRAME_LEN; ++index) {
        printf("%02X%s", frame[index], (index + 1U == ZHIKE_FRAME_LEN) ? "\n" : " ");
    }
    /* 硬编码校验字：sum(word0..word30) = 91610，0xFFFF - (91610 & 0xFFFF) = 0x9A25 */
    assert(frame[65U] == 0x25U && frame[66U] == 0x9AU);

    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(frame, sizeof(frame)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 500U);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 600U);      /* 60.0 V */
    assert(telemetry.current_valid && telemetry.current_centi_a == -2500);     /* -25.00 A */
    assert(telemetry.power_valid && telemetry.power_w == -1500);               /* 600 * -2500 / 1000 */
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 43);
    assert(telemetry.gear_valid && telemetry.gear == 3U);                      /* high → 3 挡 */
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    /* 智科实时帧没有车速 / 电机温度 / 油门百分比 */
    assert(!telemetry.speed_valid && !telemetry.motor_temp_valid && !telemetry.throttle_valid);

    /* 3) 校验和错误帧：被拒，且不产生假有效位 */
    uint8_t corrupt[ZHIKE_FRAME_LEN];
    memcpy(corrupt, frame, sizeof(corrupt));
    corrupt[3U + (10U * 2U)] ^= 0x01U; /* 改 1 个数据字，校验和立即不成立 */
    esp_bms_ctrl_zhike_reset();
    assert(!esp_bms_ctrl_zhike_feed(corrupt, sizeof(corrupt)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    expect_invalid(&telemetry);

    /* 帧头不变、尾部校验字被改也拒绝 */
    memcpy(corrupt, frame, sizeof(corrupt));
    corrupt[65U] ^= 0x01U;
    esp_bms_ctrl_zhike_reset();
    assert(!esp_bms_ctrl_zhike_feed(corrupt, sizeof(corrupt)));

    /* 4) 分片到达：任意切分点都不能提前产出数据，只在凑满 67 字节时解析 */
    esp_bms_ctrl_zhike_reset();
    for (size_t split = 1U; split < ZHIKE_FRAME_LEN; ++split) {
        esp_bms_ctrl_zhike_reset();
        size_t first = (split > 2U) ? 2U : split; /* 前两片刻意切在帧头中间 */
        assert(!esp_bms_ctrl_zhike_feed(frame, first));
        assert(!esp_bms_ctrl_zhike_feed(frame + first, split - first));
        esp_bms_ctrl_zhike_telemetry(&telemetry);
        assert(!telemetry.rpm_valid);
        assert(esp_bms_ctrl_zhike_feed(frame + split, ZHIKE_FRAME_LEN - split));
        esp_bms_ctrl_zhike_telemetry(&telemetry);
        assert(telemetry.rpm_valid && telemetry.rpm == 500U);
        assert(telemetry.voltage_deci_v == 600U && telemetry.current_centi_a == -2500);
    }

    /* 5) 前面带噪声：滑动找帧后仍能解析 */
    uint8_t noisy[ZHIKE_FRAME_LEN + 5U];
    memset(noisy, 0x77, 5U);
    memcpy(noisy + 5U, frame, ZHIKE_FRAME_LEN);
    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(noisy, sizeof(noisy)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 500U);

    /* 6) 4 字节短应答（AA 13 00 01）不含遥测；后面紧跟真实帧时两帧都要吃掉 */
    const uint8_t short_response[4] = { 0xAAU, 0x13U, 0x00U, 0x01U };
    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(short_response, sizeof(short_response)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    expect_invalid(&telemetry);

    uint8_t short_then_frame[4U + ZHIKE_FRAME_LEN];
    memcpy(short_then_frame, short_response, sizeof(short_response));
    memcpy(short_then_frame + sizeof(short_response), frame, ZHIKE_FRAME_LEN);
    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(short_then_frame, sizeof(short_then_frame)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 500U);

    /* 7) 粘包：两帧连发，状态取最后一帧 */
    uint8_t double_frame[ZHIKE_FRAME_LEN * 2U];
    memcpy(double_frame, frame, ZHIKE_FRAME_LEN);
    memcpy(double_frame + ZHIKE_FRAME_LEN, frame, ZHIKE_FRAME_LEN);
    put_word(double_frame + ZHIKE_FRAME_LEN, 9U, (uint16_t)(int16_t)30); /* 第二帧 30 A */
    finalize_frame(double_frame + ZHIKE_FRAME_LEN);
    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(double_frame, sizeof(double_frame)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    assert(telemetry.current_centi_a == 3000);

    /* 8) 超长通知：只留尾部，不溢出；随后仍能解析完整帧 */
    uint8_t huge[256];
    memset(huge, 0x55, sizeof(huge));
    memcpy(huge + sizeof(huge) - ZHIKE_FRAME_LEN, frame, ZHIKE_FRAME_LEN);
    esp_bms_ctrl_zhike_reset();
    assert(esp_bms_ctrl_zhike_feed(huge, sizeof(huge)));
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 500U);

    /* 9) reset 清空一切 */
    esp_bms_ctrl_zhike_reset();
    esp_bms_ctrl_zhike_telemetry(&telemetry);
    expect_invalid(&telemetry);
    assert(telemetry.rpm == 0U && telemetry.voltage_deci_v == 0U && telemetry.power_w == 0);

    assert(!esp_bms_ctrl_zhike_feed(NULL, 0U));
    printf("zhike ctrl protocol selftest: OK\n");
    return 0;
}
