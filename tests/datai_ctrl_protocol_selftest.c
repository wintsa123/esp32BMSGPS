#include "esp_bms_ctrl_datai.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 大泰 RTD 帧：01 05 02 | type=00 | sub=00 | len | payload(28) | sum8
 * sum8 = (byte[0] + ... + byte[n-2]) & 0xFF（v6/s.java 的 G() 判据）。 */

#define DATAI_PAYLOAD_LEN 28U
#define DATAI_FRAME_LEN (DATAI_PAYLOAD_LEN + 7U)

static void put_le16(uint8_t *out, size_t offset, uint16_t value)
{
    out[offset] = (uint8_t)(value & 0xFFU);
    out[offset + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void finalize(uint8_t *frame, size_t frame_len)
{
    uint8_t sum = 0U;
    for (size_t index = 0U; index + 1U < frame_len; ++index) {
        sum = (uint8_t)(sum + frame[index]);
    }
    frame[frame_len - 1U] = sum;
}

static void build_rtd_frame(uint8_t *frame, int16_t speed_kmh, uint16_t throttle_raw)
{
    memset(frame, 0, DATAI_FRAME_LEN);
    frame[0] = 0x01U;
    frame[1] = 0x05U;
    frame[2] = 0x02U;
    frame[3] = 0x00U; /* type：RTD */
    frame[4] = 0x00U; /* sub：RTD */
    frame[5] = (uint8_t)DATAI_PAYLOAD_LEN;

    uint8_t *payload = frame + 6U;
    put_le16(payload, 0U, (uint16_t)speed_kmh); /* 车速 km/h */
    put_le16(payload, 2U, 720U);                /* 母线电压 72.0 V */
    put_le16(payload, 4U, 1234U);               /* 母线电流 123.4 A */
    put_le16(payload, 6U, 999U);                /* 相电流（契约无字段） */
    put_le16(payload, 8U, 330U);                /* 电机温度 33.0 °C */
    put_le16(payload, 10U, 410U);               /* PCU 温度 41.0 °C */
    payload[12U] = 0x20U;                       /* fault_1 = 0x00000020 */
    put_le16(payload, 24U, 3200U);              /* 电机转速 */
    put_le16(payload, 26U, throttle_raw);       /* 油门百分比 */
    finalize(frame, DATAI_FRAME_LEN);
}

static void expect_invalid(const esp_bms_ctrl_telemetry_t *telemetry)
{
    assert(!telemetry->rpm_valid);
    assert(!telemetry->speed_valid);
    assert(!telemetry->gear_valid);
    assert(!telemetry->current_valid);
    assert(!telemetry->power_valid);
    assert(!telemetry->voltage_valid);
    assert(!telemetry->controller_temp_valid);
    assert(!telemetry->motor_temp_valid);
    assert(!telemetry->throttle_valid);
    assert(!telemetry->fault_valid);
    assert(!telemetry->controller_speed_params_valid);
}

int main(void)
{
    uint8_t buffer[ESP_BMS_CTRL_FRAME_MAX_LEN] = { 0 };
    size_t out_len = 0U;
    esp_bms_ctrl_telemetry_t telemetry = { 0 };

    /* 1) 开启帧与轮询帧都是 RTD 读请求 05 01 01 00 00 00 07 */
    esp_bms_ctrl_datai_reset();
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        assert(esp_bms_ctrl_datai_build_start(attempt, buffer, &out_len));
        assert(out_len == 7U);
        assert(memcmp(buffer, (const uint8_t[]){ 0x05U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x07U }, out_len) == 0);
    }
    for (uint16_t step = 0U; step < 3U; ++step) {
        assert(esp_bms_ctrl_datai_build_poll(step, step * 200U, buffer, &out_len));
        assert(out_len == 7U);
        assert(buffer[0] == 0x05U && buffer[6] == 0x07U);
    }
    assert(!esp_bms_ctrl_datai_build_start(0U, NULL, &out_len));
    assert(!esp_bms_ctrl_datai_build_poll(0U, 0U, buffer, NULL));

    /* 2) 正例帧：关键字段与单位换算 */
    uint8_t frame[DATAI_FRAME_LEN];
    build_rtd_frame(frame, 450, 55U);
    printf("datai RTD frame: ");
    for (size_t index = 0U; index < DATAI_FRAME_LEN; ++index) {
        printf("%02X%s", frame[index], (index + 1U == DATAI_FRAME_LEN) ? "\n" : " ");
    }
    assert(frame[DATAI_FRAME_LEN - 1U] == 0x42U); /* 手算 sum8 = 1346 & 0xFF */

    esp_bms_ctrl_datai_reset();
    assert(esp_bms_ctrl_datai_feed(frame, sizeof(frame)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    assert(telemetry.speed_valid && telemetry.speed_deci_kmh == 4500U);         /* 45.0 km/h */
    assert(telemetry.voltage_valid && telemetry.voltage_deci_v == 720U);        /* 72.0 V */
    assert(telemetry.current_valid && telemetry.current_centi_a == 12340);      /* 123.40 A */
    assert(telemetry.power_valid && telemetry.power_w == 8884);                 /* 720*12340/1000 */
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
    assert(telemetry.motor_temp_valid && telemetry.motor_temp_c == 33);
    assert(telemetry.controller_temp_valid && telemetry.controller_temp_c == 41);
    assert(telemetry.throttle_valid && telemetry.throttle_percent == 55U);
    assert(telemetry.fault_valid && telemetry.fault_code == 0x20U);
    assert(!telemetry.gear_valid); /* RTD 帧没有挡位字段 */

    /* 3) 校验错误帧：被拒，不产生假有效位 */
    uint8_t corrupt[DATAI_FRAME_LEN];
    memcpy(corrupt, frame, sizeof(corrupt));
    corrupt[6U + 24U] ^= 0x01U; /* 改转速低字节 */
    esp_bms_ctrl_datai_reset();
    assert(!esp_bms_ctrl_datai_feed(corrupt, sizeof(corrupt)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    expect_invalid(&telemetry);

    /* 帧尾校验字节本身被改同样拒绝 */
    memcpy(corrupt, frame, sizeof(corrupt));
    corrupt[DATAI_FRAME_LEN - 1U] ^= 0xFFU;
    esp_bms_ctrl_datai_reset();
    assert(!esp_bms_ctrl_datai_feed(corrupt, sizeof(corrupt)));

    /* 4) 分片到达：切在长度字节前后都不能提前解析 */
    for (size_t split = 1U; split < DATAI_FRAME_LEN; ++split) {
        esp_bms_ctrl_datai_reset();
        const size_t head = (split > 5U) ? 5U : split; /* 前一片切在长度字节里 */
        assert(!esp_bms_ctrl_datai_feed(frame, head));
        assert(!esp_bms_ctrl_datai_feed(frame + head, split - head));
        esp_bms_ctrl_datai_telemetry(&telemetry);
        assert(!telemetry.rpm_valid);
        assert(esp_bms_ctrl_datai_feed(frame + split, DATAI_FRAME_LEN - split));
        esp_bms_ctrl_datai_telemetry(&telemetry);
        assert(telemetry.rpm_valid && telemetry.rpm == 3200U);
        assert(telemetry.speed_deci_kmh == 4500U && telemetry.current_centi_a == 12340);
    }

    /* 5) 噪声前缀 + 粘包（两帧） */
    uint8_t joined[3U + (DATAI_FRAME_LEN * 2U)];
    memset(joined, 0x99, 3U);
    build_rtd_frame(joined + 3U, 120, 10U);
    build_rtd_frame(joined + 3U + DATAI_FRAME_LEN, 360, 80U);
    esp_bms_ctrl_datai_reset();
    assert(esp_bms_ctrl_datai_feed(joined, sizeof(joined)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    assert(telemetry.speed_deci_kmh == 3600U && telemetry.throttle_percent == 80U);

    /* 6) 长度字段超长（整帧 > ESP_BMS_CTRL_FRAME_MAX_LEN）：整帧丢弃并重新同步到下一帧 */
    uint8_t oversize[6U + DATAI_FRAME_LEN];
    memset(oversize, 0, sizeof(oversize));
    oversize[0] = 0x01U;
    oversize[1] = 0x05U;
    oversize[2] = 0x02U;
    oversize[5] = (uint8_t)(ESP_BMS_CTRL_FRAME_MAX_LEN - 6U); /* 声明长度使整帧 = MAX + 1 */
    memcpy(oversize + 6U, frame, DATAI_FRAME_LEN);
    esp_bms_ctrl_datai_reset();
    assert(esp_bms_ctrl_datai_feed(oversize, sizeof(oversize)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    assert(telemetry.rpm_valid && telemetry.rpm == 3200U);

    /* 7) 油门越界按 Java 的 Math.min 截断到 100% */
    build_rtd_frame(frame, 0, 250U);
    esp_bms_ctrl_datai_reset();
    assert(esp_bms_ctrl_datai_feed(frame, sizeof(frame)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    assert(telemetry.throttle_percent == 100U);
    assert(telemetry.speed_deci_kmh == 0U && telemetry.speed_valid);

    /* 8) 负车速（倒车）无法用 0.1km/h 无符号量表示 → 丢弃车速，其余字段照常 */
    build_rtd_frame(frame, -12, 0U);
    esp_bms_ctrl_datai_reset();
    assert(esp_bms_ctrl_datai_feed(frame, sizeof(frame)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    assert(!telemetry.speed_valid && telemetry.speed_deci_kmh == 0U);
    assert(telemetry.rpm_valid && telemetry.voltage_valid && telemetry.current_valid);

    /* 9) 只有噪声：不产出任何有效位 */
    esp_bms_ctrl_datai_reset();
    const uint8_t noise[9] = { 0x01U, 0x05U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0x99U };
    assert(!esp_bms_ctrl_datai_feed(noise, sizeof(noise)));
    esp_bms_ctrl_datai_telemetry(&telemetry);
    expect_invalid(&telemetry);

    /* 10) reset 清空 */
    esp_bms_ctrl_datai_reset();
    esp_bms_ctrl_datai_telemetry(&telemetry);
    expect_invalid(&telemetry);
    assert(telemetry.rpm == 0U && telemetry.speed_deci_kmh == 0U && telemetry.power_w == 0);

    assert(!esp_bms_ctrl_datai_feed(NULL, 0U));
    printf("datai ctrl protocol selftest: OK\n");
    return 0;
}
