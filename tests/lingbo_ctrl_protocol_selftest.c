/* 凌博 Lingbo 控制器协议自测：标定握手、实时帧换算、校验拒绝、分片。
 * 期望值来自 Motexa 2.7.0 com.motexaqixing.app.a9（y 标定 / z 实时）与 v6.o 车速公式。 */

#include "esp_bms_ctrl_lingbo.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CALIBRATION_LEN 16U
#define REALTIME_LEN 49U

static uint8_t sum8(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum += data[index];
    }
    return (uint8_t)(sum & 0xFFU);
}

static void put_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void put_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

/* a9.y：AA 55 0D 60 | 11 字节标定数据 | SUM8 */
static void make_calibration_frame(uint8_t frame[CALIBRATION_LEN], uint16_t phase_current_base,
                                   uint16_t phase_voltage_base, uint16_t bus_current_div,
                                   uint16_t bus_voltage_div)
{
    memset(frame, 0, CALIBRATION_LEN);
    frame[0] = 0xAAU;
    frame[1] = 0x55U;
    frame[2] = (uint8_t)(CALIBRATION_LEN - 3U); /* LEN = 13 */
    frame[3] = 0x60U;
    put_u16_le(frame + 4U, phase_current_base);
    put_u16_le(frame + 6U, phase_voltage_base);
    put_u16_le(frame + 8U, bus_current_div);
    put_u16_le(frame + 10U, bus_voltage_div);
    frame[12] = 0x00U; /* 控制模式 */
    put_u16_le(frame + 13U, 500U);
    frame[CALIBRATION_LEN - 1U] = sum8(frame, CALIBRATION_LEN - 1U);
}

/* a9.z：AA 55 2E 62 | 44 字节实时数据 | SUM8（总长 49） */
static void make_realtime_frame(uint8_t frame[REALTIME_LEN], int16_t rpm, uint16_t raw_voltage,
                                int16_t raw_current, uint16_t controller_temp, uint16_t motor_temp,
                                uint32_t fault, uint16_t throttle_centi_v, uint8_t gear)
{
    memset(frame, 0, REALTIME_LEN);
    frame[0] = 0xAAU;
    frame[1] = 0x55U;
    frame[2] = (uint8_t)(REALTIME_LEN - 3U); /* LEN = 46 */
    frame[3] = 0x62U;
    put_u16_le(frame + 4U, (uint16_t)rpm);
    put_u32_le(frame + 6U, 65536U); /* 相电流平方项，本模块不使用 */
    put_u16_le(frame + 10U, raw_voltage);
    put_u16_le(frame + 12U, (uint16_t)raw_current);
    put_u16_le(frame + 14U, controller_temp);
    put_u16_le(frame + 16U, motor_temp);
    put_u32_le(frame + 18U, fault);
    put_u16_le(frame + 22U, throttle_centi_v);
    put_u16_le(frame + 24U, 0x0004U);
    frame[26] = 0x00U;
    frame[27] = (uint8_t)(0xF0U | (gear & 0x0FU));
    frame[REALTIME_LEN - 1U] = sum8(frame, REALTIME_LEN - 1U);
}

static const uint8_t REQUEST_CALIBRATION[5] = { 0xAAU, 0x55U, 0x02U, 0x20U, 0x21U };
static const uint8_t REQUEST_REALTIME[5] = { 0xAAU, 0x55U, 0x02U, 0x22U, 0x23U };

