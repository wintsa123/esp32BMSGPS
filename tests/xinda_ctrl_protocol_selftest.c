#include "esp_bms_ctrl_xinda.h"

#include <assert.h>
#include <string.h>

/* 25 字节实时帧：故障码 5 / MOS 45℃ / 母线 5280(=52.8V) / 电流 -250(=-25.0A) /
 * 转速原始 60000（极对数 30 → 2000rpm）/ 相电流 250A / 电机 52℃ / 转把 27 / XOR。 */
static const uint8_t realtime_frame[] = {
    0xECU, 0x9FU, 0x05U, 0x55U, 0x14U, 0xA0U, 0xFFU, 0x06U, 0xEAU, 0x60U, 0x00U, 0xFAU, 0x01U,
    0x02U, 0x1EU, 0x5CU, 0x1BU, 0x11U, 0x22U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x77U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_xinda_reset();

    /* 开启帧：单字节设备类型请求 0x96（全服务探测型号，握手由本模块承担）。 */
    assert(esp_bms_ctrl_xinda_build_start(0U, frame, &frame_len) && frame_len == 1U && frame[0U] == 0x96U);
    assert(esp_bms_ctrl_xinda_build_start(3U, frame, &frame_len) && frame[0U] == 0x96U);

    /* 轮询：只在节拍边界补发流开关请求。 */
    assert(esp_bms_ctrl_xinda_build_poll(0U, 0U, frame, &frame_len) && frame_len == 1U && frame[0U] == 0x96U);
    assert(!esp_bms_ctrl_xinda_build_poll(1U, 200U, frame, &frame_len) && frame_len == 0U);
    assert(!esp_bms_ctrl_xinda_build_poll(14U, 2800U, frame, &frame_len) && frame_len == 0U);
    assert(esp_bms_ctrl_xinda_build_poll(15U, 3000U, frame, &frame_len) && frame_len == 1U);

    /* 正例。 */
    assert(esp_bms_ctrl_xinda_feed(realtime_frame, sizeof(realtime_frame)));
    esp_bms_ctrl_xinda_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 528U);
    assert(telemetry.current_valid && telemetry.current_centi_a == -2500);
    assert(telemetry.rpm_valid && telemetry.rpm == 2000U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 52);
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    assert(telemetry.power_valid && telemetry.power_w == -1320);

    /* 分片到达：先 10 字节，再补 15 字节。 */
    esp_bms_ctrl_xinda_reset();
    memset(&telemetry, 0, sizeof(telemetry));
    assert(!esp_bms_ctrl_xinda_feed(realtime_frame, 10U));
    esp_bms_ctrl_xinda_telemetry(&telemetry);
    assert(!telemetry.rpm_valid);
    assert(esp_bms_ctrl_xinda_feed(realtime_frame + 10U, sizeof(realtime_frame) - 10U));
    esp_bms_ctrl_xinda_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 2000U && telemetry.voltage_deci_v == 528U);

    /* 校验错误帧被拒（XOR 字节改一位）。 */
    uint8_t corrupt[sizeof(realtime_frame)] = { 0 };
    memcpy(corrupt, realtime_frame, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1U] ^= 0x01U;
    esp_bms_ctrl_xinda_reset();
    assert(!esp_bms_ctrl_xinda_feed(corrupt, sizeof(corrupt)));
    memset(&telemetry, 0, sizeof(telemetry));
    esp_bms_ctrl_xinda_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid && !telemetry.fault_valid);

    /* 噪点前缀不影响后续帧同步。 */
    esp_bms_ctrl_xinda_reset();
    const uint8_t noisy[3] = { 0x54U, 0x01U, 0x00U };
    assert(!esp_bms_ctrl_xinda_feed(noisy, sizeof(noisy)));
    assert(esp_bms_ctrl_xinda_feed(realtime_frame, sizeof(realtime_frame)));

    return 0;
}
