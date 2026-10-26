#include "esp_bms_ctrl_jh.h"

#include <assert.h>
#include <string.h>

/* 0x04 0x03 0x0E + 7 个寄存器（大端）+ CRC16。
 * 状态 0x0005 / 转速 3200rpm / 母线 528(52.8V) / 电流 0xFFE7(-2.5A) /
 * 相线 250(25.0A) / MOS 45℃ / 电机 52℃。 */
static const uint8_t realtime_frame[] = {
    0x04U, 0x03U, 0x0EU, 0x00U, 0x05U, 0x0CU, 0x80U, 0x02U, 0x10U, 0xFFU,
    0xE7U, 0x00U, 0xFAU, 0x00U, 0x2DU, 0x00U, 0x34U, 0xB4U, 0x59U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_jh_reset();

    /* 开启/握手：型号 JN → 版本 JN → 实时块读取，周期重发由 attempt 递增驱动。 */
    assert(esp_bms_ctrl_jh_build_start(0U, frame, &frame_len) && frame_len == 10U);
    assert(memcmp(frame, (const uint8_t[]){ 0x4AU, 0x4EU, 0x27U, 0x03U, 0x22U, 0xF1U, 0x87U, 0xC4U, 0x4BU,
                                            0x4AU },
                  frame_len) == 0);
    assert(esp_bms_ctrl_jh_build_start(1U, frame, &frame_len) && frame_len == 10U);
    assert(frame[6U] == 0x95U && frame[7U] == 0xD2U);
    assert(esp_bms_ctrl_jh_build_start(2U, frame, &frame_len) && frame_len == 8U);
    assert(memcmp(frame, (const uint8_t[]){ 0x04U, 0x03U, 0xA1U, 0x00U, 0x00U, 0x07U, 0x27U, 0xA1U },
                  frame_len) == 0);

    /* 在线轮询：默认读实时块，第 25 拍补读信息块。 */
    assert(esp_bms_ctrl_jh_build_poll(0U, 0U, frame, &frame_len) && frame_len == 8U);
    assert(frame[2U] == 0xA1U && frame[3U] == 0x00U && frame[5U] == 0x07U);
    assert(esp_bms_ctrl_jh_build_poll(24U, 4800U, frame, &frame_len) && frame_len == 8U);
    assert(frame[3U] == 0x10U && frame[5U] == 0x1BU);

    /* 正例：完整实时帧。 */
    assert(esp_bms_ctrl_jh_feed(realtime_frame, sizeof(realtime_frame)));
    esp_bms_ctrl_jh_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 528U);
    assert(telemetry.current_valid && telemetry.current_centi_a == -250);
    assert(telemetry.power_valid && telemetry.power_w == -132);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 52);
    assert(telemetry.fault_valid && telemetry.fault_code == 0x0001U);

    /* 分片到达：先 10 字节，再补剩余 9 字节。 */
    esp_bms_ctrl_jh_reset();
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_ctrl_jh_feed(realtime_frame, 10U));
    esp_bms_ctrl_jh_telemetry(&telemetry);
    assert(!telemetry.rpm_valid);
    assert(esp_bms_ctrl_jh_feed(realtime_frame + 10U, sizeof(realtime_frame) - 10U));
    esp_bms_ctrl_jh_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U && telemetry.voltage_deci_v == 528U);

    /* 校验错误帧被拒（改 CRC 高字节）。 */
    uint8_t corrupt[sizeof(realtime_frame)] = { 0 };
    memcpy(corrupt, realtime_frame, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1U] ^= 0x01U;
    esp_bms_ctrl_jh_reset();
    assert(!esp_bms_ctrl_jh_feed(corrupt, sizeof(corrupt)));
    memset(&telemetry, 0, sizeof(telemetry));
    esp_bms_ctrl_jh_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid);

    return 0;
}
