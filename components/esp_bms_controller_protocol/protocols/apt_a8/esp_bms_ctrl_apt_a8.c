#include "esp_bms_ctrl_apt_a8.h"

#include <string.h>

/* APT A8（Motexa 2.7.0 的 v6.e）：A8 | dir | marker | cmd | sub | len | payload[len] | sumLo | sumHi
 * - dir/marker 取 (0x02,0x01) 或 (0x01,0x02)；cmd 0x24=实时，0x22=参数组。
 * - 校验不是 CRC，而是"字节和查表"：sum = Σ payload 前的全部字节（除 0xA8），
 *   第 1 个校验字节 = 表A[sum & 0xFF]，第 2 个 = 表B[255 - (sum & 0xFF)]。
 *   两张表来自 Motexa 的 f19388l/f19389m，无法用公式推导，故整表内置（512B）。
 * - 帧长 = len + 8；A8 没有握手命令，订阅后直接发实时读取帧。 */

#define A8_HEAD 0xA8U
#define A8_HEADER_LEN 6U
#define A8_TRAILER_LEN 2U
#define A8_STREAM_CAPACITY 192U
#define A8_MAX_ACCEPTED_LEN 56U /* len + 8 <= 64（ESP_BMS_CTRL_FRAME_MAX_LEN） */

#define A8_CMD_REALTIME 0x24U
#define A8_CMD_PARAMETER 0x22U
#define A8_SUB_REALTIME 0x01U
#define A8_PARAM_GROUP_MIN 0x01U
#define A8_PARAM_GROUP_MAX 0x0AU

/* 转把标定默认值（Motexa n.a()：0.85V..4.2V），单位 0.01V。 */
#define A8_THROTTLE_MIN_CV 85U
#define A8_THROTTLE_MAX_CV 420U

/* 实时回包字段偏移（相对 payload，payload[0] = 电压高字节） */
#define A8_OFF_VOLTAGE 0U
#define A8_OFF_BUS_CURRENT 2U
#define A8_OFF_MOTOR_RPM 4U
#define A8_OFF_CONTROLLER_TEMP 6U
#define A8_OFF_MOTOR_TEMP 7U
#define A8_OFF_PEAK_PHASE_CURRENT 8U
#define A8_OFF_THROTTLE 10U
#define A8_OFF_GEAR 12U
#define A8_OFF_CONTROLLER_STATE 13U
#define A8_OFF_CURRENT_FAULT 15U
#define A8_OFF_PORT_STATE 17U
#define A8_OFF_IQ_TARGET 19U
#define A8_OFF_FIELD_WEAKENING 25U
#define A8_OFF_HISTORY_FAULT 27U
#define A8_OFF_BRAKE_VOLTAGE 29U
#define A8_TEMP_OFFSET 55

/* Motexa 的实机回包长度：31=完整、19/29=常见变体、27=转动检测变体。
 * 只要求 >= 17 拿到基础字段，其余按需读取。 */
#define A8_REALTIME_MIN_LEN 17U

