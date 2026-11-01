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
     * 只认 CRC 会把这类控制器判成"没有任何数据"。
     *
     * 关键：求和方言的字段布局与 CRC 方言**完全不同**（官方原生库 libfd_protocol.so
     * 里这是两份独立的 handler 表）。老固件（如 ND60240）的块 0 把挡位放在 frame[4]、
     * 转速放在 frame[6..7] 且是**大端**；新固件是 frame[2] 与小端 frame[8..9]。
     * 这里按老固件布局构造，验证固件确实分派到了正确的解析路径。 */
    esp_fardriver_state_t sum_state = { 0 };
    memset(data, 0, sizeof(data));
    /* 官方老固件分支的 stage 原始值要先做一次 `^ 2`（0x011c4c 的 eor w8,w8,#2），
     * 再按「0→1、1→2、其余→3」映射（e8.X 的 stage 分支）。
     * 这里令 frame[4] 的 bit2-3 = 0 → (0 ^ 2) = 2 → 挡位 3。 */
    data[2] = 0x00U;
    data[4] = 0x12U;                      /* 大端 frame[6..7] = 0x12C0 = 4800 */
    data[5] = 0xC0U;
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.gear_valid && sum_state.gear == 3U);
    assert(sum_state.rpm_valid && sum_state.rpm == 4800U);
    assert(sum_state.dialect == (uint8_t)ESP_FARDRIVER_DIALECT_SUM16);
    assert(sum_state.sum16_frame_count == 1U && sum_state.crc16_frame_count == 0U);

    /* 挡位编码 (raw ^ 2) 再映射：raw=2 → 0 → 1；raw=3 → 1 → 2；raw=0/1 → 3 */
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[2] = 0x08U; /* raw = 2 */
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.gear == 1U);
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[2] = 0x0CU; /* raw = 3 */
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.gear == 2U);
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[2] = 0x04U; /* raw = 1 → (1^2)=3 → 挡位 3 */
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.gear == 3U);

    /* 求和方言的块 1：电压/电流都是**大端有符号 16 位**，电流在 frame[4..5]。
     * 这正是 60240 电压/电流读不出来的地方——旧代码按新固件的
     * 「小端 frame[2..3] + 小端 frame[6..7]」去读，整块错位。 */
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[0] = 0x02U; /* 大端 0x02D2 = 722 → 72.2V */
    data[1] = 0xD2U;
    data[2] = 0x00U; /* 大端 0x0032 = 50 → 50 * 0.25A = 12.50A */
    data[3] = 0x32U;
    make_sum_frame(frame, 1U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.voltage_deci_v == 722U);
    assert(sum_state.current_valid && sum_state.current_centi_a == 1250);
    assert(sum_state.power_valid && sum_state.power_w == 902);

    /* 求和方言的回充：负电流也要能解出来（大端有符号） */
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[0] = 0x02U;
    data[1] = 0xD2U;
    data[2] = 0xFFU; /* 大端 0xFFE0 = -32 → -8.00A */
    data[3] = 0xE0U;
    make_sum_frame(frame, 1U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.current_centi_a == -800);
    assert(sum_state.power_valid && sum_state.power_w == -577);

    /* 求和方言的控制器温度（块 4，取 frame[4] 单字节）与电机温度（块 13，取 frame[2]） */
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[2] = 29U;
    make_sum_frame(frame, 4U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.controller_temp_valid && sum_state.controller_temp_c == 29);

    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[0] = 45U;
    make_sum_frame(frame, 13U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(sum_state.motor_temp_valid && sum_state.motor_temp_c == 45);

    /* 求和错误必须被拒绝，且不改变状态 */
    memset(&sum_state, 0, sizeof(sum_state));
    memset(data, 0, sizeof(data));
    data[2] = (uint8_t)((3U - 1U) << 2U);
    data[4] = 0x12U;
    data[5] = 0xC0U;
    make_sum_frame(frame, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    frame[15] ^= 1U;
    esp_fardriver_state_t before_sum_failure = sum_state;
    assert(!esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    assert(memcmp(&sum_state, &before_sum_failure, sizeof(sum_state)) == 0);

    /* 求和方言的块号只有 0..23：0x40 既不是合法块号，也不能被当成 CRC 方言的索引 */
    make_sum_frame(frame, 0x40U, data);
    assert(!esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));
    make_sum_frame(frame, 23U, data); /* 23 是最后一个合法块号，应被接受 */
    assert(esp_fardriver_parse_frame(&sum_state, frame, sizeof(frame)));

    /* 求和帧同样支持滑动定位（更长缓冲、带前缀） */
    uint8_t sum_framing[ESP_FARDRIVER_FRAME_LEN + 1U] = { 0 };
    esp_fardriver_state_t sum_framing_state = { 0 };
    sum_framing[0] = 0x11U;
    make_sum_frame(sum_framing + 1U, 0U, data);
    assert(esp_fardriver_parse_frame(&sum_framing_state, sum_framing, sizeof(sum_framing)));
    assert(sum_framing_state.rpm_valid);

    /* 方言判定与命名 */
    assert(esp_fardriver_frame_dialect(frame) == ESP_FARDRIVER_DIALECT_SUM16);
    assert(strcmp(esp_fardriver_dialect_name(ESP_FARDRIVER_DIALECT_SUM16), "sum16") == 0);
    assert(strcmp(esp_fardriver_dialect_name(ESP_FARDRIVER_DIALECT_CRC16), "crc16") == 0);
    assert(strcmp(esp_fardriver_dialect_name(ESP_FARDRIVER_DIALECT_NONE), "none") == 0);
    assert(esp_fardriver_frame_dialect(NULL) == ESP_FARDRIVER_DIALECT_NONE);

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