static void test_requests(void)
{
    uint8_t frame[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;

    memset(frame, 0xAA, sizeof(frame));
    assert(esp_bms_ctrl_lingbo_build_start(0U, frame, &len));
    assert(len == 5U);
    assert(memcmp(frame, REQUEST_CALIBRATION, sizeof(REQUEST_CALIBRATION)) == 0);

    assert(esp_bms_ctrl_lingbo_build_start(1U, frame, &len));
    assert(memcmp(frame, REQUEST_REALTIME, sizeof(REQUEST_REALTIME)) == 0);
    assert(frame[4] == sum8(frame, 4U));

    /* 未标定 -> 轮询拍仍发标定请求 */
    esp_bms_ctrl_lingbo_reset();
    assert(esp_bms_ctrl_lingbo_build_poll(0U, 0U, frame, &len));
    assert(memcmp(frame, REQUEST_CALIBRATION, sizeof(REQUEST_CALIBRATION)) == 0);

    assert(!esp_bms_ctrl_lingbo_build_start(0U, NULL, &len));
    assert(!esp_bms_ctrl_lingbo_build_poll(0U, 0U, NULL, &len));
}

static void test_calibration_then_realtime(void)
{
    uint8_t calibration[CALIBRATION_LEN] = { 0 };
    uint8_t realtime[REALTIME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;
    uint8_t poll[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t len = 0U;

    esp_bms_ctrl_lingbo_reset();

    /* 未标定的实时帧必须被丢弃 */
    make_realtime_frame(realtime, 600, 7200U, 1500, 45U, 38U, 0x00010004U, 200U, 3U);
    assert(!esp_bms_ctrl_lingbo_feed(realtime, sizeof(realtime)));

    /* 标定帧：分母 100 -> 电压 0.01V/count、电流 0.01A/count */
    make_calibration_frame(calibration, 1024U, 4096U, 100U, 100U);
    assert(!esp_bms_ctrl_lingbo_feed(calibration, sizeof(calibration)));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);

    /* 标定完成后轮询拍改发实时请求 */
    assert(esp_bms_ctrl_lingbo_build_poll(1U, 0U, poll, &len));
    assert(memcmp(poll, REQUEST_REALTIME, sizeof(REQUEST_REALTIME)) == 0);

    assert(esp_bms_ctrl_lingbo_feed(realtime, sizeof(realtime)));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    /* 7200 / 100 = 72.0V */
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 720U);
    /* 1500 / 100 = 15.0A */
    assert(telemetry.current_valid && telemetry.current_centi_a == 1500);
    assert(telemetry.power_valid && telemetry.power_w == 1080);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 45);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 38);
    assert(telemetry.rpm_valid && telemetry.rpm == 600U);
    /* 600rpm * 1414mm * 60 / 1e6 = 50.904 km/h -> 509 */
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 509U);
    /* 2.00V 转把 -> (2.00-0.85)/3.35 = 34% */
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 34U);
    /* @27 低 4 位 = 挡位 3 */
    assert(telemetry.gear_valid && telemetry.gear == 3U);
    assert(telemetry.fault_valid && telemetry.fault_code == 0x0004U);
    assert(telemetry.wheel_circumference_mm == 1414U);
    assert(!telemetry.controller_speed_params_valid);
}

static void test_checksum_rejected(void)
{
    uint8_t calibration[CALIBRATION_LEN] = { 0 };
    uint8_t realtime[REALTIME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_lingbo_reset();
    make_calibration_frame(calibration, 1024U, 4096U, 100U, 100U);
    assert(!esp_bms_ctrl_lingbo_feed(calibration, sizeof(calibration)));

    make_realtime_frame(realtime, 600, 7200U, 1500, 45U, 38U, 0x00010004U, 200U, 3U);
    realtime[REALTIME_LEN - 1U] ^= 0x5AU; /* 破坏 SUM8 */
    assert(!esp_bms_ctrl_lingbo_feed(realtime, sizeof(realtime)));

    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(!telemetry.voltage_valid && !telemetry.current_valid && !telemetry.power_valid);
    assert(!telemetry.rpm_valid && !telemetry.speed_valid && !telemetry.fault_valid);

    /* 标定分母为 0 的帧不可用（a9.a：busCurrentConvert/busVoltageConvert 必须 > 0） */
    esp_bms_ctrl_lingbo_reset();
    make_calibration_frame(calibration, 1024U, 4096U, 0U, 100U);
    assert(!esp_bms_ctrl_lingbo_feed(calibration, sizeof(calibration)));
    make_realtime_frame(realtime, 600, 7200U, 1500, 45U, 38U, 0x00010004U, 200U, 3U);
    assert(!esp_bms_ctrl_lingbo_feed(realtime, sizeof(realtime)));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);
}