static const uint8_t A8_SUM_HI_TABLE[256] = {
    0xB3U, 0xA5U, 0xC4U, 0x33U, 0x6CU, 0x62U, 0x0FU, 0x0AU, 0x05U, 0x7FU, 0x45U, 0x02U, 0x8DU, 0xD9U, 0x9DU, 0x0CU,
    0x7FU, 0x13U, 0xFDU, 0x00U, 0xA2U, 0xE8U, 0x05U, 0x92U, 0x2EU, 0xBFU, 0x4FU, 0x83U, 0x30U, 0x80U, 0xF5U, 0x14U,
    0x2FU, 0xA7U, 0x54U, 0xC0U, 0x10U, 0x97U, 0x10U, 0x2AU, 0x37U, 0x30U, 0x59U, 0x55U, 0xEFU, 0xA3U, 0x6CU, 0x13U,
    0x0CU, 0x85U, 0x5CU, 0xC0U, 0xCFU, 0x23U, 0x12U, 0x2FU, 0x09U, 0x16U, 0x92U, 0xE5U, 0x83U, 0x17U, 0x81U, 0x85U,
    0x9EU, 0x14U, 0xE4U, 0x8CU, 0x37U, 0x83U, 0x2BU, 0x3EU, 0xCDU, 0xF6U, 0x6EU, 0xE1U, 0xE6U, 0xEFU, 0xF6U, 0x28U,
    0xAEU, 0xF6U, 0xFFU, 0xF2U, 0xE2U, 0xEAU, 0xBCU, 0x35U, 0xECU, 0x95U, 0x9CU, 0x34U, 0x50U, 0x82U, 0xCCU, 0xF9U,
    0x46U, 0x13U, 0xFDU, 0xFFU, 0xA8U, 0xCEU, 0x67U, 0x2FU, 0x0FU, 0xF8U, 0x0EU, 0x0DU, 0x3AU, 0x65U, 0x44U, 0x34U,
    0xAFU, 0x8EU, 0x6FU, 0x00U, 0xA4U, 0xE4U, 0x0CU, 0x8AU, 0x1FU, 0x64U, 0xF4U, 0xD9U, 0x5DU, 0x6CU, 0xDEU, 0x56U,
    0x71U, 0xCDU, 0x26U, 0x81U, 0x2DU, 0x21U, 0xCDU, 0xE3U, 0x45U, 0x5FU, 0xC0U, 0x43U, 0xB2U, 0xACU, 0x5DU, 0x1DU,
    0x55U, 0x74U, 0x33U, 0x50U, 0xDDU, 0xBAU, 0x09U, 0x17U, 0xEAU, 0xACU, 0x22U, 0x3AU, 0x72U, 0x7BU, 0xC0U, 0x86U,
    0x65U, 0x69U, 0xE7U, 0x7AU, 0x8CU, 0x24U, 0x63U, 0x43U, 0xB7U, 0x52U, 0x0DU, 0xEBU, 0x17U, 0x6DU, 0x49U, 0xCDU,
    0xE9U, 0xD1U, 0xD3U, 0x4CU, 0x54U, 0x13U, 0xBCU, 0xC4U, 0x95U, 0x95U, 0xAFU, 0xC1U, 0x59U, 0x57U, 0x79U, 0x6FU,
    0x6AU, 0x10U, 0xC8U, 0x52U, 0x8EU, 0x7EU, 0x33U, 0x37U, 0xADU, 0xFAU, 0x7BU, 0x6BU, 0x31U, 0x4FU, 0x11U, 0x2AU,
    0xB2U, 0xCCU, 0xD8U, 0x5AU, 0xD3U, 0x98U, 0x2BU, 0x7AU, 0x67U, 0x46U, 0x22U, 0xD5U, 0xD9U, 0xA9U, 0x11U, 0xFBU,
    0xCAU, 0xEAU, 0x52U, 0x71U, 0xFBU, 0xD7U, 0x45U, 0xA8U, 0x6EU, 0x7EU, 0x94U, 0x2CU, 0xC8U, 0xFBU, 0xBCU, 0x1EU,
    0xFAU, 0x8EU, 0xC9U, 0xE4U, 0x20U, 0xF1U, 0x60U, 0x1FU, 0xA9U, 0xE8U, 0x03U, 0xDDU, 0xB9U, 0x19U, 0x91U, 0x11U,
};
static const uint8_t A8_SUM_LO_TABLE[256] = {
    0x27U, 0xCBU, 0x05U, 0x99U, 0x07U, 0x6DU, 0x96U, 0xBCU, 0x57U, 0x8CU, 0x4AU, 0xADU, 0xA0U, 0x9EU, 0x8FU, 0xAFU,
    0x68U, 0x20U, 0xBCU, 0x4FU, 0x56U, 0x2DU, 0x51U, 0x99U, 0x97U, 0x4FU, 0x80U, 0x8BU, 0x7FU, 0x95U, 0xEDU, 0xCAU,
    0x74U, 0xE6U, 0xB3U, 0x94U, 0x08U, 0x94U, 0xA7U, 0xF6U, 0x65U, 0x34U, 0xE0U, 0x3FU, 0xEEU, 0xAEU, 0x77U, 0xB4U,
    0x23U, 0x90U, 0x8BU, 0x85U, 0xC5U, 0xA8U, 0x88U, 0x00U, 0x3BU, 0xD1U, 0xA9U, 0x05U, 0x35U, 0x0DU, 0xBCU, 0xBAU,
    0x8FU, 0xD4U, 0x26U, 0x7FU, 0x78U, 0xACU, 0x26U, 0x25U, 0xD3U, 0xFAU, 0x5EU, 0x5AU, 0xDDU, 0x18U, 0x8FU, 0x68U,
    0x11U, 0xA7U, 0xA4U, 0x20U, 0x49U, 0x27U, 0xF2U, 0x10U, 0x25U, 0xC5U, 0xBEU, 0xFDU, 0xB0U, 0x73U, 0xFFU, 0x8DU,
    0x41U, 0x3EU, 0x66U, 0x43U, 0xA0U, 0xDCU, 0x9CU, 0xB0U, 0x6AU, 0x85U, 0xCBU, 0xE8U, 0xB7U, 0x05U, 0x5FU, 0x34U,
    0xF9U, 0x0EU, 0x0EU, 0x06U, 0x28U, 0xD2U, 0x15U, 0x32U, 0x1CU, 0xD1U, 0xC5U, 0x5AU, 0x3AU, 0xF0U, 0x3FU, 0xAAU,
    0x52U, 0xCCU, 0x7CU, 0xC6U, 0xC8U, 0x4DU, 0x90U, 0x01U, 0xF2U, 0x7EU, 0x2FU, 0xD0U, 0xC2U, 0x9CU, 0x70U, 0x7EU,
    0xA5U, 0x6DU, 0xD1U, 0x21U, 0xAAU, 0xD2U, 0x7BU, 0xCCU, 0xE7U, 0xA0U, 0xC8U, 0x06U, 0x19U, 0xACU, 0x03U, 0x7BU,
    0x8AU, 0x25U, 0x6FU, 0xF2U, 0x38U, 0x27U, 0x8AU, 0x7FU, 0x33U, 0x8CU, 0x92U, 0xFAU, 0x47U, 0x06U, 0x49U, 0xAFU,
    0xDBU, 0x6BU, 0xF6U, 0x57U, 0x19U, 0x4FU, 0xACU, 0x48U, 0x4FU, 0xD8U, 0xCEU, 0xE8U, 0x95U, 0xCEU, 0xD3U, 0x67U,
    0xB1U, 0xF2U, 0x47U, 0xAEU, 0x37U, 0x91U, 0x12U, 0x92U, 0xF5U, 0x58U, 0xFDU, 0x4EU, 0x8DU, 0x6AU, 0x72U, 0x30U,
    0x64U, 0xB0U, 0x83U, 0x92U, 0xBBU, 0x70U, 0x2FU, 0x0CU, 0x1CU, 0x22U, 0xE0U, 0xE8U, 0xF8U, 0x7FU, 0x37U, 0xD6U,
    0x8EU, 0xDAU, 0x0CU, 0xE1U, 0x0EU, 0xB3U, 0xB1U, 0xA2U, 0xFFU, 0x8AU, 0x77U, 0xB4U, 0xDEU, 0xF1U, 0x74U, 0x68U,
    0x08U, 0xE5U, 0x83U, 0xB9U, 0xDAU, 0x5EU, 0x8BU, 0x82U, 0x16U, 0x25U, 0x05U, 0xEEU, 0x88U, 0xE6U, 0xB8U, 0x30U,
};

