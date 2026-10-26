/* apt_a8（APT A8）控制器协议自测：帧编码、字节和查表校验、分片/粘包、字段解码。
 *
 * gcc -std=c11 -Wall -Wextra -Werror \
 *   -I components/esp_bms_controller_protocol/include \
 *   -I components/esp_bms_controller_protocol/protocols/apt_a8 \
 *   tests/apt_a8_ctrl_protocol_selftest.c \
 *   components/esp_bms_controller_protocol/protocols/apt_a8/esp_bms_ctrl_apt_a8.c \
 *   -o /tmp/apt_a8_test && /tmp/apt_a8_test
 */

#include "esp_bms_ctrl_apt_a8.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A8 没有握手帧：订阅后直接发实时读取帧；0x1B 变体是 Motexa 的备用读取帧。 */
static const uint8_t realtime_request[] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x1FU, 0x3EU, 0x4FU,
};
static const uint8_t variant_1b_request[] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x1BU, 0x8CU, 0x95U,
};

/* 实时回包（31 字节 payload）：52.0V / 25.0A / 3000rpm / 10°C / 20°C /
 * 相线峰值 40.0A / 转把 1.28V / 档位 3 / 当前故障位图 0x0004 */
static const uint8_t realtime_response[] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x1FU, 0x08U, 0x02U, 0xFAU, 0x00U, 0xB8U, 0x0BU, 0x41U,
    0x4BU, 0x90U, 0x01U, 0x80U, 0x00U, 0x23U, 0x01U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x64U,
    0x00U, 0x62U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x00U, 0x00U, 0x00U, 0xDCU, 0x05U, 0xCDU, 0xD1U,
};

/* 19 字节 payload 的现场变体（字段布局相同，尾部没有 iq/故障历史） */
static const uint8_t realtime_response_short[] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x13U, 0x08U, 0x02U, 0xFAU, 0x00U, 0xB8U, 0x0BU, 0x41U,
    0x4BU, 0x90U, 0x01U, 0x80U, 0x00U, 0x23U, 0x01U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x37U,
    0x3BU,
};

/* 参数组 01 回包：只做帧验证，不参与遥测 */
static const uint8_t parameter_response[] = {
    0xA8U, 0x02U, 0x01U, 0x22U, 0x01U, 0x11U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
    0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU, 0x10U, 0x11U, 0xB2U, 0xB4U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_apt_a8_reset();

    /* --- 开启帧：首帧就是实时读取（无握手），失败重试时轮换 0x1B 变体 --- */
    assert(esp_bms_ctrl_apt_a8_build_start(0U, frame, &frame_len));
    assert(frame_len == sizeof(realtime_request));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_a8_build_start(1U, frame, &frame_len));
    assert(frame_len == sizeof(variant_1b_request));
    assert(memcmp(frame, variant_1b_request, sizeof(variant_1b_request)) == 0);
    /* 越界 attempt 不能崩，且仍在两帧之间轮换 */
    assert(esp_bms_ctrl_apt_a8_build_start(255U, frame, &frame_len));
    assert(memcmp(frame, variant_1b_request, sizeof(variant_1b_request)) == 0);

    /* --- 轮询帧：每 4 拍插一次 0x1B 变体 --- */
    assert(esp_bms_ctrl_apt_a8_build_poll(0U, 0U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_a8_build_poll(2U, 400U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_apt_a8_build_poll(3U, 600U, frame, &frame_len));
    assert(memcmp(frame, variant_1b_request, sizeof(variant_1b_request)) == 0);

    /* --- 实时回包解码 --- */
    assert(esp_bms_ctrl_apt_a8_feed(realtime_response, sizeof(realtime_response)));
    esp_bms_ctrl_apt_a8_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 520U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 2500);
    assert(telemetry.rpm_valid && telemetry.rpm == 3000U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 10);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 20);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 13U);
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.fault_valid && telemetry.fault_code == 4U);
    assert(telemetry.power_valid && telemetry.power_w == 1300);
    assert(!telemetry.speed_valid);

    /* --- 校验错误帧：字节和表不匹配，必须丢弃 --- */
    uint8_t corrupted[sizeof(realtime_response)] = { 0 };
    memcpy(corrupted, realtime_response, sizeof(corrupted));
    corrupted[sizeof(corrupted) - 2U] ^= 0xFFU;
    assert(!esp_bms_ctrl_apt_a8_feed(corrupted, sizeof(corrupted)));

    /* --- 短回包（19 字节 payload）也要能解出基础字段 --- */
    assert(esp_bms_ctrl_apt_a8_feed(realtime_response_short, sizeof(realtime_response_short)));
    esp_bms_ctrl_apt_a8_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 520U && telemetry.rpm == 3000U &&
           telemetry.throttle_percent == 13U);

    /* --- 分片到达：前半包返回 false，拼完才解析 --- */
    assert(!esp_bms_ctrl_apt_a8_feed(realtime_response, 10U));
    assert(esp_bms_ctrl_apt_a8_feed(realtime_response + 10U, sizeof(realtime_response) - 10U));

    /* --- 粘包：实时帧 + 参数组帧一起到达 --- */
    uint8_t joined[sizeof(realtime_response) + sizeof(parameter_response)] = { 0 };
    memcpy(joined, realtime_response, sizeof(realtime_response));
    memcpy(joined + sizeof(realtime_response), parameter_response, sizeof(parameter_response));
    assert(esp_bms_ctrl_apt_a8_feed(joined, sizeof(joined)));

    /* --- 参数组回包本身也是有效帧（但不改遥测） --- */
    assert(esp_bms_ctrl_apt_a8_feed(parameter_response, sizeof(parameter_response)));

    /* --- 帧头前噪声必须被跳过 --- */
    uint8_t noisy[sizeof(realtime_response) + 2U] = { 0x00U, 0xFFU };
    memcpy(noisy + 2U, realtime_response, sizeof(realtime_response));
    assert(esp_bms_ctrl_apt_a8_feed(noisy, sizeof(noisy)));

    /* --- 未映射命令不算有效帧 --- */
    uint8_t unmapped[sizeof(realtime_response)] = { 0 };
    memcpy(unmapped, realtime_response, sizeof(unmapped));
    assert(!esp_bms_ctrl_apt_a8_feed(unmapped, 8U)); /* 只有半帧 */

    /* --- reset 清空快照 --- */
    esp_bms_ctrl_apt_a8_reset();
    esp_bms_ctrl_apt_a8_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && telemetry.voltage_deci_v == 0U);
    assert(!telemetry.current_valid && telemetry.current_centi_a == 0);
    assert(!telemetry.gear_valid && telemetry.gear == 0U);
    assert(!telemetry.fault_valid && telemetry.fault_code == 0U);

    printf("apt_a8 ctrl protocol selftest OK\n");
    return 0;
}
