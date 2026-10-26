#include "esp_bms_ctrl_suniu.h"

#include <string.h>

/* 速牛 Suniu（Motexa 2.7.0 / v6.d1）。
 *
 * 帧格式：28 | len | cmd | data... | xor | 29
 *   [0]      0x28 '('
 *   [1]      len，整帧长度 = len + 4（v6.d1.B()：iG + 4）
 *   [2]      命令字
 *   [3..]    data（长度为 len - 2，含 sub 命令字的 0x93 帧）
 *   倒数第 2 xor = byte[1] ^ ... ^ byte[total-3]（v6.d1.G()）
 *   [last]   0x29 ')'
 *
 * 请求帧（v6.d1 常量）：
 *   联机    28 01 D0 D1 29        （f19363f，回应 0x55）
 *   版本    28 01 E0 E1 29        （f19364g，回应 0xA0）
 *   实时    28 02 D3 F3 22 29     （f19367j，回应 0x93 F3，主遥测）
 *   状态 F0 28 02 D3 F0 21 29     （f19365h，回应 0x93 F0 故障位图）
 *   状态 F1 28 02 D3 F1 20 29     （f19366i，回应 0x93 F1 功能位图 + 挡位）
 *   参数    28 07 D4 00 96 00 97 00 98 4A 29（f19368k，回应 0x94 参数：150/151/152）
 *
 * 0x93 F3 主遥测（payload_len 15，总长 20 字节，大端有符号 16 位，v6.d1.y()）：
 *   [4]  bus-voltage           s16 BE /10 V
 *   [6]  bus-current           s16 BE /10 A
 *   [8]  phase-current         s16 BE /10 A（遥测契约无对应字段）
 *   [10] controller-temperature s16 BE /10 °C
 *   [12] motor-temperature     s16 BE /10 °C
 *   [14] motor-rpm             s16 BE  rpm（不缩放）
 *   [16] throttle              s16 BE /10 %
 *
 * 0x93 F0/F1（payload_len 5，总长 10 字节）：[4..7] 4 字节位图；
 *   F1 的挡位 = byte[5] & 7（v6.d1.y()）。
 *
 * 0x94 参数帧：从 [3] 起每 4 字节一组「u16 BE 参数号 + u16 BE 值」：
 *   150 轮胎周长、151 轮胎校正系数、152 传动比系数（v6.d1.u()）。
 *
 * 车速在 Motexa 里由 rpm × 周长 × 校正 × 传动比 推出（v6.d1.L()）：
 *   km/h = rpm × (circumference_mm × 60 / 1e6) × (corr / 2048) × (ratio / 2048) */

#define SUNIU_START 0x28U
#define SUNIU_END 0x29U
#define SUNIU_LEN_OFFSET 1U
#define SUNIU_CMD_OFFSET 2U
#define SUNIU_SUB_OFFSET 3U
#define SUNIU_FRAME_EXTRA 4U /* 长度字节之外的固定开销：'('、len、...、xor、')' */
#define SUNIU_STREAM_CAPACITY 256U /* 覆盖 MTU 512 的通知整包（12 帧 × 20B） */
#define SUNIU_TEMP_RAW_MIN (-500) /* -50.0 °C */
#define SUNIU_TEMP_RAW_MAX 2000   /* 200.0 °C */
#define SUNIU_THROTTLE_RAW_MAX 1000U
#define SUNIU_FACTOR_SCALE 2048U

static const uint8_t SUNIU_ONLINE_REQUEST[] = { 0x28U, 0x01U, 0xD0U, 0xD1U, 0x29U };
static const uint8_t SUNIU_VERSION_REQUEST[] = { 0x28U, 0x01U, 0xE0U, 0xE1U, 0x29U };
static const uint8_t SUNIU_REALTIME_REQUEST[] = { 0x28U, 0x02U, 0xD3U, 0xF3U, 0x22U, 0x29U };
static const uint8_t SUNIU_STATUS_F0_REQUEST[] = { 0x28U, 0x02U, 0xD3U, 0xF0U, 0x21U, 0x29U };
static const uint8_t SUNIU_STATUS_F1_REQUEST[] = { 0x28U, 0x02U, 0xD3U, 0xF1U, 0x20U, 0x29U };
static const uint8_t SUNIU_PARAMS_REQUEST[] = { 0x28U, 0x07U, 0xD4U, 0x00U, 0x96U,
                                                0x00U, 0x97U, 0x00U, 0x98U, 0x4AU, 0x29U };

