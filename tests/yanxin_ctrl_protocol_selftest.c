/* yanxin（雁鑫 VITMC）控制器协议自测：帧编码、CRC、分片/粘包、字段解码。
 *
 * gcc -std=c11 -Wall -Wextra -Werror \
 *   -I components/esp_bms_controller_protocol/include \
 *   -I components/esp_bms_controller_protocol/protocols/yanxin \
 *   tests/yanxin_ctrl_protocol_selftest.c \
 *   components/esp_bms_controller_protocol/protocols/yanxin/esp_bms_ctrl_yanxin.c \
 *   -o /tmp/yanxin_test && /tmp/yanxin_test
 */

#include "esp_bms_ctrl_yanxin.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 握手帧：attempt 0 → scheme 0x1F + customerId 0；attempt 1 → 0x0F + 0；attempt 2 → 0x1F + 1 */
static const uint8_t handshake_attempt0[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x16U, 0xB9U,
};
static const uint8_t handshake_attempt1[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x0FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x07U, 0x78U,
};
static const uint8_t handshake_attempt2[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0xD7U, 0x79U,
};
/* 探测表用尽后的旧协议实时探测帧 payload {01,01,00,...} */
static const uint8_t legacy_probe[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xE8U, 0xB8U,
};

static const uint8_t realtime_request_step0[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xE8U, 0xB8U,
};
static const uint8_t speed_context_request[] = {
    0xAAU, 0x0AU, 0x00U, 0x00U, 0x01U, 0x05U, 0x10U, 0x00U, 0x11U, 0x00U, 0xB1U, 0x11U,
};

/* 实时回包：52.0V / 25.0A（chip_io bit22 使能）/ 3000rpm / 10°C / 20°C / 1.28V 转把 / 故障位图 5 */
static const uint8_t realtime_response[] = {
    0xAAU, 0x2CU, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x08U, 0x02U,
    0x92U, 0x3BU, 0xD8U, 0x59U, 0xE8U, 0x80U, 0x18U, 0x79U, 0xBCU, 0x02U, 0x20U, 0x03U, 0x80U, 0x00U,
    0x02U, 0x00U, 0x01U, 0x00U, 0x10U, 0x00U, 0x40U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0xDCU, 0x05U,
    0x07U, 0x00U, 0x41U, 0xA6U,
};

/* 速度参数回包：W=800，R raw=500（ratio 1.25） */
static const uint8_t speed_context_response[] = {
    0xAAU, 0x0AU, 0x00U, 0x00U, 0x01U, 0x05U, 0x20U, 0x03U, 0xF4U, 0x01U, 0xC5U, 0x41U,
};

static const uint8_t handshake_accept[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x16U, 0xB9U,
};
static const uint8_t handshake_reject_try_next[] = {
    0xAAU, 0x0CU, 0x00U, 0x00U, 0x01U, 0x00U, 0x1FU, 0x05U, 0x00U, 0x00U, 0x00U, 0x00U, 0x36U, 0x17U,
};

/* 同一个实时逻辑包拆成两片（flags bit1 表示后续还有） */
static const uint8_t realtime_fragment_a[] = {
    0xAAU, 0x18U, 0x00U, 0x02U, 0x01U, 0x01U, 0x00U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x08U, 0x02U,
    0x92U, 0x3BU, 0xD8U, 0x59U, 0xE8U, 0x80U, 0x18U, 0x79U, 0xBCU, 0x02U, 0x9FU, 0x5DU,
};
static const uint8_t realtime_fragment_b[] = {
    0xAAU, 0x18U, 0x01U, 0x00U, 0x20U, 0x03U, 0x80U, 0x00U, 0x02U, 0x00U, 0x01U, 0x00U, 0x10U, 0x00U,
    0x40U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0xDCU, 0x05U, 0x07U, 0x00U, 0x70U, 0xB0U,
};

