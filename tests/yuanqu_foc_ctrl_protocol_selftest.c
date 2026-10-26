/* 远驱 YuanQu FOC 控制器协议自测：16 字节分页帧、SUM16 校验、分片、保活变体切换。
 * 期望值来自 Motexa 2.7.0 v6.n1（帧引擎/分页解析）+ kl（字段解释）+ jl（适配器）。 */

#include "esp_bms_ctrl_yuanqu_foc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAME_LEN 16U
#define KEEPALIVE_LEN 8U

static void finish_frame(uint8_t frame[FRAME_LEN])
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < 14U; ++index) {
        sum += frame[index];
    }
    frame[14] = (uint8_t)((sum >> 8U) & 0xFFU);
    frame[15] = (uint8_t)(sum & 0xFFU);
}

/* v6.n1.D：AA | 页号 | 12 字节数据 | SUM16(大端) */
static void make_frame(uint8_t frame[FRAME_LEN], uint8_t page, const uint8_t data[12])
{
    memset(frame, 0, FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = page;
    memcpy(frame + 2U, data, 12U);
    finish_frame(frame);
}

/* 页 0x00：@4 驱动状态/xs 挡位、@6 转速、@8/@9 故障位图 */
static void make_status_frame(uint8_t frame[FRAME_LEN], uint8_t state_byte, int16_t rpm, uint8_t fault_0,
                              uint8_t fault_1, uint8_t pass_code)
{
    uint8_t data[12] = { 0 };
    data[2] = state_byte;
    data[3] = (uint8_t)((pass_code & 0x03U) << 2U);
    data[4] = (uint8_t)(((uint16_t)rpm >> 8U) & 0xFFU);
    data[5] = (uint8_t)((uint16_t)rpm & 0xFFU);
    data[6] = fault_0;
    data[7] = fault_1;
    make_frame(frame, 0x00U, data);
}

/* 页 0x01：@2 母线电压(0.1V)、@4 母线电流(0.25A) */
static void make_power_frame(uint8_t frame[FRAME_LEN], int16_t raw_voltage, int16_t raw_current)
{
    uint8_t data[12] = { 0 };
    data[0] = (uint8_t)(((uint16_t)raw_voltage >> 8U) & 0xFFU);
    data[1] = (uint8_t)((uint16_t)raw_voltage & 0xFFU);
    data[2] = (uint8_t)(((uint16_t)raw_current >> 8U) & 0xFFU);
    data[3] = (uint8_t)((uint16_t)raw_current & 0xFFU);
    data[4] = 0x40U; /* 调制比 0.5 */
    data[5] = 0x01U; /* 弱磁模式 */
    data[10] = 0x01U;
    data[11] = 0x80U; /* 转把深度原始值 384 */
    make_frame(frame, 0x01U, data);
}

/* 页 0x04：@4 控制器/MOS 温度 */
static void make_temperature_frame(uint8_t frame[FRAME_LEN], uint8_t celsius)
{
    uint8_t data[12] = { 0 };
    data[2] = celsius;
    make_frame(frame, 0x04U, data);
}

/* 页 0x0D：@2 电机温度原始值、@4 转把 ADC 原始值 */
static void make_motor_frame(uint8_t frame[FRAME_LEN], uint8_t motor_temp_raw, uint16_t throttle_raw)
{
    uint8_t data[12] = { 0 };
    data[0] = motor_temp_raw;
    data[2] = (uint8_t)((throttle_raw >> 8U) & 0xFFU);
    data[3] = (uint8_t)(throttle_raw & 0xFFU);
    make_frame(frame, 0x0DU, data);
}

static const uint8_t KEEPALIVE_DISCOVERY[KEEPALIVE_LEN] = { 0xAAU, 0x17U, 0xE8U, 0xF1U,
                                                            0x13U, 0x51U, 0xFEU, 0x01U };
static const uint8_t KEEPALIVE_PRIMARY_A[KEEPALIVE_LEN] = { 0xAAU, 0x13U, 0xECU, 0x07U,
                                                            0x5FU, 0x5FU, 0x6EU, 0x91U };
static const uint8_t KEEPALIVE_PRIMARY_B[KEEPALIVE_LEN] = { 0xAAU, 0x13U, 0xECU, 0x07U,
                                                            0x09U, 0x6FU, 0x28U, 0xD7U };

static void test_keepalive(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;

    esp_bms_ctrl_yuanqu_foc_reset();
    /* attempt 0 必须是发现/保活帧（v6.n1.g，未验证发现结果） */
    assert(esp_bms_ctrl_yuanqu_foc_build_start(0U, frame, &len));
    assert(len == KEEPALIVE_LEN);
    assert(memcmp(frame, KEEPALIVE_DISCOVERY, KEEPALIVE_LEN) == 0);

    /* 还没有连接状态码 -> 主保活变体 A */
    assert(esp_bms_ctrl_yuanqu_foc_build_start(1U, frame, &len));
    assert(memcmp(frame, KEEPALIVE_PRIMARY_A, KEEPALIVE_LEN) == 0);
    assert(esp_bms_ctrl_yuanqu_foc_build_poll(0U, 0U, frame, &len));
    assert(memcmp(frame, KEEPALIVE_PRIMARY_A, KEEPALIVE_LEN) == 0);

    assert(!esp_bms_ctrl_yuanqu_foc_build_start(0U, NULL, &len));
    assert(!esp_bms_ctrl_yuanqu_foc_build_poll(0U, 0U, NULL, &len));
}

static void test_pages(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;
    uint8_t poll[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;

    esp_bms_ctrl_yuanqu_foc_reset();
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid);

    /* 页 0x01：72.0V / 15.0A（60 * 0.25A） */
    make_power_frame(frame, 720, 60);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 720U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 1500);
    assert(telemetry.power_valid && telemetry.power_w == 1080);

    /* 页 0x00：xs 编码 1 -> 2 挡，600rpm，故障 0x0102 */
    make_status_frame(frame, 0x0DU, 600, 0x01U, 0x82U, 3U);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 600U);
    assert(telemetry.gear_valid && telemetry.gear == 2U);
    assert(telemetry.fault_valid && telemetry.fault_code == 0x0102U);
    assert(!telemetry.speed_valid); /* FOC 旧协议不换算车速 */

    /* 页 0x04：控制器温度 45℃ */
    make_temperature_frame(frame, 45U);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(!telemetry.motor_temp_valid);

    /* 页 0x0D：转把 ADC 1655 -> 2.000V -> (2.000-0.85)/3.35 = 34% */
    make_motor_frame(frame, 38U, 1655U);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 34U);

    /* 连接状态码 > 0 -> 保活切到变体 B（v6.n1.g 的 f19709g > 0 分支） */
    assert(esp_bms_ctrl_yuanqu_foc_build_poll(1U, 0U, poll, &len));
    assert(memcmp(poll, KEEPALIVE_PRIMARY_B, KEEPALIVE_LEN) == 0);
}