typedef struct {
    bool rpm_valid;
    uint16_t rpm;
    bool speed_valid;
    uint16_t speed_deci_kmh;
    bool current_valid;
    int32_t current_centi_a;
    bool voltage_valid;
    uint16_t voltage_deci_v;
    bool power_valid;
    int32_t power_w;
    bool controller_temp_valid;
    int16_t controller_temp_c;
    bool motor_temp_valid;
    int16_t motor_temp_c;
    bool throttle_valid;
    uint8_t throttle_percent;
    bool gear_valid;
    uint8_t gear;
    bool fault_valid;
    uint16_t fault_code;
    bool params_valid;
    uint16_t wheel_circumference_mm;
    uint16_t gear_ratio_centi;
    bool has_circumference;
    bool has_tire_correction;
    bool has_ratio_factor;
    uint16_t tire_correction_raw;
    uint16_t transmission_ratio_raw;
} suniu_state_t;

static suniu_state_t s_state;
static uint8_t s_stream[SUNIU_STREAM_CAPACITY];
static size_t s_stream_len;

static uint16_t suniu_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

static int16_t suniu_sbe16(const uint8_t *data)
{
    return (int16_t)suniu_be16(data);
}

static int16_t suniu_deci_to_int(int16_t raw)
{
    if (raw >= 0) {
        return (int16_t)((raw + 5) / 10);
    }
    return (int16_t)(-((-raw + 5) / 10));
}

static void suniu_compact_stream(size_t consumed)
{
    if (consumed == 0U) {
        return;
    }
    if (consumed >= s_stream_len) {
        s_stream_len = 0U;
        return;
    }
    memmove(s_stream, s_stream + consumed, s_stream_len - consumed);
    s_stream_len -= consumed;
}

