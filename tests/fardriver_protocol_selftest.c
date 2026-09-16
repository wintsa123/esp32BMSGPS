#include "esp_fardriver_protocol.h"

#include <assert.h>
#include <string.h>

static void finish_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    const uint16_t crc = esp_fardriver_crc(frame, ESP_FARDRIVER_FRAME_LEN - 2U);
    frame[14] = (uint8_t)(crc >> 8U);
    frame[15] = (uint8_t)crc;
}

/* 真机帧格式：AA | 0x80|索引 | 12 字节数据 | CRC16 */
static void make_device_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN],
                              uint8_t index,
                              const uint8_t data[12])
{
    memset(frame, 0, ESP_FARDRIVER_FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = (uint8_t)(0x80U | (index & 0x3FU));
    memcpy(frame + 2U, data, 12U);
    finish_frame(frame);
}

/* 参数块 0xD0：data[4..5]=0xD2 轮胎规格、data[6..7]=0xD3 胎宽、data[8..9]=0xD4 传动比 */
static void make_params_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN])
{
    uint8_t data[12] = { 0 };
    data[4] = 70U; /* 扁平比 70% */
    data[5] = 12U; /* 轮辋 12 英寸 */
    data[6] = 90U; /* 胎宽 90mm */
    data[8] = 60U; /* 传动比原始值 60 -> 1.00 */
    make_device_frame(frame, 47U, data); /* FLASH_READ_ADDR[47] = 0xD0 */
}

/* 老固件的求和校验帧：AA | 索引(bit7=0) | 12 字节数据 | 校验和高字节 | 低字节 */
static void make_sum_frame(uint8_t frame[ESP_FARDRIVER_FRAME_LEN],
                           uint8_t index,
                           const uint8_t data[12])
{
    memset(frame, 0, ESP_FARDRIVER_FRAME_LEN);
    frame[0] = 0xAAU;
    frame[1] = (uint8_t)(index & 0x7FU);
    memcpy(frame + 2U, data, 12U);
    uint32_t sum = (uint32_t)0xAAU + frame[1];
    for (size_t position = 2U; position < ESP_FARDRIVER_FRAME_LEN - 2U; ++position) {
        sum += frame[position];
    }
    frame[14] = (uint8_t)((sum >> 8U) & 0xFFU);
    frame[15] = (uint8_t)(sum & 0xFFU);
}