static uint8_t s_stream[A8_STREAM_CAPACITY];
static size_t s_stream_len;

static bool s_voltage_valid;
static bool s_current_valid;
static bool s_rpm_valid;
static bool s_controller_temp_valid;
static bool s_motor_temp_valid;
static bool s_throttle_valid;
static bool s_gear_valid;
static bool s_fault_valid;

static uint16_t s_voltage_deci_v;
static int32_t s_current_centi_a;
static uint16_t s_rpm;
static int16_t s_controller_temp_c;
static int16_t s_motor_temp_c;
static uint8_t s_throttle_percent;
static uint8_t s_gear;
static uint16_t s_fault_code;
static int32_t s_power_w;

/* Motexa 用 w0.b/w0.e，都是小端（低字节在前）。 */
static bool read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || index > len || len - index < 2U) {
        return false;
    }
    *out = (uint16_t)data[index] | ((uint16_t)data[index + 1U] << 8U);
    return true;
}

static bool read_i16_le(const uint8_t *data, size_t len, size_t index, int16_t *out)
{
    uint16_t value = 0U;
    if (!read_u16_le(data, len, index, &value)) {
        return false;
    }
    *out = (int16_t)value;
    return true;
}

static uint8_t throttle_percent_from_centivolt(uint16_t centivolt)
{
    if (centivolt <= A8_THROTTLE_MIN_CV) {
        return 0U;
    }
    if (centivolt >= A8_THROTTLE_MAX_CV) {
        return 100U;
    }
    const uint32_t span = (uint32_t)(A8_THROTTLE_MAX_CV - A8_THROTTLE_MIN_CV);
    const uint32_t offset = (uint32_t)(centivolt - A8_THROTTLE_MIN_CV);
    return (uint8_t)((offset * 100U + span / 2U) / span);
}