static size_t suniu_find_start(const uint8_t *data, size_t len)
{
    for (size_t index = 0U; index < len; ++index) {
        if (data[index] == SUNIU_START) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool suniu_xor_ok(const uint8_t *frame, size_t frame_len)
{
    uint8_t value = 0U;
    for (size_t index = 1U; index + 2U < frame_len; ++index) {
        value = (uint8_t)(value ^ frame[index]);
    }
    return value == frame[frame_len - 2U];
}

static void suniu_refresh_derived(void)
{
    s_state.power_valid = s_state.voltage_valid && s_state.current_valid;
    if (s_state.power_valid) {
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
    } else {
        s_state.power_w = 0;
    }

    /* 车速由 rpm 与 0x94 参数推得（v6.d1.L()）；参数不全时保持无效 */
    s_state.speed_valid = false;
    s_state.speed_deci_kmh = 0U;
    if (!s_state.rpm_valid || !s_state.params_valid) {
        return;
    }
    if (s_state.wheel_circumference_mm == 0U || s_state.tire_correction_raw == 0U ||
        s_state.transmission_ratio_raw == 0U) {
        return;
    }
    /* deci-km/h = rpm × mm × 600 / 1e6 × corr / 2048 × ratio / 2048 */
    uint64_t value = (uint64_t)s_state.rpm * (uint64_t)s_state.wheel_circumference_mm;
    value = (value * 600U) / 1000000U;
    value = (value * (uint64_t)s_state.tire_correction_raw) / SUNIU_FACTOR_SCALE;
    value = (value * (uint64_t)s_state.transmission_ratio_raw) / SUNIU_FACTOR_SCALE;
    if (value == 0U || value > UINT16_MAX) {
        return;
    }
    s_state.speed_deci_kmh = (uint16_t)value;
    s_state.speed_valid = true;
}

static void suniu_parse_realtime(const uint8_t *frame)
{
    /* [4] 母线电压：s16 BE / 10 V → 0.1V 单位即原始值 */
    const int16_t voltage_raw = suniu_sbe16(frame + 4U);
    if (voltage_raw > 0) {
        s_state.voltage_deci_v = (uint16_t)voltage_raw;
        s_state.voltage_valid = true;
    } else {
        s_state.voltage_deci_v = 0U;
        s_state.voltage_valid = false;
    }

    /* [6] 母线电流：s16 BE / 10 A → 0.01A */
    s_state.current_centi_a = (int32_t)suniu_sbe16(frame + 6U) * 10;
    s_state.current_valid = true;

    /* [10] 控制器温度 / [12] 电机温度：s16 BE / 10 °C，越界丢弃 */
    const int16_t controller_raw = suniu_sbe16(frame + 10U);
    if (controller_raw >= SUNIU_TEMP_RAW_MIN && controller_raw <= SUNIU_TEMP_RAW_MAX) {
        s_state.controller_temp_c = suniu_deci_to_int(controller_raw);
        s_state.controller_temp_valid = true;
    } else {
        s_state.controller_temp_c = 0;
        s_state.controller_temp_valid = false;
    }

    const int16_t motor_raw = suniu_sbe16(frame + 12U);
    if (motor_raw >= SUNIU_TEMP_RAW_MIN && motor_raw <= SUNIU_TEMP_RAW_MAX) {
        s_state.motor_temp_c = suniu_deci_to_int(motor_raw);
        s_state.motor_temp_valid = true;
    } else {
        s_state.motor_temp_c = 0;
        s_state.motor_temp_valid = false;
    }

    /* [14] 电机转速：s16 BE，整数 rpm；负值视为越界 */
    const int16_t rpm_raw = suniu_sbe16(frame + 14U);
    if (rpm_raw >= 0) {
        s_state.rpm = (uint16_t)rpm_raw;
        s_state.rpm_valid = true;
    } else {
        s_state.rpm = 0U;
        s_state.rpm_valid = false;
    }

    /* [16] 油门开度：s16 BE / 10 %，超出 0..100% 丢弃 */
    const int16_t throttle_raw = suniu_sbe16(frame + 16U);
    if (throttle_raw >= 0 && (uint16_t)throttle_raw <= SUNIU_THROTTLE_RAW_MAX) {
        s_state.throttle_percent = (uint8_t)suniu_deci_to_int(throttle_raw);
        s_state.throttle_valid = true;
    } else {
        s_state.throttle_percent = 0U;
        s_state.throttle_valid = false;
    }

    suniu_refresh_derived();
}

static void suniu_parse_status(const uint8_t *frame, uint8_t sub)
{
    if (sub == 0xF0U) {
        /* [4..7] 故障位图（大端 32 位）；契约只有 16 位故障码，取低 16 位 */
        s_state.fault_code = suniu_be16(frame + 6U);
        s_state.fault_valid = true;
    } else if (sub == 0xF1U) {
        /* [4..7] 功能/运行状态位图，其中 [5] 低 3 位是挡位号（v6.d1.y()） */
        s_state.gear = (uint8_t)(frame[5U] & 0x07U);
        s_state.gear_valid = true;
    }
    suniu_refresh_derived();
}

static void suniu_parse_params(const uint8_t *frame, size_t frame_len)
{
    /* 数据区 = [3, frame_len - 2)，每 4 字节一组 */
    for (size_t offset = 3U; offset + 4U <= frame_len - 2U; offset += 4U) {
        const uint16_t id = suniu_be16(frame + offset);
        const uint16_t value = suniu_be16(frame + offset + 2U);
        switch (id) {
        case 150U:
            s_state.wheel_circumference_mm = value;
            s_state.has_circumference = true;
            break;
        case 151U:
            s_state.tire_correction_raw = value;
            s_state.has_tire_correction = true;
            break;
        case 152U:
            s_state.transmission_ratio_raw = value;
            s_state.has_ratio_factor = true;
            break;
        default:
            /* 27 = 极对数等其余参数在遥测契约里没有字段 */
            break;
        }
    }
    s_state.params_valid = s_state.has_circumference && s_state.has_tire_correction && s_state.has_ratio_factor;
    if (s_state.params_valid) {
        /* 传动比系数以 2048 为 1.00；gear_ratio_centi 用百分之一为单位 */
        const uint32_t ratio_centi =
            (((uint32_t)s_state.transmission_ratio_raw * 100U) + (SUNIU_FACTOR_SCALE / 2U)) / SUNIU_FACTOR_SCALE;
        s_state.gear_ratio_centi = (uint16_t)((ratio_centi > UINT16_MAX) ? UINT16_MAX : ratio_centi);
    } else {
        s_state.gear_ratio_centi = 0U;
    }
    suniu_refresh_derived();
}

static void suniu_parse_frame(const uint8_t *frame, size_t frame_len)
{
    const uint8_t command = frame[SUNIU_CMD_OFFSET];
    const size_t payload_len = (size_t)frame[SUNIU_LEN_OFFSET] - 1U; /* 去掉长度字节自身 */
    if (command == 0x93U) {
        const uint8_t sub = frame[SUNIU_SUB_OFFSET];
        if (sub == 0xF3U) {
            if (payload_len == 15U && frame_len == 20U) {
                suniu_parse_realtime(frame);
            }
        } else if (sub == 0xF0U || sub == 0xF1U) {
            if (payload_len == 5U && frame_len == 10U) {
                suniu_parse_status(frame, sub);
            }
        }
    } else if (command == 0x94U) {
        /* 参数区长度必须是 4 的整数倍（v6.d1.y()：i9 % 4 != 0 视为布局无效） */
        if (payload_len >= 4U && (payload_len % 4U) == 0U) {
            suniu_parse_params(frame, frame_len);
        }
    }
    /* 0x55 联机应答、0xA0 版本应答、0xA2 敏感应答都不产出遥测 */
}

void esp_bms_ctrl_suniu_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_suniu_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* Motexa 的开启链是 联机(D0) → 版本(E0) → 实时(F3)；用 attempt 依次轮换，
     * 这样即使某一步没有回应，重发也能推进到实时读取。 */
    const uint8_t *command = SUNIU_ONLINE_REQUEST;
    size_t command_len = sizeof(SUNIU_ONLINE_REQUEST);
    switch (attempt % 3U) {
    case 0U:
        break;
    case 1U:
        command = SUNIU_VERSION_REQUEST;
        command_len = sizeof(SUNIU_VERSION_REQUEST);
        break;
    default:
        command = SUNIU_REALTIME_REQUEST;
        command_len = sizeof(SUNIU_REALTIME_REQUEST);
        break;
    }
    memcpy(out, command, command_len);
    *out_len = command_len;
    return true;
}

bool esp_bms_ctrl_suniu_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    /* 8 拍一轮：实时帧占多数，每轮穿插故障位图、功能位图（含挡位）与轮胎参数读取，
     * 对应 Motexa realtime 链里"每 3 帧 F3 后接 F0/F1"的节奏。 */
    const uint8_t *command = SUNIU_REALTIME_REQUEST;
    size_t command_len = sizeof(SUNIU_REALTIME_REQUEST);
    switch (step % 8U) {
    case 3U:
        command = SUNIU_STATUS_F0_REQUEST;
        command_len = sizeof(SUNIU_STATUS_F0_REQUEST);
        break;
    case 4U:
        command = SUNIU_STATUS_F1_REQUEST;
        command_len = sizeof(SUNIU_STATUS_F1_REQUEST);
        break;
    case 6U:
        command = SUNIU_PARAMS_REQUEST;
        command_len = sizeof(SUNIU_PARAMS_REQUEST);
        break;
    default:
        break;
    }
    memcpy(out, command, command_len);
    *out_len = command_len;
    return true;
}

