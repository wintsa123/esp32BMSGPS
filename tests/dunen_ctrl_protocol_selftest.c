/* dunen（顿恩 DN）控制器协议自测：Modbus RTU Function 03 请求/回包、CRC、
 * 分片/粘包、14 组 32 位实时数据解码。
 *
 * gcc -std=c11 -Wall -Wextra -Werror \
 *   -I components/esp_bms_controller_protocol/include \
 *   -I components/esp_bms_controller_protocol/protocols/dunen \
 *   tests/dunen_ctrl_protocol_selftest.c \
 *   components/esp_bms_controller_protocol/protocols/dunen/esp_bms_ctrl_dunen.c \
 *   -o /tmp/dunen_test && /tmp/dunen_test
 */

#include "esp_bms_ctrl_dunen.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 0x03FE / 28 字：实时读取（Motexa 的 dn-realtime） */
static const uint8_t realtime_request[] = {
    0x01U, 0x03U, 0x03U, 0xFEU, 0x00U, 0x1CU, 0x25U, 0xB7U,
};
/* 0xFFEC / 15 字：型号；0xFFEE / 2：软件版本；0xFFED / 2：硬件版本 */
static const uint8_t identity_model_request[] = {
    0x01U, 0x03U, 0xFFU, 0xECU, 0x00U, 0x0FU, 0xF4U, 0x2FU,
};
static const uint8_t identity_software_request[] = {
    0x01U, 0x03U, 0xFFU, 0xEEU, 0x00U, 0x02U, 0x94U, 0x2AU,
};
static const uint8_t identity_hardware_request[] = {
    0x01U, 0x03U, 0xFFU, 0xEDU, 0x00U, 0x02U, 0x64U, 0x2AU,
};
/* 参数轮转读：parameters-basic 首地址 492 / 2 字 */
static const uint8_t parameter_request[] = {
    0x01U, 0x03U, 0x01U, 0xECU, 0x00U, 0x02U, 0x04U, 0x02U,
};

/* 61 字节实时回包：3000rpm / 52V / 相线 400A / 电控 65°C / 电机 75°C /
 * 转把 65/256*5≈1.27V / 母线 25A / 故障号 7 */
static const uint8_t realtime_response[] = {
    0x01U, 0x03U, 0x38U, 0x00U, 0x00U, 0x0BU, 0xB8U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x34U, 0x00U, 0x41U, 0x01U, 0x90U, 0x00U, 0x00U, 0x00U, 0x41U, 0x00U, 0x00U, 0x00U,
    0x4BU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x19U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x07U, 0x13U, 0x81U,
};