static void refresh_power(void)
{
    const bool valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    s_power_w = valid ? (int32_t)(((int64_t)s_voltage_deci_v * s_current_centi_a) / 1000) : 0;
}

/* 字节和查表校验：数据区间是 frame[1 .. len+5]。 */
static uint16_t a8_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum = (sum + data[index]) & 0xFFFFU;
    }
    const uint8_t low = (uint8_t)(sum & 0xFFU);
    return ((uint16_t)A8_SUM_HI_TABLE[low] << 8U) | A8_SUM_LO_TABLE[255U - low];
}

static void compact_stream(size_t consumed)
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

static void parse_realtime(const uint8_t *payload, size_t len)
{
    int16_t voltage = 0;
    int16_t current = 0;
    int16_t rpm = 0;
    uint16_t throttle = 0U;
    uint16_t fault_bitmap = 0U;

    if (!read_i16_le(payload, len, A8_OFF_VOLTAGE, &voltage) ||
        !read_i16_le(payload, len, A8_OFF_BUS_CURRENT, &current) ||
        !read_i16_le(payload, len, A8_OFF_MOTOR_RPM, &rpm)) {
        return;
    }

    s_voltage_valid = voltage >= 0;
    s_voltage_deci_v = voltage > 0 ? (uint16_t)voltage : 0U;
    s_current_valid = true;
    s_current_centi_a = (int32_t)current * 10; /* 0.1A → 0.01A */
    s_rpm_valid = true;
    s_rpm = rpm >= 0 ? (uint16_t)rpm : (uint16_t)(-rpm);

    s_controller_temp_valid = true;
    s_controller_temp_c = (int16_t)((int16_t)payload[A8_OFF_CONTROLLER_TEMP] - A8_TEMP_OFFSET);
    s_motor_temp_valid = true;
    s_motor_temp_c = (int16_t)((int16_t)payload[A8_OFF_MOTOR_TEMP] - A8_TEMP_OFFSET);

    if (read_u16_le(payload, len, A8_OFF_THROTTLE, &throttle)) {
        s_throttle_valid = true;
        s_throttle_percent = throttle_percent_from_centivolt(throttle);
    }

    /* byte12：高 4 位运行状态，低 4 位档位。 */
    s_gear_valid = true;
    s_gear = (uint8_t)(payload[A8_OFF_GEAR] & 0x0FU);

    if (read_u16_le(payload, len, A8_OFF_CURRENT_FAULT, &fault_bitmap)) {
        s_fault_code = fault_bitmap;
        s_fault_valid = fault_bitmap != 0U;
    }

    refresh_power();
}

/* 返回 true 表示这是被识别的 A8 回包。 */
static bool handle_frame(const uint8_t *frame, size_t total)
{
    const uint8_t cmd = frame[3];
    const uint8_t sub = frame[4];
    const size_t payload_len = frame[5];
    const uint8_t *payload = frame + A8_HEADER_LEN;

    if (total != payload_len + A8_HEADER_LEN + A8_TRAILER_LEN) {
        return false;
    }
    if (cmd == A8_CMD_REALTIME && sub == A8_SUB_REALTIME && payload_len >= A8_REALTIME_MIN_LEN) {
        parse_realtime(payload, payload_len);
        return true;
    }
    if (cmd == A8_CMD_PARAMETER && sub >= A8_PARAM_GROUP_MIN && sub <= A8_PARAM_GROUP_MAX) {
        /* 参数组回包只验证帧，不参与遥测（Motexa 也把它标成手动只读）。 */
        return true;
    }
    return false;
}