static void test_fragmented(void)
{
    uint8_t calibration[CALIBRATION_LEN] = { 0 };
    uint8_t realtime[REALTIME_LEN] = { 0 };
    esp_bms_ctrl_telemetry_t telemetry;

    esp_bms_ctrl_lingbo_reset();
    make_calibration_frame(calibration, 1024U, 4096U, 100U, 100U);
    assert(!esp_bms_ctrl_lingbo_feed(calibration, sizeof(calibration)));

    make_realtime_frame(realtime, 600, 7200U, 1500, 45U, 38U, 0x00010004U, 200U, 3U);
    /* 前半段：帧头 + 长度 + 一部分数据 */
    assert(!esp_bms_ctrl_lingbo_feed(realtime, 20U));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(!telemetry.voltage_valid);

    /* 后半段补齐后必须一次解析成功 */
    assert(esp_bms_ctrl_lingbo_feed(realtime + 20U, REALTIME_LEN - 20U));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 720U);
    assert(telemetry.current_centi_a == 1500);
    assert(telemetry.gear == 3U);

    /* 标定帧同样要能跨分片重组 */
    esp_bms_ctrl_lingbo_reset();
    assert(!esp_bms_ctrl_lingbo_feed(calibration, 6U));
    assert(!esp_bms_ctrl_lingbo_feed(calibration + 6U, CALIBRATION_LEN - 6U));
    assert(esp_bms_ctrl_lingbo_feed(realtime, sizeof(realtime)));
    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 720U);
}

static void test_noise_and_oversize(void)
{
    uint8_t calibration[CALIBRATION_LEN] = { 0 };
    uint8_t realtime[REALTIME_LEN] = { 0 };
    uint8_t chunk[REALTIME_LEN + 7U];
    esp_bms_ctrl_telemetry_t telemetry;

    /* 声明长度超过 64 字节上限的帧必须被丢弃，不得溢出缓冲 */
    uint8_t huge[8] = { 0xAAU, 0x55U, 0xF0U, 0x62U, 0x00U, 0x00U, 0x00U, 0x00U };
    assert(!esp_bms_ctrl_lingbo_feed(huge, sizeof(huge)));

    esp_bms_ctrl_lingbo_reset();
    make_calibration_frame(calibration, 1024U, 4096U, 100U, 100U);
    assert(!esp_bms_ctrl_lingbo_feed(calibration, sizeof(calibration)));

    /* 噪声 + 实时帧粘在一个通知里 */
    make_realtime_frame(realtime, -300, 4800U, -1500, 30U, 25U, 0x00000000U, 420U, 1U);
    memset(chunk, 0x00, sizeof(chunk));
    chunk[0] = 0x12U;
    chunk[1] = 0xAAU;
    chunk[2] = 0x34U;
    memcpy(chunk + 7U, realtime, sizeof(realtime));
    assert(esp_bms_ctrl_lingbo_feed(chunk, sizeof(chunk)));

    esp_bms_ctrl_lingbo_telemetry(&telemetry);
    assert(telemetry.voltage_deci_v == 480U);
    assert(telemetry.current_centi_a == -1500);
    assert(telemetry.power_valid && telemetry.power_w == -720);
    /* 反转：转速取绝对值，车速仍按 |rpm| 换算 */
    assert(telemetry.rpm == 300U);
    /* 300rpm * 1414mm * 60 / 1e6 = 25.452 km/h -> 255 */
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 255U);
    /* 4.2V -> 100% */
    assert(telemetry.throttle_percent == 100U);

    uint8_t junk[400];
    memset(junk, 0x5A, sizeof(junk));
    assert(!esp_bms_ctrl_lingbo_feed(junk, sizeof(junk)));
    assert(!esp_bms_ctrl_lingbo_feed(NULL, 0U));
}

int main(void)
{
    test_requests();
    test_calibration_then_realtime();
    test_checksum_rejected();
    test_fragmented();
    test_noise_and_oversize();
    printf("lingbo ctrl protocol selftest: OK\n");
    return 0;
}
