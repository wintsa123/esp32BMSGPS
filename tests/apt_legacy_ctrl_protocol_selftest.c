/* apt_legacy（APT 安培旧版 / apt-anpei）控制器协议自测：固定 32 字节帧、
 * 首字节识别、分片/粘包、字段解码。
 *
 * gcc -std=c11 -Wall -Wextra -Werror \
 *   -I components/esp_bms_controller_protocol/include \
 *   -I components/esp_bms_controller_protocol/protocols/apt_legacy \
 *   tests/apt_legacy_ctrl_protocol_selftest.c \
 *   components/esp_bms_controller_protocol/protocols/apt_legacy/esp_bms_ctrl_apt_legacy.c \
 *   -o /tmp/apt_legacy_test && /tmp/apt_legacy_test
 */

#include "esp_bms_ctrl_apt_legacy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t realtime_request[] = { 0xFDU, 0x48U, 0x55U };
static const uint8_t fallback_request[] = { 0xFDU, 0x48U, 0x60U };
static const uint8_t idc_request[] = { 0xFBU, 0xEDU, 0xDAU };

/* 32 字节实时帧：52.00V / 25.0A / 3000rpm / 电控 30°C / 电机 30°C /
 * 档位 3（raw 5 - 2）/ 当前故障位图 0x0002 / 帧尾 0xDA */
static const uint8_t realtime_frame[] = {
    0xDFU, 0x01U, 0x50U, 0x14U, 0xFAU, 0x00U, 0xB8U, 0x0BU, 0x28U, 0x00U, 0x00U, 0x00U, 0x0FU,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x05U, 0xDAU,
};

/* IDC 参数帧：只证明链路正常，不参与遥测 */
static const uint8_t idc_frame[] = {
    0xFBU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xDAU,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_apt_legacy_reset();

    /* --- 开启帧：实时读取；重试时退到 FD 48 60 --- */
    assert(esp_bms_ctrl_apt_legacy_build_start(0U, frame, &frame_len));
    assert(frame_len == sizeof(realtime_request));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_legacy_build_start(1U, frame, &frame_len));
    assert(frame_len == sizeof(fallback_request));
    assert(memcmp(frame, fallback_request, sizeof(fallback_request)) == 0);

    /* --- 轮询帧：每 4 拍插一次 IDC 参数读取 --- */
    assert(esp_bms_ctrl_apt_legacy_build_poll(0U, 0U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_legacy_build_poll(2U, 400U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_legacy_build_poll(3U, 600U, frame, &frame_len));
    assert(frame_len == sizeof(idc_request));
    assert(memcmp(frame, idc_request, sizeof(idc_request)) == 0);
    assert(esp_bms_ctrl_apt_legacy_build_poll(4U, 800U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);

    /* --- 实时帧解码 --- */
    assert(esp_bms_ctrl_apt_legacy_feed(realtime_frame, sizeof(realtime_frame)));
    esp_bms_ctrl_apt_legacy_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 520U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 2500);
    assert(telemetry.rpm_valid && telemetry.rpm == 3000U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 30);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 30);
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.fault_valid && telemetry.fault_code == 2U);
    assert(telemetry.power_valid && telemetry.power_w == 1300);
    assert(!telemetry.throttle_valid && telemetry.throttle_percent == 0U);
    assert(!telemetry.speed_valid);

    /* --- 分片到达：21 字节不够一帧，拼完才解析 --- */
    assert(!esp_bms_ctrl_apt_legacy_feed(realtime_frame, 21U));
    assert(esp_bms_ctrl_apt_legacy_feed(realtime_frame + 21U, sizeof(realtime_frame) - 21U));

    /* --- 粘包：实时帧 + IDC 帧一起到达 --- */
    uint8_t joined[sizeof(realtime_frame) + sizeof(idc_frame)] = { 0 };
    memcpy(joined, realtime_frame, sizeof(realtime_frame));
    memcpy(joined + sizeof(realtime_frame), idc_frame, sizeof(idc_frame));
    assert(esp_bms_ctrl_apt_legacy_feed(joined, sizeof(joined)));

    /* --- IDC 帧本身也是有效帧，但不改遥测 --- */
    assert(!esp_bms_ctrl_apt_legacy_feed(idc_frame, sizeof(idc_frame)));
    esp_bms_ctrl_apt_legacy_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 520U);

    /* --- 首字节前塞垃圾：必须滑动到 0xDF 再对齐 --- */
    uint8_t noisy[sizeof(realtime_frame) + 5U] = { 0x11U, 0x22U, 0x33U, 0x44U, 0x55U };
    memcpy(noisy + 5U, realtime_frame, sizeof(realtime_frame));
    assert(esp_bms_ctrl_apt_legacy_feed(noisy, sizeof(noisy)));

    /* --- 帧尾标记不是 0xDA 时仍按 32 字节窗口接受（Motexa 只标 unverified） --- */
    uint8_t shifted_tail[sizeof(realtime_frame)] = { 0 };
    memcpy(shifted_tail, realtime_frame, sizeof(shifted_tail));
    shifted_tail[31] = 0x00U;
    assert(esp_bms_ctrl_apt_legacy_feed(shifted_tail, sizeof(shifted_tail)));

    /* --- 电机温度候选超范围时置空 --- */
    uint8_t hot[sizeof(realtime_frame)] = { 0 };
    memcpy(hot, realtime_frame, sizeof(hot));
    hot[12] = 0xF0U; /* 240 °C，超出 -40..180 */
    assert(esp_bms_ctrl_apt_legacy_feed(hot, sizeof(hot)));
    esp_bms_ctrl_apt_legacy_telemetry(&telemetry);
    assert(!telemetry.motor_temp_valid && telemetry.motor_temp_c == 0);

    /* --- reset 清空快照 --- */
    esp_bms_ctrl_apt_legacy_reset();
    esp_bms_ctrl_apt_legacy_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && telemetry.voltage_deci_v == 0U);
    assert(!telemetry.current_valid && telemetry.current_centi_a == 0);
    assert(!telemetry.rpm_valid && telemetry.rpm == 0U);
    assert(!telemetry.gear_valid && telemetry.gear == 0U);
    assert(!telemetry.fault_valid && telemetry.fault_code == 0U);

    printf("apt_legacy ctrl protocol selftest OK\n");
    return 0;
}