/* 30 字节身份回包（型号文本） */
static const uint8_t identity_response[] = {
    0x01U, 0x03U, 0x1EU, 0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U,
    0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU, 0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U,
    0x17U, 0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x5CU, 0x21U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_dunen_reset();

    /* --- 开启帧：先实时读，失败重试轮换身份读取（Motexa 的顺序：型号→软件→硬件） --- */
    assert(esp_bms_ctrl_dunen_build_start(0U, frame, &frame_len));
    assert(frame_len == sizeof(realtime_request));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_start(1U, frame, &frame_len));
    assert(frame_len == sizeof(identity_model_request));
    assert(memcmp(frame, identity_model_request, sizeof(identity_model_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_start(2U, frame, &frame_len));
    assert(memcmp(frame, identity_software_request, sizeof(identity_software_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_start(3U, frame, &frame_len));
    assert(memcmp(frame, identity_hardware_request, sizeof(identity_hardware_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_start(4U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);

    /* --- 轮询帧：默认实时读；每 16 拍轮转一个已批准的参数地址 --- */
    assert(esp_bms_ctrl_dunen_build_poll(0U, 0U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_poll(14U, 3500U, frame, &frame_len));
    assert(memcmp(frame, realtime_request, sizeof(realtime_request)) == 0);
    assert(esp_bms_ctrl_dunen_build_poll(15U, 3750U, frame, &frame_len));
    assert(frame_len == sizeof(parameter_request));
    assert(memcmp(frame, parameter_request, sizeof(parameter_request)) == 0);

    /* --- 实时回包解码 --- */
    assert(esp_bms_ctrl_dunen_feed(realtime_response, sizeof(realtime_response)));
    esp_bms_ctrl_dunen_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3000U);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 520U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 2500);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 65);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 75);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 12U);
    assert(telemetry.fault_valid && telemetry.fault_code == 7U);
    assert(telemetry.power_valid && telemetry.power_w == 1300);
    /* 速度：3000rpm * 1414mm * 600 / 1e6 = 2545 (0.1km/h) */
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 2545U);

    /* --- CRC 错误帧：必须丢弃，且不能污染快照 --- */
    uint8_t corrupted[sizeof(realtime_response)] = { 0 };
    memcpy(corrupted, realtime_response, sizeof(corrupted));
    corrupted[sizeof(corrupted) - 1U] ^= 0xFFU;
    assert(!esp_bms_ctrl_dunen_feed(corrupted, sizeof(corrupted)));

    /* --- 分片到达：前 40 字节不够一帧，拼完才解析 --- */
    assert(!esp_bms_ctrl_dunen_feed(realtime_response, 40U));
    assert(esp_bms_ctrl_dunen_feed(realtime_response + 40U, sizeof(realtime_response) - 40U));

    /* --- 粘包：实时回包 + 身份回包 --- */
    uint8_t joined[sizeof(realtime_response) + sizeof(identity_response)] = { 0 };
    memcpy(joined, realtime_response, sizeof(realtime_response));
    memcpy(joined + sizeof(realtime_response), identity_response, sizeof(identity_response));
    assert(esp_bms_ctrl_dunen_feed(joined, sizeof(joined)));

    /* --- 身份回包本身是有效帧，但不改遥测 --- */
    assert(esp_bms_ctrl_dunen_feed(identity_response, sizeof(identity_response)));
    esp_bms_ctrl_dunen_telemetry(&telemetry);
    assert(telemetry.rpm == 3000U && telemetry.voltage_deci_v == 520U);

    /* --- 帧头前噪声必须滑动跳过 --- */
    uint8_t noisy[sizeof(realtime_response) + 3U] = { 0xAAU, 0x00U, 0x01U };
    memcpy(noisy + 3U, realtime_response, sizeof(realtime_response));
    assert(esp_bms_ctrl_dunen_feed(noisy, sizeof(noisy)));

    /* --- 故障号取非零字：把组 13 改成低字非零 / 高字为 0 --- */
    uint8_t fault_low[sizeof(realtime_response)] = { 0 };
    memcpy(fault_low, realtime_response, sizeof(fault_low));
    /* 组 13 = payload[52..55] = 帧[55..58]，CRC 在帧[59..60] */
    fault_low[55] = 0x00U;
    fault_low[56] = 0x09U; /* wordA = 9（低字） */
    fault_low[57] = 0x00U;
    fault_low[58] = 0x00U; /* wordB = 0 */
    /* 重算 CRC（发送侧校验要自洽） */
    unsigned crc = 0xFFFFU;
    for (size_t index = 0U; index + 2U < sizeof(fault_low); ++index) {
        crc ^= fault_low[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
        }
    }
    fault_low[sizeof(fault_low) - 2U] = (uint8_t)(crc & 0xFFU);
    fault_low[sizeof(fault_low) - 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
    assert(esp_bms_ctrl_dunen_feed(fault_low, sizeof(fault_low)));
    esp_bms_ctrl_dunen_telemetry(&telemetry);
    assert(telemetry.fault_valid && telemetry.fault_code == 9U);

    /* --- reset 清空快照 --- */
    esp_bms_ctrl_dunen_reset();
    esp_bms_ctrl_dunen_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && telemetry.rpm == 0U);
    assert(!telemetry.voltage_valid && telemetry.voltage_deci_v == 0U);
    assert(!telemetry.speed_valid && telemetry.speed_deci_kmh == 0U);
    assert(!telemetry.fault_valid && telemetry.fault_code == 0U);

    printf("dunen ctrl protocol selftest OK\n");
    return 0;
}