bool esp_bms_ctrl_suniu_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > SUNIU_STREAM_CAPACITY) {
        chunk += len - SUNIU_STREAM_CAPACITY;
        len = SUNIU_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > SUNIU_STREAM_CAPACITY) {
        suniu_compact_stream(s_stream_len + len - SUNIU_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        const uint8_t *window = s_stream + offset;
        const size_t avail = s_stream_len - offset;
        const size_t start = suniu_find_start(window, avail);
        if (start == SIZE_MAX) {
            /* 帧必然以 '(' 开头，缓冲里没有就整段丢弃 */
            offset = s_stream_len;
            break;
        }
        if (start > 0U) {
            offset += start;
            continue;
        }
        if (avail < (SUNIU_LEN_OFFSET + 1U)) {
            break; /* 长度字节还没到 */
        }
        const size_t declared = window[SUNIU_LEN_OFFSET];
        if (declared < 1U) {
            offset += 1U;
            continue;
        }
        const size_t frame_len = declared + SUNIU_FRAME_EXTRA;
        if (frame_len > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            /* 超长帧丢弃，避免溢出解析窗口 */
            offset += 1U;
            continue;
        }
        if (avail < frame_len) {
            break; /* 等下一片补齐 */
        }
        if (window[frame_len - 1U] != SUNIU_END) {
            offset += 1U;
            continue;
        }
        if (!suniu_xor_ok(window, frame_len)) {
            offset += 1U;
            continue;
        }
        suniu_parse_frame(window, frame_len);
        parsed = true;
        offset += frame_len;
    }
    suniu_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_suniu_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    /* 速牛没有轮辋/胎宽/扁平比参数，控制器参数只有周长 + 传动比 */
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
    out->speed_valid = s_state.speed_valid;
    out->speed_deci_kmh = s_state.speed_deci_kmh;
    out->gear_valid = s_state.gear_valid;
    out->gear = s_state.gear;
    out->current_valid = s_state.current_valid;
    out->current_centi_a = s_state.current_centi_a;
    out->power_valid = s_state.power_valid;
    out->power_w = s_state.power_w;
    out->voltage_valid = s_state.voltage_valid;
    out->voltage_deci_v = s_state.voltage_deci_v;
    out->controller_temp_valid = s_state.controller_temp_valid;
    out->controller_temp_c = s_state.controller_temp_c;
    out->motor_temp_valid = s_state.motor_temp_valid;
    out->motor_temp_c = s_state.motor_temp_c;
    out->throttle_valid = s_state.throttle_valid;
    out->throttle_percent = s_state.throttle_percent;
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
    out->controller_speed_params_valid = s_state.params_valid;
    out->wheel_circumference_mm = s_state.wheel_circumference_mm;
    out->gear_ratio_centi = s_state.gear_ratio_centi;
}
