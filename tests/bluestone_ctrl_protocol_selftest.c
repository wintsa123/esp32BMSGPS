/* 蓝石 BlueStone 控制器协议自测：帧构造、校验、分片、字段换算。
 * 期望值来自 Motexa 2.7.0 com.motexaqixing.app.j0 / i0 / v6.o 的换算公式。 */

#include "esp_bms_ctrl_bluestone.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAME_LEN 56U
#define ACK_LEN 16U

static uint8_t sum8(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum += data[index];
    }
    return (uint8_t)(sum & 0xFFU);
}

static void write_f32_le(uint8_t *dst, float value)
{
    uint32_t raw = 0U;
    memcpy(&raw, &value, sizeof(raw));
    dst[0] = (uint8_t)(raw & 0xFFU);
    dst[1] = (uint8_t)((raw >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((raw >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((raw >> 24U) & 0xFFU);
}

/* 按 j0.K 的偏移拼一帧实时数据：55 E0 02 | 数据 | SUM8 */
static void make_realtime_frame(uint8_t frame[FRAME_LEN], float rpm, float volts, float amps, float temp,
                                float throttle, float phase_amps)
{
    memset(frame, 0, FRAME_LEN);
    frame[0] = 0x55U;
    frame[1] = 0xE0U;
    frame[2] = 0x02U;
    for (size_t index = 3U; index < 11U; ++index) {
        frame[index] = (uint8_t)(0x20U + index); /* 未定位的头部字段，保持非零 */
    }
    write_f32_le(frame + 11U, rpm);
    write_f32_le(frame + 15U, volts);
    write_f32_le(frame + 19U, amps);
    write_f32_le(frame + 23U, phase_amps);
    for (size_t index = 27U; index < 31U; ++index) {
        frame[index] = (uint8_t)(0x30U + index);
    }
    write_f32_le(frame + 31U, temp);
    for (size_t index = 35U; index < 39U; ++index) {
        frame[index] = (uint8_t)(0x40U + index);
    }
    write_f32_le(frame + 39U, throttle);
    frame[FRAME_LEN - 1U] = sum8(frame, FRAME_LEN - 1U);
}

/* j0.F 里的 0x70 命令应答页（16 字节），不应产生遥测 */
static void make_ack_frame(uint8_t frame[ACK_LEN])
{
    memset(frame, 0, ACK_LEN);
    frame[0] = 0x55U;
    frame[1] = 0x70U;
    frame[2] = 0x02U;
    frame[6] = 0x08U;
    frame[7] = 0x42U;
    frame[ACK_LEN - 1U] = sum8(frame, ACK_LEN - 1U);
}

static const uint8_t START_ATTEMPT_0[16] = { 0x55U, 0x70U, 0x06U, 0x00U, 0x00U, 0x00U, 0x08U,
                                             0x42U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                             0x00U, 0x15U };
static const uint8_t START_ATTEMPT_1[16] = { 0x55U, 0x70U, 0x06U, 0x00U, 0x00U, 0x00U, 0x08U,
                                             0x15U, 0x19U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                             0x00U, 0x01U };
static const uint8_t POLL_READ[16] = { 0x55U, 0x70U, 0x06U, 0x00U, 0x00U, 0x00U, 0x08U,
                                       0x40U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                                       0x00U, 0x14U };

static void test_frames(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;

    memset(frame, 0xAA, sizeof(frame));
    assert(esp_bms_ctrl_bluestone_build_start(0U, frame, &len));
    assert(len == 16U);
    assert(memcmp(frame, START_ATTEMPT_0, sizeof(START_ATTEMPT_0)) == 0);

    assert(esp_bms_ctrl_bluestone_build_start(1U, frame, &len));
    assert(memcmp(frame, START_ATTEMPT_1, sizeof(START_ATTEMPT_1)) == 0);

    /* 校验位自检：最后一个字节 == 前 15 字节和 */
    assert(frame[15] == sum8(frame, 15U));

    assert(esp_bms_ctrl_bluestone_build_poll(0U, 120U, frame, &len));
    assert(len == 16U);
    assert(memcmp(frame, POLL_READ, sizeof(POLL_READ)) == 0);

    /* 超时补发录音机请求（j0.J） */
    assert(esp_bms_ctrl_bluestone_build_poll(3U, 1600U, frame, &len));
    assert(len == 16U);
    assert(memcmp(frame + 6U, "\x08\x15\x19", 3U) == 0);
    assert(frame[15] == sum8(frame, 15U));

    /* 空指针必须安全返回 false */
    assert(!esp_bms_ctrl_bluestone_build_start(0U, NULL, &len));
    assert(!esp_bms_ctrl_bluestone_build_poll(0U, 0U, NULL, &len));
}

static void test_realtime_frame(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;
    size_t len = 0U;

    esp_bms_ctrl_bluestone_reset();
    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(!telemetry.rpm_valid && !telemetry.voltage_valid && !telemetry.speed_valid);

    /* 72.0V / 15.0A / 45℃ / 2.00V 转把 / 600rpm */
    make_realtime_frame(frame, 600.0f, 72.0f, 15.0f, 45.0f, 2.0f, 30.0f);
    assert(frame[55] == sum8(frame, 55U));
    assert(esp_bms_ctrl_bluestone_feed(frame, sizeof(frame)));

    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 720U);
    assert(telemetry.current_valid && telemetry.current_centi_a == 1500);
    assert(telemetry.power_valid && telemetry.power_w == 1080);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.rpm_valid && telemetry.rpm == 600U);
    /* 600rpm * 1414mm * 60 / 1e6 = 50.904 km/h -> 509 */
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 509U);
    /* (2.00 - 0.85) / (4.2 - 0.85) * 100 = 34.3% -> 34 */
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 34U);
    /* 蓝石不上报整车参数 */
    assert(!telemetry.controller_speed_params_valid);
    assert(telemetry.wheel_circumference_mm == 1414U);
    (void)len;
}

