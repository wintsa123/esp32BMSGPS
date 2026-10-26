#include "esp_bms_ctrl_ausi.h"

#include <assert.h>
#include <string.h>

/* 44 字节实时逻辑载荷被拆成 3 帧（18+18+8），帧序号 0/1/2。 */
static const uint8_t realtime_fragment_0[] = {
    0xAAU, 0x16U, 0x00U, 0x33U, 0x01U, 0x01U, 0x09U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U,
    0x10U, 0x02U, 0x7FU, 0x3AU, 0xA0U, 0x5AU, 0x2AU, 0x76U, 0x30U, 0x75U, 0x18U, 0x4EU,
};
static const uint8_t realtime_fragment_1[] = {
    0xAAU, 0x16U, 0x01U, 0x33U, 0x1AU, 0x04U, 0x60U, 0x04U, 0x13U, 0x01U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0xFEU, 0x8AU,
};
static const uint8_t realtime_fragment_2[] = {
    0xAAU, 0x0CU, 0x02U, 0x31U, 0xDCU, 0x05U, 0x00U, 0x00U,
    0x78U, 0x00U, 0xC0U, 0x12U, 0x51U, 0x4BU,
};

/* 握手应答：载荷 {1,31,0,0,0,0,0,0}。 */
static const uint8_t handshake_ack[] = {
    0xAAU, 0x0CU, 0x00U, 0x31U, 0x01U, 0x1FU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x4FU, 0x28U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_ausi_reset();

    /* 开启帧：固定握手。 */
    assert(esp_bms_ctrl_ausi_build_start(0U, frame, &frame_len) && frame_len == 14U);
    assert(memcmp(frame, (const uint8_t[]){ 0xAAU, 0x0CU, 0x00U, 0x31U, 0x01U, 0x1FU, 0x01U, 0x41U, 0x55U,
                                            0x02U, 0x00U, 0x07U, 0x83U, 0x38U },
                  frame_len) == 0);

    /* 未握手时没有可发的心跳帧。 */
    assert(!esp_bms_ctrl_ausi_build_poll(0U, 0U, frame, &frame_len) && frame_len == 0U);

    /* 未握手前推来的实时帧被忽略。 */
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_0, sizeof(realtime_fragment_0)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_1, sizeof(realtime_fragment_1)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_2, sizeof(realtime_fragment_2)));

    /* 握手应答后进入在线态。 */
    assert(!esp_bms_ctrl_ausi_feed(handshake_ack, sizeof(handshake_ack)));

    /* 心跳帧：序号 0/prev 0，再下一个序号 1/prev 0。 */
    assert(esp_bms_ctrl_ausi_build_poll(0U, 0U, frame, &frame_len) && frame_len == 14U);
    assert(memcmp(frame, (const uint8_t[]){ 0xAAU, 0x0CU, 0x00U, 0x31U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U,
                                            0x00U, 0x00U, 0x00U, 0xB1U, 0x29U },
                  frame_len) == 0);
    assert(esp_bms_ctrl_ausi_build_poll(1U, 400U, frame, &frame_len) && frame_len == 14U);
    assert(frame[6U] == 0x01U && frame[7U] == 0x00U);
    assert(memcmp(frame, (const uint8_t[]){ 0xAAU, 0x0CU, 0x00U, 0x31U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U,
                                            0x00U, 0x00U, 0x00U, 0xB0U, 0xF8U },
                  frame_len) == 0);

    /* 分片到达：连续两片不够，末片到达才解析。 */
    esp_bms_ctrl_ausi_reset();
    assert(esp_bms_ctrl_ausi_build_start(1U, frame, &frame_len));
    assert(!esp_bms_ctrl_ausi_feed(handshake_ack, sizeof(handshake_ack)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_0, sizeof(realtime_fragment_0)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_1, sizeof(realtime_fragment_1)));
    assert(esp_bms_ctrl_ausi_feed(realtime_fragment_2, sizeof(realtime_fragment_2)));
    esp_bms_ctrl_ausi_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 528U);
    assert(telemetry.current_valid && telemetry.current_centi_a == -250);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 52);
    assert(telemetry.power_valid && telemetry.power_w == -132);
    assert(telemetry.fault_valid && telemetry.fault_code == 2U);

    /* 校验错误帧被拒：改首片 CRC 高字节，后续分片因序号对不上也一并被拒。 */
    uint8_t corrupt[sizeof(realtime_fragment_0)] = { 0 };
    memcpy(corrupt, realtime_fragment_0, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1U] ^= 0x01U;
    esp_bms_ctrl_ausi_reset();
    assert(esp_bms_ctrl_ausi_build_start(0U, frame, &frame_len));
    assert(!esp_bms_ctrl_ausi_feed(handshake_ack, sizeof(handshake_ack)));
    assert(!esp_bms_ctrl_ausi_feed(corrupt, sizeof(corrupt)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_1, sizeof(realtime_fragment_1)));
    assert(!esp_bms_ctrl_ausi_feed(realtime_fragment_2, sizeof(realtime_fragment_2)));
    memset(&telemetry, 0, sizeof(telemetry));
    esp_bms_ctrl_ausi_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && !telemetry.rpm_valid && !telemetry.current_valid);

    return 0;
}
