#include "esp_bms_ctrl_jiazhou.h"

#include <assert.h>
#include <string.h>

/* A2 发现应答：MessagePack ["DS11_1", 0]（节点 ID = 0）。 */
static const uint8_t discovery_response[] = {
    0x60U, 0xA2U, 0x00U, 0x09U, 0x92U, 0xA6U, 0x44U, 0x53U, 0x31U, 0x31U, 0x5FU, 0x31U, 0x00U,
};
/* A0 设备信息应答：MessagePack ["A", 1, 2]。 */
static const uint8_t device_info_response[] = {
    0x60U, 0xA0U, 0x00U, 0x05U, 0x93U, 0xA1U, 0x41U, 0x01U, 0x02U,
};
/* A3 DCF 应答：MessagePack [2, 0]。 */
static const uint8_t dcf_response[] = {
    0x60U, 0xA3U, 0x00U, 0x03U, 0x92U, 0x02U, 0x00U,
};
/* A4 Monitor 应答：8 个整数 = 电压 48V / 电流 25A / 3200rpm / 电机 52℃ /
 * 相电流 250A / MOS 45℃ / 故障 5 / 档位 3。 */
static const uint8_t monitor_response[] = {
    0x60U, 0xA4U, 0x00U, 0x0CU, 0x98U, 0x30U, 0x19U, 0xCDU,
    0x0CU, 0x80U, 0x34U, 0xCCU, 0xFAU, 0x2DU, 0x05U, 0x03U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_jiazhou_reset();

    /* 开启帧按 attempt 分段：A2 发现 → A0 设备信息 → A3 DCF → A4 Monitor。 */
    assert(esp_bms_ctrl_jiazhou_build_start(0U, frame, &frame_len) && frame_len == 6U);
    assert(memcmp(frame, (const uint8_t[]){ 0x60U, 0xA2U, 0x00U, 0x02U, 0x01U, 0xF4U }, frame_len) == 0);
    assert(esp_bms_ctrl_jiazhou_build_start(1U, frame, &frame_len) && frame_len == 6U);
    assert(memcmp(frame, (const uint8_t[]){ 0x60U, 0xA0U, 0x00U, 0x02U, 0x00U, 0x00U }, frame_len) == 0);
    assert(esp_bms_ctrl_jiazhou_build_start(2U, frame, &frame_len) && frame_len == 6U);
    assert(memcmp(frame, (const uint8_t[]){ 0x60U, 0xA3U, 0x00U, 0x02U, 0x01U, 0x00U }, frame_len) == 0);
    assert(esp_bms_ctrl_jiazhou_build_start(3U, frame, &frame_len) && frame_len == 36U);
    assert(frame[0U] == 0x60U && frame[1U] == 0xA4U && frame[2U] == 0x00U && frame[3U] == 0x20U);
    /* 8 个参数 × 4 字节：0x0448 0x0450 0x0452 0x0456 0x0458 0x0462 0x0465 0x0463。 */
    assert(memcmp(frame + 4U, (const uint8_t[]){ 0x04U, 0x48U, 0x00U, 0x00U, 0x04U, 0x50U, 0x00U, 0x00U },
                  8U) == 0);
    assert(frame[4U + 24U] == 0x04U && frame[5U + 24U] == 0x65U);
    assert(frame[4U + 28U] == 0x04U && frame[5U + 28U] == 0x63U);

    /* 未拿到 A2 应答前，轮询继续发现帧，不跳步。 */
    assert(esp_bms_ctrl_jiazhou_build_poll(0U, 0U, frame, &frame_len) && frame[1U] == 0xA2U);

    /* 握手阶段推进。 */
    assert(!esp_bms_ctrl_jiazhou_feed(discovery_response, sizeof(discovery_response)));
    assert(esp_bms_ctrl_jiazhou_build_poll(1U, 1500U, frame, &frame_len) && frame[1U] == 0xA0U);
    assert(!esp_bms_ctrl_jiazhou_feed(device_info_response, sizeof(device_info_response)));
    assert(esp_bms_ctrl_jiazhou_build_poll(2U, 3000U, frame, &frame_len) && frame[1U] == 0xA3U);
    assert(!esp_bms_ctrl_jiazhou_feed(dcf_response, sizeof(dcf_response)));
    assert(esp_bms_ctrl_jiazhou_build_poll(3U, 4500U, frame, &frame_len) && frame[1U] == 0xA4U &&
           frame_len == 36U);

    /* Monitor 应答 → 遥测。 */
    assert(esp_bms_ctrl_jiazhou_feed(monitor_response, sizeof(monitor_response)));
    esp_bms_ctrl_jiazhou_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 480U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 2500);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 52);
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.power_valid && telemetry.power_w == 1200);

    /* 分片到达：前 8 字节 + 后 8 字节。 */
    esp_bms_ctrl_jiazhou_reset();
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_ctrl_jiazhou_feed(monitor_response, 8U));
    esp_bms_ctrl_jiazhou_telemetry(&telemetry);
    assert(!telemetry.rpm_valid);
    assert(esp_bms_ctrl_jiazhou_feed(monitor_response + 8U, sizeof(monitor_response) - 8U));
    esp_bms_ctrl_jiazhou_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U && telemetry.voltage_deci_v == 480U);

    /* 结构错误帧被拒：信封无 CRC，源码只做长度/MessagePack 结构校验；
     * 这里用 7 元素的 A4 数组触发 response_count_mismatch。 */
    const uint8_t mismatch[] = {
        0x60U, 0xA4U, 0x00U, 0x0CU, 0x97U, 0x30U, 0x19U, 0xCDU,
        0x0CU, 0x80U, 0x34U, 0xCDU, 0x00U, 0xFAU, 0x2DU, 0x05U,
    };
    esp_bms_ctrl_jiazhou_reset();
    assert(!esp_bms_ctrl_jiazhou_feed(mismatch, sizeof(mismatch)));
    memset(&telemetry, 0, sizeof(telemetry));
    esp_bms_ctrl_jiazhou_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid && !telemetry.current_valid);

    return 0;
}