int main(void)
{
    esp_fardriver_state_t state = { .fallback_wheel_circumference_mm = 1350U,
                                    .fallback_gear_ratio_centi = 400U };
    uint8_t frame[ESP_FARDRIVER_FRAME_LEN] = { 0 };
    uint8_t data[12] = { 0 };

    /* 真机抓包回归：0xE2 挡位=2、转速=0（YuanQu-V3.3 静止） */
    const uint8_t rpm_block[12] = { 0x85, 0x0F, 0x11, 0x20, 0x00, 0x00,
                                    0x00, 0x00, 0xFC, 0xFF, 0x00, 0x00 };
    make_device_frame(frame, 0U, rpm_block);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.gear_valid && state.gear == 2U);
    assert(state.rpm_valid && state.rpm == 0U);

    /* 真机抓包回归：0xE8 电压 22.7V、电流 0 */
    const uint8_t power_block[12] = { 0xE3, 0x00, 0xEA, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    make_device_frame(frame, 1U, power_block);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.voltage_deci_v == 227U);
    assert(state.current_valid && state.current_centi_a == 0);

    /* 真机抓包回归：0xD6 控制器温度 29℃ */
    const uint8_t controller_temp_block[12] = { 0x01, 0x4A, 0x02, 0x05, 0x10, 0x70,
                                                0x00, 0x04, 0x80, 0x12, 0x1D, 0x00 };
    make_device_frame(frame, 51U, controller_temp_block);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_temp_valid && state.controller_temp_c == 29);

    /* 真机抓包回归：0xF4 未接电机读到 -40，必须被丢弃 */
    const uint8_t motor_temp_block[12] = { 0xD8, 0xFF, 0xE8, 0x00, 0x79, 0x18,
                                           0x01, 0x11, 0x01, 0x09, 0xA7, 0x00 };
    make_device_frame(frame, 53U, motor_temp_block);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(!state.motor_temp_valid);
    assert(esp_fardriver_has_instrument_telemetry(&state));
    assert(esp_fardriver_link_online(&state));

    /* 挡位 1..4（data[0] 的 bit2-3）与转速小端编码 */
    for (uint8_t gear = 1U; gear <= 4U; ++gear) {
        memset(data, 0, sizeof(data));
        data[0] = (uint8_t)((gear - 1U) << 2U);
        data[6] = 0xC0U;
        data[7] = 0x12U; /* 小端 0x12C0 = 4800 */
        make_device_frame(frame, 0U, data);
        assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
        assert(state.gear_valid && state.gear == gear);
        assert(state.rpm_valid && state.rpm == 4800U);
    }

    /* 电压 22.7V + 电流 4.00A（0.25A 单位）= 90W */
    memset(data, 0, sizeof(data));
    data[0] = 0xE3U;
    data[4] = 0x10U; /* 16 * 0.25A = 4.00A */
    make_device_frame(frame, 1U, data);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.voltage_deci_v == 227U && state.current_centi_a == 400);
    assert(state.power_valid && state.power_w == 90);

    /* 回充：负电流 -> 负功率 */
    memset(data, 0, sizeof(data));
    data[0] = 0xE3U;
    data[4] = 0xF0U;
    data[5] = 0xFFU; /* -16 */
    make_device_frame(frame, 1U, data);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.current_centi_a == -400);
    assert(state.power_valid && state.power_w == -90);

    /* 参数块：轮胎 70/12/90 + 传动比 1.00 -> 周长 1353mm，4800rpm 对应 3896 (0.1km/h) */
    make_params_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(state.controller_speed_params_valid);
    assert(state.tire_aspect_percent == 70U && state.tire_rim_inch == 12U &&
           state.tire_width_mm == 90U && state.wheel_circumference_mm == 1353U &&
           state.gear_ratio_centi == 100U);
    assert(state.speed_valid && state.speed_deci_kmh == 3896U);

    /* 只收到参数块时：不算仪器遥测，但仍算链路在线 */
    esp_fardriver_state_t params_only_state = { 0 };
    assert(esp_fardriver_parse_frame(&params_only_state, frame, sizeof(frame)));
    assert(params_only_state.controller_speed_params_valid);
    assert(!esp_fardriver_has_instrument_telemetry(&params_only_state));
    assert(esp_fardriver_link_online(&params_only_state));

    /* 空状态判据 */
    esp_fardriver_state_t empty_state = { 0 };
    assert(!esp_fardriver_has_instrument_telemetry(&empty_state));
    assert(!esp_fardriver_link_online(&empty_state));
    assert(!esp_fardriver_link_online(NULL));

    /* open / keepalive 命令字节（与已验证可用的参考实现一致） */
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN] = { 0 };
    assert(esp_fardriver_build_open_command(command));
    assert(command[0] == 0xAAU && command[1] == 0x13U && command[2] == 0xECU &&
           command[3] == 0x07U && command[4] == 0x01U && command[5] == 0xF1U &&
           command[6] == 0xA2U && command[7] == 0x5DU);
    assert(esp_fardriver_build_keepalive_command(command));
    assert(command[0] == 0xAAU && command[1] == 0x13U && command[2] == 0xECU &&
           command[3] == 0x07U && command[4] == 0x5FU && command[5] == 0x5FU &&
           command[6] == 0x6EU && command[7] == 0x91U);

    /* 五字节读请求（Nordic UART 轮询协议） */
    uint8_t request[ESP_FARDRIVER_READ_REQUEST_LEN] = { 0 };
    assert(esp_fardriver_poll_address_count() == 40U);
    assert(esp_fardriver_poll_address(0U, &request[0]) && request[0] == 0xE2U);
    assert(esp_fardriver_poll_address(12U, &request[0]) && request[0] == 0xD0U);
    assert(esp_fardriver_poll_address(39U, &request[0]) && request[0] == 0xACU);
    assert(!esp_fardriver_poll_address(40U, &request[0]));
    assert(esp_fardriver_build_read_request(0xE2U, request));
    assert(request[0] == 0xE2U && request[1] == 0xE2U && request[2] == 0x80U &&
           request[3] == 0x09U && request[4] == 0x0AU);

    /* 未知型号的 BLE 模块可能把帧放在更长的通知里，或一次通知带两帧：
     * 滑动窗口必须能靠帧头与 CRC 定位真实帧。 */
    uint8_t framing[ESP_FARDRIVER_FRAME_LEN + 2U] = { 0 };
    esp_fardriver_state_t framing_state = { 0 };
    framing[0] = 0x5AU;
    framing[1] = 0xAAU; /* 前面出现假帧头也不能误判 */
    make_params_frame(framing + 2U);
    assert(esp_fardriver_parse_frame(&framing_state, framing, sizeof(framing)));
    assert(framing_state.controller_speed_params_valid);

    uint8_t joined[ESP_FARDRIVER_FRAME_LEN * 2U] = { 0 };
    esp_fardriver_state_t joined_state = { 0 };
    make_device_frame(joined, 0U, rpm_block);
    make_params_frame(joined + ESP_FARDRIVER_FRAME_LEN);
    assert(esp_fardriver_parse_frame(&joined_state, joined, sizeof(joined)));
    assert(joined_state.rpm_valid && !joined_state.controller_speed_params_valid);

    /* 另一些固件用 16 位求和校验（frame[1] bit7=0），官方 App 也同时支持：
     * 只认 CRC 会把这类控制器判成"没有任何数据"。 */
    esp_fardriver_state_t sum_state = { 0 };
    memset(data, 0, sizeof(data));
    data[0] = (uint8_t)((3U - 1U) << 2U); /* 挡位 3 */
    data[6] = 0xC0U;
    data[7] = 0x12U; /* 转速 4800 */
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.gear_valid && sum_state.gear == 3U);
    assert(sum_state.rpm_valid && sum_state.rpm == 4800U);

    /* 求和错误必须被拒绝，且不改变状态 */
    make_sum_frame(frame, 0U, data);
    frame[15] ^= 1U;
    esp_fardriver_state_t before_sum_failure = sum_state;
    assert(!esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(memcmp(&sum_state, &before_sum_failure, sizeof(sum_state)) == 0);

    /* 索引是 7 位：超过寄存器表的索引（>= 56）按未知块丢弃 */
    make_sum_frame(frame, 0x40U, data);
    assert(!esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));

    /* 求和帧同样支持滑动定位（更长缓冲、带前缀） */
    uint8_t sum_framing[ESP_FARDRIVER_FRAME_LEN + 1U] = { 0 };
    esp_fardriver_state_t sum_framing_state = { 0 };
    sum_framing[0] = 0x11U;
    make_sum_frame(sum_framing + 1U, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_framing_state, sum_framing, sizeof(sum_framing)));
    assert(sum_framing_state.rpm_valid);

    /* 无效帧：CRC/长度/帧头/索引越界，且不得改变已有状态 */
    make_params_frame(frame);
    assert(esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    const esp_fardriver_state_t before_invalid_frame = state;
    frame[15] ^= 1U;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    assert(memcmp(&state, &before_invalid_frame, sizeof(state)) == 0);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame) - 1U));
    finish_frame(frame);
    frame[0] = 0xABU;
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    frame[0] = 0xAAU;
    frame[1] = (uint8_t)(0x80U | 55U); /* 索引越界 */
    finish_frame(frame);
    assert(!esp_fardriver_parse_frame(&state, frame, sizeof(frame)));
    return 0;
}