static void test_checksum_rejected(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_bluestone_reset();
    make_realtime_frame(frame, 600.0f, 72.0f, 15.0f, 45.0f, 2.0f, 30.0f);
    frame[FRAME_LEN - 1U] ^= 0xFFU; /* 破坏 SUM8 */
    assert(!esp_bms_ctrl_bluestone_feed(frame, sizeof(frame)));

    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && !telemetry.current_valid && !telemetry.power_valid);
    assert(!telemetry.rpm_valid && !telemetry.speed_valid && !telemetry.throttle_valid);
    assert(!telemetry.controller_temp_valid);

    /* 数据字节被篡改但校验位同步重算：仍应被区间校验挡住（rpm 5000rpm 越界） */
    make_realtime_frame(frame, 5000.0f, 72.0f, 15.0f, 45.0f, 2.0f, 30.0f);
    assert(esp_bms_ctrl_bluestone_feed(frame, sizeof(frame)));
    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(telemetry.voltage_valid);
    assert(!telemetry.rpm_valid && !telemetry.speed_valid);
    assert(telemetry.rpm == 0U && telemetry.speed_deci_kmh == 0U);
}

static void test_fragmented(void)
{
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_bluestone_reset();
    make_realtime_frame(frame, 600.0f, 72.0f, 15.0f, 45.0f, 2.0f, 30.0f);

    assert(!esp_bms_ctrl_bluestone_feed(frame, 30U));
    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);

    assert(esp_bms_ctrl_bluestone_feed(frame + 30U, FRAME_LEN - 30U));
    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 720U);
    assert(telemetry.rpm == 600U);
    assert(telemetry.speed_deci_kmh == 509U);
}

static void test_ack_and_noise(void)
{
    uint8_t ack[ACK_LEN] = { 0 };
    uint8_t frame[FRAME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_bluestone_reset();
    make_ack_frame(ack);
    /* 0x70 应答页：消费但不产生遥测 */
    assert(!esp_bms_ctrl_bluestone_feed(ack, sizeof(ack)));
    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);

    /* 噪声 + 应答页 + 实时页粘在一个通知里 */
    make_realtime_frame(frame, 300.0f, 48.0f, -15.0f, 30.0f, 4.2f, 10.0f);
    uint8_t chunk[FRAME_LEN + ACK_LEN + 5U];
    memset(chunk, 0x00, sizeof(chunk));
    chunk[0] = 0x11U;
    chunk[1] = 0x55U;
    chunk[2] = 0x00U;
    memcpy(chunk + 5U, ack, sizeof(ack));
    memcpy(chunk + 5U + sizeof(ack), frame, sizeof(frame));
    assert(esp_bms_ctrl_bluestone_feed(chunk, sizeof(chunk)));

    esp_bms_ctrl_bluestone_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 480U);
    /* 回充：负电流 -> 负功率 */
    assert(telemetry.current_centi_a == -1500);
    assert(telemetry.power_valid && telemetry.power_w == -720);
    assert(telemetry.rpm == 300U);
    /* 4.2V 满量程 -> 100% */
    assert(telemetry.throttle_percent == 100U);

    /* 超长垃圾数据不得越界 */
    uint8_t junk[300];
    memset(junk, 0xA5, sizeof(junk));
    assert(!esp_bms_ctrl_bluestone_feed(junk, sizeof(junk)));
    assert(!esp_bms_ctrl_bluestone_feed(NULL, 0U));
}

int main(void)
{
    test_frames();
    test_realtime_frame();
    test_checksum_rejected();
    test_fragmented();
    test_ack_and_noise();
    printf("bluestone ctrl protocol selftest: OK\n");
    return 0;
}