int main(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t frame_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    esp_bms_ctrl_yanxin_reset();

    /* --- 开启帧：三档探测轮换，再退化成旧协议探测帧 --- */
    assert(esp_bms_ctrl_yanxin_build_start(0U, frame, &frame_len));
    assert(frame_len == sizeof(handshake_attempt0));
    assert(memcmp(frame, handshake_attempt0, sizeof(handshake_attempt0)) == 0);

    assert(esp_bms_ctrl_yanxin_build_start(1U, frame, &frame_len));
    assert(frame_len == sizeof(handshake_attempt1));
    assert(memcmp(frame, handshake_attempt1, sizeof(handshake_attempt1)) == 0);

    assert(esp_bms_ctrl_yanxin_build_start(2U, frame, &frame_len));
    assert(frame_len == sizeof(handshake_attempt2));
    assert(memcmp(frame, handshake_attempt2, sizeof(handshake_attempt2)) == 0);

    assert(esp_bms_ctrl_yanxin_build_start(10U, frame, &frame_len));
    assert(frame_len == sizeof(legacy_probe));
    assert(memcmp(frame, legacy_probe, sizeof(legacy_probe)) == 0);

    /* --- 握手回包：先拒绝(05=换地址)，再接受。回包要能对上最近一次握手用的
     * scheme/customerId，所以先重发 attempt 0（0x1F + customerId 0）。 --- */
    esp_bms_ctrl_yanxin_reset();
    assert(esp_bms_ctrl_yanxin_build_start(0U, frame, &frame_len));
    assert(esp_bms_ctrl_yanxin_feed(handshake_reject_try_next, sizeof(handshake_reject_try_next)));
    assert(esp_bms_ctrl_yanxin_feed(handshake_accept, sizeof(handshake_accept)));

    /* --- 轮询帧：step3 发速度参数读取，其余为实时读取 --- */
    assert(esp_bms_ctrl_yanxin_build_poll(0U, 0U, frame, &frame_len));
    assert(frame_len == sizeof(realtime_request_step0));
    assert(memcmp(frame, realtime_request_step0, sizeof(realtime_request_step0)) == 0);
    assert(esp_bms_ctrl_yanxin_build_poll(1U, 200U, frame, &frame_len));
    assert(frame[6] == 0x01U && frame[7] == 0x00U); /* seq 递增，prev 记录上一拍 */
    assert(esp_bms_ctrl_yanxin_build_poll(3U, 600U, frame, &frame_len));
    assert(frame_len == sizeof(speed_context_request));
    assert(memcmp(frame, speed_context_request, sizeof(speed_context_request)) == 0);

    /* --- 实时回包解码 --- */
    assert(esp_bms_ctrl_yanxin_feed(realtime_response, sizeof(realtime_response)));
    esp_bms_ctrl_yanxin_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 520U);
    assert(telemetry.rpm_valid && telemetry.rpm == 3000U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 2500);
    assert(telemetry.power_valid && telemetry.power_w == 1300);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 10);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 20);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 13U);
    assert(telemetry.fault_valid && telemetry.fault_code == 5U);
    assert(!telemetry.speed_valid); /* 速度参数还没读到 */

    /* --- 速度参数：deci_kmh = rpm * W * 24 / (raw * 100) = 3000*800*24/40000 --- */
    assert(esp_bms_ctrl_yanxin_feed(speed_context_response, sizeof(speed_context_response)));
    esp_bms_ctrl_yanxin_telemetry(&telemetry);
    assert(telemetry.controller_speed_params_valid);
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 1152U);

    /* --- 校验错误帧：必须被丢弃，且不影响后续好帧 --- */
    uint8_t corrupted[sizeof(realtime_response)] = { 0 };
    memcpy(corrupted, realtime_response, sizeof(corrupted));
    corrupted[sizeof(corrupted) - 1U] ^= 0xFFU;
    assert(!esp_bms_ctrl_yanxin_feed(corrupted, sizeof(corrupted)));
    assert(esp_bms_ctrl_yanxin_feed(realtime_response, sizeof(realtime_response)));

    /* --- 分片到达：前半包不返回 true，拼完才解析 --- */
    assert(!esp_bms_ctrl_yanxin_feed(realtime_response, 18U));
    assert(esp_bms_ctrl_yanxin_feed(realtime_response + 18U, sizeof(realtime_response) - 18U));

    /* --- 粘包：两帧一起到达 --- */
    uint8_t joined[sizeof(realtime_response) * 2U] = { 0 };
    memcpy(joined, realtime_response, sizeof(realtime_response));
    memcpy(joined + sizeof(realtime_response), speed_context_response, sizeof(speed_context_response));
    assert(esp_bms_ctrl_yanxin_feed(joined, sizeof(realtime_response) + sizeof(speed_context_response)));

    /* --- 逻辑包跨帧分片（flags bit1）--- */
    assert(!esp_bms_ctrl_yanxin_feed(realtime_fragment_a, sizeof(realtime_fragment_a)));
    assert(esp_bms_ctrl_yanxin_feed(realtime_fragment_b, sizeof(realtime_fragment_b)));
    esp_bms_ctrl_yanxin_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 520U && telemetry.rpm == 3000U);

    /* --- 帧头前的噪声必须被跳过 --- */
    uint8_t noisy[sizeof(realtime_response) + 3U] = { 0x00U, 0xFFU, 0xAAU };
    memcpy(noisy + 3U, realtime_response, sizeof(realtime_response));
    assert(esp_bms_ctrl_yanxin_feed(noisy, sizeof(noisy)));

    /* --- reset 清空快照与游标 --- */
    esp_bms_ctrl_yanxin_reset();
    esp_bms_ctrl_yanxin_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && telemetry.voltage_deci_v == 0U);
    assert(!telemetry.rpm_valid && telemetry.rpm == 0U);
    assert(!telemetry.speed_valid && telemetry.speed_deci_kmh == 0U);
    /* 游标归零后第一拍又是 seq=0 */
    assert(esp_bms_ctrl_yanxin_build_poll(0U, 0U, frame, &frame_len));
    assert(memcmp(frame, realtime_request_step0, sizeof(realtime_request_step0)) == 0);

    /* --- 密码握手：4 位 ASCII 写进 payload[2..5] --- */
    const uint8_t password[4] = { '1', '2', '3', '4' };
    esp_bms_ctrl_yanxin_set_password(password);
    assert(esp_bms_ctrl_yanxin_build_start(0U, frame, &frame_len));
    assert(frame_len == 14U);
    assert(frame[4] == 0x01U && frame[5] == 0x1FU);
    assert(frame[6] == '1' && frame[7] == '2' && frame[8] == '3' && frame[9] == '4');
    /* 密码帧 CRC 自己也要自洽：重算比对 */
    unsigned crc = 0xFFFFU;
    for (size_t index = 1U; index < 12U; ++index) {
        crc ^= frame[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
        }
    }
    assert(frame[12] == (uint8_t)(crc & 0xFFU));
    assert(frame[13] == (uint8_t)((crc >> 8U) & 0xFFU));
    esp_bms_ctrl_yanxin_set_password(NULL);
    assert(esp_bms_ctrl_yanxin_build_start(0U, frame, &frame_len));
    assert(frame[6] == 0x00U && frame[9] == 0x00U);

    printf("yanxin ctrl protocol selftest OK\n");
    return 0;
}