bool esp_bms_ctrl_apt_a8_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > A8_STREAM_CAPACITY) {
        chunk += len - A8_STREAM_CAPACITY;
        len = A8_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > A8_STREAM_CAPACITY) {
        compact_stream(s_stream_len + len - A8_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        if (s_stream[offset] != A8_HEAD) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < A8_HEADER_LEN) {
            break;
        }
        const uint8_t dir = s_stream[offset + 1U];
        const uint8_t marker = s_stream[offset + 2U];
        const bool pair_ok = (dir == 0x02U && marker == 0x01U) || (dir == 0x01U && marker == 0x02U);
        if (!pair_ok) {
            ++offset;
            continue;
        }
        const uint8_t payload_len = s_stream[offset + 5U];
        if (payload_len == 0U || payload_len > A8_MAX_ACCEPTED_LEN) {
            ++offset;
            continue;
        }
        const size_t total = (size_t)payload_len + A8_HEADER_LEN + A8_TRAILER_LEN;
        if (s_stream_len - offset < total) {
            break;
        }
        const uint8_t *frame = s_stream + offset;
        const uint16_t wire = ((uint16_t)frame[total - 2U] << 8U) | frame[total - 1U];
        if (a8_checksum(frame + 1U, total - 3U) != wire) {
            ++offset;
            continue;
        }
        offset += total;
        if (handle_frame(frame, total)) {
            parsed = true;
        }
    }
    compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_apt_a8_reset(void)
{
    s_stream_len = 0U;
    s_voltage_valid = false;
    s_current_valid = false;
    s_rpm_valid = false;
    s_controller_temp_valid = false;
    s_motor_temp_valid = false;
    s_throttle_valid = false;
    s_gear_valid = false;
    s_fault_valid = false;
    s_voltage_deci_v = 0U;
    s_current_centi_a = 0;
    s_rpm = 0U;
    s_controller_temp_c = 0;
    s_motor_temp_c = 0;
    s_throttle_percent = 0U;
    s_gear = 0U;
    s_fault_code = 0U;
    s_power_w = 0;
}

/* A8 无握手：订阅后直接发实时读取。变体 0x1B 是 Motexa 的“转动检测”备用帧。 */
static const uint8_t A8_REALTIME_FRAME[8] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x1FU, 0x3EU, 0x4FU,
};
static const uint8_t A8_VARIANT_1B_FRAME[8] = {
    0xA8U, 0x02U, 0x01U, 0x24U, 0x01U, 0x1BU, 0x8CU, 0x95U,
};

bool esp_bms_ctrl_apt_a8_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    const uint8_t *frame = ((attempt & 1U) != 0U) ? A8_VARIANT_1B_FRAME : A8_REALTIME_FRAME;
    memcpy(out, frame, sizeof(A8_REALTIME_FRAME));
    *out_len = sizeof(A8_REALTIME_FRAME);
    return true;
}

bool esp_bms_ctrl_apt_a8_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    /* 每 4 拍插一次 0x1B 变体，其余拍读标准实时帧。 */
    const uint8_t *frame = ((step % 4U) == 3U) ? A8_VARIANT_1B_FRAME : A8_REALTIME_FRAME;
    memcpy(out, frame, sizeof(A8_REALTIME_FRAME));
    *out_len = sizeof(A8_REALTIME_FRAME);
    return true;
}

void esp_bms_ctrl_apt_a8_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->voltage_valid = s_voltage_valid;
    out->voltage_deci_v = s_voltage_deci_v;
    out->current_valid = s_current_valid;
    out->current_centi_a = s_current_centi_a;
    out->rpm_valid = s_rpm_valid;
    out->rpm = s_rpm;
    out->controller_temp_valid = s_controller_temp_valid;
    out->controller_temp_c = s_controller_temp_c;
    out->motor_temp_valid = s_motor_temp_valid;
    out->motor_temp_c = s_motor_temp_c;
    out->throttle_valid = s_throttle_valid;
    out->throttle_percent = s_throttle_percent;
    out->gear_valid = s_gear_valid;
    out->gear = s_gear;
    out->fault_valid = s_fault_valid;
    out->fault_code = s_fault_code;
    out->power_valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    out->power_w = s_power_w;
    /* A8 没有速度/轮胎换算参数，Motexa 也把速度标成 unavailable。 */
    out->speed_valid = false;
    out->speed_deci_kmh = 0U;
    out->controller_speed_params_valid = false;
    out->tire_rim_inch = 0U;
    out->tire_aspect_percent = 0U;
    out->tire_width_mm = 0U;
    out->wheel_circumference_mm = 0U;
    out->gear_ratio_centi = 0U;
}
