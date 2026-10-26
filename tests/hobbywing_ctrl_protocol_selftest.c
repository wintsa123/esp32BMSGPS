#include "esp_bms_ctrl_hobbywing.h"

#include <assert.h>
#include <string.h>

/* 实时应答（功能号 1，6 个寄存器）：转速 3200 / 图表 0 / 母线 528(52.8V) /
 * 电流 0xFFE7(-2.5A) / 相电流 250(25.0A) / MOS 45℃。应答本身无 CRC。 */
static const uint8_t realtime_response[] = {
    0x0AU, 0x55U, 0x10U, 0x00U, 0x00U, 0x01U, 0x80U, 0x0CU, 0x00U, 0x00U,
    0x10U, 0x02U, 0xE7U, 0xFFU, 0xFAU, 0x00U, 0x2DU, 0x00U, 0x00U, 0x00U,
};
/* 整车状态（功能号 12）：状态位图 0x0021 / 档位 3 / 油门 55%。 */
static const uint8_t status_response[] = {
    0x0AU, 0x55U, 0x10U, 0x00U, 0x00U, 0x0CU, 0x21U, 0x00U, 0x03U, 0x00U, 0x37U, 0x00U, 0x00U, 0x00U,
};
/* 故障（功能号 7）：电机温度 52℃ / 故障位图 1 = 0x0005 / 位图 2 = 0。 */
static const uint8_t fault_response[] = {
    0x0AU, 0x55U, 0x10U, 0x00U, 0x00U, 0x07U, 0x34U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_hobbywing_reset();

    /* 开启帧 = 实时读取请求，CRC 覆盖 01 01 02 01 06。 */
    assert(esp_bms_ctrl_hobbywing_build_start(0U, frame, &frame_len) && frame_len == 9U);
    assert(memcmp(frame, (const uint8_t[]){ 0x0AU, 0x55U, 0x01U, 0x01U, 0x02U, 0x01U, 0x06U, 0x38U, 0x6EU },
                  frame_len) == 0);

    /* 轮询节奏：实时/整车状态/车辆数据各占 200ms 槽，5s 边界发故障读取。 */
    assert(esp_bms_ctrl_hobbywing_build_poll(0U, 0U, frame, &frame_len) && frame_len == 9U && frame[5U] == 0x01U);
    assert(esp_bms_ctrl_hobbywing_build_poll(1U, 200U, frame, &frame_len) && frame[5U] == 0x0CU);
    assert(memcmp(frame, (const uint8_t[]){ 0x0AU, 0x55U, 0x01U, 0x01U, 0x02U, 0x0CU, 0x03U, 0xFCU, 0xFDU },
                  frame_len) == 0);
    assert(esp_bms_ctrl_hobbywing_build_poll(2U, 400U, frame, &frame_len) && frame[5U] == 0x0AU);
    assert(!esp_bms_ctrl_hobbywing_build_poll(3U, 600U, frame, &frame_len) && frame_len == 0U);
    assert(esp_bms_ctrl_hobbywing_build_poll(25U, 5000U, frame, &frame_len) && frame[5U] == 0x07U);

    /* 实时应答。 */
    assert(esp_bms_ctrl_hobbywing_feed(realtime_response, sizeof(realtime_response)));
    esp_bms_ctrl_hobbywing_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 528U);
    assert(telemetry.current_valid && telemetry.current_centi_a == -250);
    assert(telemetry.power_valid && telemetry.power_w == -132);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(!telemetry.motor_temp_valid && !telemetry.gear_valid);

    /* 整车状态 + 故障帧补全档位/油门/电机温度。 */
    assert(esp_bms_ctrl_hobbywing_feed(status_response, sizeof(status_response)));
    assert(esp_bms_ctrl_hobbywing_feed(fault_response, sizeof(fault_response)));
    esp_bms_ctrl_hobbywing_telemetry(&telemetry);
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 55U);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 52);
    assert(telemetry.fault_valid && telemetry.fault_code == 0x0005U);

    /* 分片到达：前 10 字节 + 后 10 字节。 */
    esp_bms_ctrl_hobbywing_reset();
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_ctrl_hobbywing_feed(realtime_response, 10U));
    esp_bms_ctrl_hobbywing_telemetry(&telemetry);
    assert(!telemetry.rpm_valid);
    assert(esp_bms_ctrl_hobbywing_feed(realtime_response + 10U, sizeof(realtime_response) - 10U));
    esp_bms_ctrl_hobbywing_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);

    /* 校验/结构错误帧被拒：应答无 CRC，未知功能号与错误帧头都按结构校验拒绝。 */
    uint8_t unknown_feature[sizeof(realtime_response)] = { 0 };
    memcpy(unknown_feature, realtime_response, sizeof(unknown_feature));
    unknown_feature[5U] = 0xEEU;
    esp_bms_ctrl_hobbywing_reset();
    assert(!esp_bms_ctrl_hobbywing_feed(unknown_feature, sizeof(unknown_feature)));
    memset(&telemetry, 0, sizeof(telemetry));
    esp_bms_ctrl_hobbywing_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid);

    uint8_t bad_sync[sizeof(realtime_response)] = { 0 };
    memcpy(bad_sync, realtime_response, sizeof(bad_sync));
    bad_sync[2U] = 0x11U;
    esp_bms_ctrl_hobbywing_reset();
    assert(!esp_bms_ctrl_hobbywing_feed(bad_sync, sizeof(bad_sync)));

    return 0;
}