static void test_checksum_rejected(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_yuanqu_foc_reset();
    make_power_frame(frame, 720, 60);
    frame[15] ^= 0x3CU; /* 破坏 SUM16 */
    assert(!esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && !telemetry.current_valid && !telemetry.power_valid);

    /* 页号字段被篡改而校验位没跟着改 -> 也必须拒收 */
    esp_bms_ctrl_yuanqu_foc_reset();
    make_power_frame(frame, 720, 60);
    frame[1] = 0x00U;
    assert(!esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.gear_valid);

    /* 页 0x04 的越界温度（> 200）不得产生有效位 */
    esp_bms_ctrl_yuanqu_foc_reset();
    make_temperature_frame(frame, 210U);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.controller_temp_valid);

    /* 电压为 0 或越界：V 无效，功率也随之无效 */
    esp_bms_ctrl_yuanqu_foc_reset();
    make_power_frame(frame, 0, 60);
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame, FRAME_LEN));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && !telemetry.power_valid);
    assert(telemetry.current_valid && telemetry.current_centi_a == 1500);
}

static void test_fragmented(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_yuanqu_foc_reset();
    make_power_frame(frame, 720, 60);
    /* 前半 9 字节：不足一帧 */
    assert(!esp_bms_ctrl_yuanqu_foc_feed(frame, 9U));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);
    /* 后半补齐 */
    assert(esp_bms_ctrl_yuanqu_foc_feed(frame + 9U, FRAME_LEN - 9U));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 720U);
    assert(telemetry.current_centi_a == 1500);

    /* 两帧共用一个通知（含 1 字节噪声前缀） */
    uint8_t pair[FRAME_LEN * 2U + 1U] = { 0 };
    uint8_t status[FRAME_LEN] = { 0 };
    make_status_frame(status, 0x0DU, -300, 0x00U, 0x00U, 0U);
    pair[0] = 0x33U;
    memcpy(pair + 1U, frame, FRAME_LEN);
    memcpy(pair + 1U + FRAME_LEN, status, FRAME_LEN);
    assert(esp_bms_ctrl_yuanqu_foc_feed(pair, sizeof(pair)));
    esp_bms_ctrl_yuanqu_foc_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 720U);
    /* 反转：转速取绝对值 */
    assert(telemetry.rpm_valid && telemetry.rpm == 300U);
    /* 连接状态码 0 -> 保活回到变体 A */
    uint8_t poll[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;
    assert(esp_bms_ctrl_yuanqu_foc_build_poll(2U, 0U, poll, &len));
    assert(memcmp(poll, KEEPALIVE_PRIMARY_A, KEEPALIVE_LEN) == 0);

    /* 未知页（0x02 相电流）是合法帧但不产生遥测字段 */
    uint8_t data[12] = { 0x00U, 0x10U, 0x00U };
    uint8_t phase[FRAME_LEN] = { 0 };
    make_frame(phase, 0x02U, data);
    assert(esp_bms_ctrl_yuanqu_foc_feed(phase, FRAME_LEN));

    uint8_t junk[400];
    memset(junk, 0x77, sizeof(junk));
    assert(!esp_bms_ctrl_yuanqu_foc_feed(junk, sizeof(junk)));
    assert(!esp_bms_ctrl_yuanqu_foc_feed(NULL, 0U));
}

int main(void)
{
    test_keepalive();
    test_pages();
    test_checksum_rejected();
    test_fragmented();
    printf("yuanqu_foc ctrl protocol selftest: OK\n");
    return 0;
}
