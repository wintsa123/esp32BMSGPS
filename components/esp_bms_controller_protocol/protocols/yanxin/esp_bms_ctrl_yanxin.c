#include "esp_bms_ctrl_yanxin.h"

#include <string.h>

/* 雁鑫 VITMC（Motexa 2.7.0 的 v6.h1）：
 *   [0]=0xAA [1]=len(=payload+4) [2]=seq [3]=flags payload... crcLo crcHi
 *   CRC16/MODBUS over bytes[1 .. payload+3]，低字节在前。
 * 逻辑包可以跨多个 BLE 通知分片：flags bit1 置位表示后面还有，seq 递增；
 * 最后一片清 bit1，拼完整后再按 function 分发。 */

#define YANXIN_HEAD 0xAAU
#define YANXIN_HEADER_LEN 4U
#define YANXIN_CRC_LEN 2U
#define YANXIN_MIN_LEN_BYTE 4U
#define YANXIN_PAYLOAD_MAX (ESP_BMS_CTRL_FRAME_MAX_LEN - YANXIN_HEADER_LEN - YANXIN_CRC_LEN)
#define YANXIN_STREAM_CAPACITY 256U
#define YANXIN_MESSAGE_CAPACITY 192U
#define YANXIN_FLAG_CONTINUED 0x02U

#define YANXIN_FUNC_HANDSHAKE 0x01U
#define YANXIN_FUNC_REALTIME 0x01U
#define YANXIN_FUNC_NAMED_READ 0x05U

/* 握手探测：客户号寄存器 0..4（Motexa 在 customerId=0 或“全量扫描”时按
 * 0,1,2,3,4 顺序探测），每种地址配 0x1F/0x0F 两种密码方案，共 10 次。 */
#define YANXIN_PROBE_COUNT 5U
#define YANXIN_PROBE_SLOT_COUNT (YANXIN_PROBE_COUNT * 2U)
#define YANXIN_SCHEME_FULL 0x1FU
#define YANXIN_SCHEME_LOW 0x0FU
#define YANXIN_ERROR_TRY_NEXT 5U

/* 转把电压标定默认值，取 Motexa n.a() 的空闲默认 0.85V..4.2V（0.01V 单位）。 */
#define YANXIN_THROTTLE_MIN_CV 85U
#define YANXIN_THROTTLE_MAX_CV 420U

/* 实时帧补偿量（Motexa X()）：电流/相线/弱磁 偏置 1500/3000（0.1A），温度偏置 60
 * （0.1°C），转速偏置 20000 rpm。 */
#define YANXIN_CURRENT_OFFSET_DECI_A 1500
#define YANXIN_TEMP_OFFSET_DECI_C 600
#define YANXIN_RPM_OFFSET 20000
#define YANXIN_CHIP_IO_CURRENT_GATE 0x00400000U

/* 实时帧偏移（相对 payload，payload[0]=01 payload[1]=01） */
#define YANXIN_OFF_FAULT_STATE 4U
#define YANXIN_OFF_BUS_VOLTAGE 8U
#define YANXIN_OFF_BUS_CURRENT 10U
#define YANXIN_OFF_MOTOR_RPM 12U
#define YANXIN_OFF_PHASE_CURRENT 14U
#define YANXIN_OFF_FIELD_WEAKENING 16U
#define YANXIN_OFF_MOS_TEMP 18U
#define YANXIN_OFF_MOTOR_TEMP 20U
#define YANXIN_OFF_THROTTLE 22U
#define YANXIN_OFF_RUN_STATE 24U
#define YANXIN_OFF_CHIP_IO 28U
#define YANXIN_OFF_MCU_IO 32U
#define YANXIN_OFF_SUPPLY_15V 36U
#define YANXIN_OFF_BOOST_TIME 38U
#define YANXIN_REALTIME_MIN_LEN 40U

static uint8_t s_stream[YANXIN_STREAM_CAPACITY];
static size_t s_stream_len;

static uint8_t s_message[YANXIN_MESSAGE_CAPACITY];
static size_t s_message_len;
static bool s_message_open;
static uint8_t s_message_next_seq;

static uint8_t s_password[4];
static bool s_password_set;

static bool s_authenticated;
static bool s_handshake_failed;
static uint8_t s_scheme = YANXIN_SCHEME_FULL;
static uint16_t s_customer_id;
static uint8_t s_device_error;

static uint8_t s_poll_seq;
static uint8_t s_poll_prev_seq;
static bool s_poll_have_prev;

static bool s_rpm_valid;
static bool s_voltage_valid;
static bool s_current_valid;
static bool s_controller_temp_valid;
static bool s_motor_temp_valid;
static bool s_throttle_valid;
static bool s_fault_valid;
static bool s_speed_params_valid;

static uint16_t s_rpm;
static uint16_t s_voltage_deci_v;
static int32_t s_current_centi_a;
static int32_t s_power_w;
static int16_t s_controller_temp_c;
static int16_t s_motor_temp_c;
static uint8_t s_throttle_percent;
static uint16_t s_fault_code;
static uint16_t s_speed_factor_w;
static uint16_t s_speed_ratio_raw;
static uint16_t s_speed_deci_kmh;
static bool s_speed_valid;

static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < len; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static bool read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || index > len || len - index < 2U) {
        return false;
    }
    *out = (uint16_t)data[index] | ((uint16_t)data[index + 1U] << 8U);
    return true;
}

static bool read_u32_le(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || index > len || len - index < 4U) {
        return false;
    }
    *out = (uint32_t)data[index] | ((uint32_t)data[index + 1U] << 8U) |
           ((uint32_t)data[index + 2U] << 16U) | ((uint32_t)data[index + 3U] << 24U);
    return true;
}

/* 0.1 单位的原始值 → 四舍五入的摄氏度 */
static int16_t deci_to_celsius(int32_t deci)
{
    return (int16_t)(deci >= 0 ? (deci + 5) / 10 : (deci - 5) / 10);
}

static uint8_t throttle_percent_from_centivolt(uint16_t centivolt)
{
    if (centivolt <= YANXIN_THROTTLE_MIN_CV) {
        return 0U;
    }
    if (centivolt >= YANXIN_THROTTLE_MAX_CV) {
        return 100U;
    }
    const uint32_t span = (uint32_t)(YANXIN_THROTTLE_MAX_CV - YANXIN_THROTTLE_MIN_CV);
    const uint32_t offset = (uint32_t)(centivolt - YANXIN_THROTTLE_MIN_CV);
    return (uint8_t)((offset * 100U + span / 2U) / span);
}

static void refresh_derived(void)
{
    /* 功率 = 电压(0.1V) x 电流(0.01A) → W */
    const bool power_valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    s_power_w = power_valid ? (int32_t)(((int64_t)s_voltage_deci_v * s_current_centi_a) / 1000) : 0;

    /* 速度：Motexa h1.b() → |rpm| / R * W * 6e-5 km/h，其中 R = raw * 0.0025。
     * 展开成整数：deci_kmh = rpm * W * 24 / (raw * 100)。 */
    s_speed_valid = false;
    s_speed_deci_kmh = 0U;
    if (s_rpm_valid && s_speed_params_valid && s_speed_ratio_raw > 0U && s_speed_factor_w > 0U) {
        const uint64_t deci = ((uint64_t)s_rpm * (uint64_t)s_speed_factor_w * 24ULL) /
                              ((uint64_t)s_speed_ratio_raw * 100ULL);
        if (deci <= UINT16_MAX) {
            s_speed_deci_kmh = (uint16_t)deci;
            s_speed_valid = true;
        }
    }
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

static void reset_message(void)
{
    s_message_len = 0U;
    s_message_open = false;
    s_message_next_seq = 0U;
}

static void parse_speed_context(const uint8_t *payload, size_t len)
{
    uint16_t factor = 0U;
    uint16_t ratio = 0U;
    if (!read_u16_le(payload, len, 2U, &factor) || !read_u16_le(payload, len, 4U, &ratio)) {
        return;
    }
    s_speed_factor_w = factor;
    s_speed_ratio_raw = ratio;
    s_speed_params_valid = factor > 0U && ratio > 0U;
    refresh_derived();
}

static void parse_realtime(const uint8_t *payload, size_t len)
{
    uint16_t bus_voltage = 0U;
    uint16_t bus_current = 0U;
    uint16_t motor_rpm = 0U;
    uint16_t mos_temp = 0U;
    uint16_t motor_temp = 0U;
    uint16_t throttle = 0U;
    uint32_t fault_state = 0U;

    if (!read_u16_le(payload, len, YANXIN_OFF_BUS_VOLTAGE, &bus_voltage) ||
        !read_u16_le(payload, len, YANXIN_OFF_BUS_CURRENT, &bus_current) ||
        !read_u16_le(payload, len, YANXIN_OFF_MOTOR_RPM, &motor_rpm) ||
        !read_u16_le(payload, len, YANXIN_OFF_MOS_TEMP, &mos_temp) ||
        !read_u16_le(payload, len, YANXIN_OFF_MOTOR_TEMP, &motor_temp) ||
        !read_u16_le(payload, len, YANXIN_OFF_THROTTLE, &throttle)) {
        return;
    }

    s_voltage_valid = true;
    s_voltage_deci_v = bus_voltage;

    s_rpm_valid = true;
    /* 反向时取绝对值：telemetry.rpm 是 uint16。 */
    s_rpm = motor_rpm >= YANXIN_RPM_OFFSET ? (uint16_t)(motor_rpm - YANXIN_RPM_OFFSET)
                                           : (uint16_t)(YANXIN_RPM_OFFSET - motor_rpm);

    s_controller_temp_valid = true;
    s_controller_temp_c = deci_to_celsius((int32_t)mos_temp - YANXIN_TEMP_OFFSET_DECI_C);
    s_motor_temp_valid = true;
    s_motor_temp_c = deci_to_celsius((int32_t)motor_temp - YANXIN_TEMP_OFFSET_DECI_C);

    s_throttle_valid = true;
    s_throttle_percent = throttle_percent_from_centivolt(throttle);

    /* 母线电流只有在 chip_io 的 bit22（电流显示使能）置位时才可信；否则 Motexa
     * 只把原始值丢进 "compatibility_*" 字段并对外报 0。 */
    uint32_t chip_io = 0U;
    const bool gate = read_u32_le(payload, len, YANXIN_OFF_CHIP_IO, &chip_io) &&
                      (chip_io & YANXIN_CHIP_IO_CURRENT_GATE) != 0U;
    s_current_valid = gate;
    /* raw*0.1A - 1500A → 0.01A 单位：raw*10 - 150000 */
    s_current_centi_a = gate
                            ? (int32_t)((int32_t)bus_current * 10) - (YANXIN_CURRENT_OFFSET_DECI_A * 100)
                            : 0;

    if (read_u32_le(payload, len, YANXIN_OFF_FAULT_STATE, &fault_state)) {
        s_fault_code = (uint16_t)(fault_state & 0xFFFFU);
        s_fault_valid = fault_state != 0U;
    }

    refresh_derived();
}

/* 返回 true 表示该逻辑包被识别（帧本身有效且属于已知 function）。 */
static bool parse_message(const uint8_t *payload, size_t len)
{
    if (!payload || len < 2U || payload[0] != 0x01U) {
        return false;
    }
    if (payload[1] == YANXIN_FUNC_REALTIME && len >= YANXIN_REALTIME_MIN_LEN) {
        parse_realtime(payload, len);
        return true;
    }
    if (payload[1] == YANXIN_FUNC_NAMED_READ && len >= 6U) {
        parse_speed_context(payload, len);
        return true;
    }
    if (len == 8U && payload[1] == 0x00U && (payload[2] & 0x0FU) == YANXIN_SCHEME_LOW) {
        /* 拒绝/换地址标记：(01,00,scheme,code)。code=5 表示该 customerId 不对，
         * 需要换下一个探测地址；否则就是真错误（6 = 需要/密码错误）。 */
        if (payload[2] != s_scheme) {
            return false;
        }
        s_device_error = payload[3];
        s_authenticated = false;
        s_handshake_failed = payload[3] != YANXIN_ERROR_TRY_NEXT;
        return true;
    }
    if (len == 8U && payload[1] == s_scheme) {
        s_authenticated = true;
        s_handshake_failed = false;
        s_device_error = 0U;
        return true;
    }
    return false;
}

static bool deliver_frame(const uint8_t *frame, size_t total)
{
    const uint8_t seq = frame[2];
    const uint8_t flags = frame[3];
    const uint8_t *payload = frame + YANXIN_HEADER_LEN;
    const size_t payload_len = total - YANXIN_HEADER_LEN - YANXIN_CRC_LEN;

    if ((flags & YANXIN_FLAG_CONTINUED) != 0U) {
        if (!s_message_open) {
            if (seq != 0U) {
                return false;
            }
            reset_message();
            s_message_open = true;
        } else if (seq != s_message_next_seq) {
            reset_message();
            if (seq != 0U) {
                return false;
            }
            s_message_open = true;
        }
        if (s_message_len + payload_len > YANXIN_MESSAGE_CAPACITY) {
            reset_message();
            return false;
        }
        memcpy(s_message + s_message_len, payload, payload_len);
        s_message_len += payload_len;
        s_message_next_seq = (uint8_t)(seq + 1U);
        return false;
    }

    if (s_message_open) {
        if (seq != s_message_next_seq) {
            reset_message();
            return false;
        }
        if (s_message_len + payload_len > YANXIN_MESSAGE_CAPACITY) {
            reset_message();
            return false;
        }
        memcpy(s_message + s_message_len, payload, payload_len);
        s_message_len += payload_len;
        const bool handled = parse_message(s_message, s_message_len);
        reset_message();
        return handled;
    }

    /* 单帧逻辑包要求 seq 从 0 开始，和 Motexa 的 sequence-unexpected 判定一致。 */
    if (seq != 0U) {
        return false;
    }
    return parse_message(payload, payload_len);
}

bool esp_bms_ctrl_yanxin_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > YANXIN_STREAM_CAPACITY) {
        chunk += len - YANXIN_STREAM_CAPACITY;
        len = YANXIN_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > YANXIN_STREAM_CAPACITY) {
        compact_stream(s_stream_len + len - YANXIN_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        if (s_stream[offset] != YANXIN_HEAD) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < 2U) {
            break;
        }
        const uint8_t len_byte = s_stream[offset + 1U];
        if (len_byte < YANXIN_MIN_LEN_BYTE) {
            ++offset;
            continue;
        }
        const size_t total = (size_t)len_byte + 2U;
        if (total > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < total) {
            break;
        }
        const uint8_t *frame = s_stream + offset;
        const uint16_t wire_crc = (uint16_t)frame[total - 2U] |
                                  ((uint16_t)frame[total - 1U] << 8U);
        if (crc16_modbus(frame + 1U, total - 3U) != wire_crc) {
            reset_message();
            ++offset;
            continue;
        }
        offset += total;
        if (deliver_frame(frame, total)) {
            parsed = true;
        }
    }
    compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_yanxin_set_password(const uint8_t *ascii4)
{
    if (!ascii4) {
        memset(s_password, 0, sizeof(s_password));
        s_password_set = false;
        return;
    }
    for (size_t index = 0U; index < sizeof(s_password); ++index) {
        if (ascii4[index] < 0x21U || ascii4[index] > 0x7EU) {
            memset(s_password, 0, sizeof(s_password));
            s_password_set = false;
            return;
        }
    }
    memcpy(s_password, ascii4, sizeof(s_password));
    s_password_set = true;
}

void esp_bms_ctrl_yanxin_reset(void)
{
    s_stream_len = 0U;
    reset_message();
    s_authenticated = false;
    s_handshake_failed = false;
    s_scheme = YANXIN_SCHEME_FULL;
    s_customer_id = 0U;
    s_device_error = 0U;
    s_poll_seq = 0U;
    s_poll_prev_seq = 0U;
    s_poll_have_prev = false;
    s_rpm_valid = false;
    s_voltage_valid = false;
    s_current_valid = false;
    s_controller_temp_valid = false;
    s_motor_temp_valid = false;
    s_throttle_valid = false;
    s_fault_valid = false;
    s_speed_params_valid = false;
    s_rpm = 0U;
    s_voltage_deci_v = 0U;
    s_current_centi_a = 0;
    s_power_w = 0;
    s_controller_temp_c = 0;
    s_motor_temp_c = 0;
    s_throttle_percent = 0U;
    s_fault_code = 0U;
    s_speed_factor_w = 0U;
    s_speed_ratio_raw = 0U;
    s_speed_deci_kmh = 0U;
    s_speed_valid = false;
}

static size_t wrap_frame(const uint8_t *payload, size_t payload_len, uint8_t *out)
{
    out[0] = YANXIN_HEAD;
    out[1] = (uint8_t)(payload_len + YANXIN_HEADER_LEN);
    out[2] = 0U; /* 请求方向单帧，seq 固定 0 */
    out[3] = 0U; /* flags：不带后续分片 */
    memcpy(out + YANXIN_HEADER_LEN, payload, payload_len);
    const uint16_t crc = crc16_modbus(out + 1U, payload_len + 3U);
    out[payload_len + YANXIN_HEADER_LEN] = (uint8_t)(crc & 0xFFU);
    out[payload_len + YANXIN_HEADER_LEN + 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
    return payload_len + YANXIN_HEADER_LEN + YANXIN_CRC_LEN;
}

bool esp_bms_ctrl_yanxin_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    *out_len = 0U;

    uint8_t payload[8] = { 0 };
    if (attempt >= YANXIN_PROBE_SLOT_COUNT) {
        /* 探测表用尽之后 Motexa 会改发旧协议实时探测帧：payload = 01 01 00... */
        /* 与 Motexa 一致：旧协议探测帧不改写 scheme，回包仍按上一次握手方案匹配。 */
        payload[0] = YANXIN_FUNC_HANDSHAKE;
        payload[1] = 0x01U;
        s_customer_id = 0U;
        *out_len = wrap_frame(payload, sizeof(payload), out);
        return true;
    }

    const uint8_t slot = attempt;
    const uint8_t scheme = ((slot & 1U) != 0U) ? YANXIN_SCHEME_LOW : YANXIN_SCHEME_FULL;
    const uint16_t customer_id = (uint16_t)(slot / 2U);

    payload[0] = YANXIN_FUNC_HANDSHAKE;
    payload[1] = scheme;
    if (s_password_set) {
        memcpy(payload + 2, s_password, sizeof(s_password));
    }
    payload[6] = (uint8_t)((customer_id >> 8U) & 0xFFU);
    payload[7] = (uint8_t)(customer_id & 0xFFU);

    s_scheme = scheme;
    s_customer_id = customer_id;
    *out_len = wrap_frame(payload, sizeof(payload), out);
    return true;
}

bool esp_bms_ctrl_yanxin_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    *out_len = 0U;

    /* 每 4 拍插一次速度换算参数读取（寄存器 16/17），其余拍读实时帧。 */
    if ((step % 4U) == 3U) {
        static const uint8_t speed_context[6] = { 0x01U, 0x05U, 0x10U, 0x00U, 0x11U, 0x00U };
        *out_len = wrap_frame(speed_context, sizeof(speed_context), out);
        return true;
    }

    const uint8_t current = s_poll_seq;
    const uint8_t previous = s_poll_have_prev ? s_poll_prev_seq : 0U;
    s_poll_prev_seq = current;
    s_poll_seq = (uint8_t)(current + 1U);
    s_poll_have_prev = true;

    uint8_t payload[8] = { 0 };
    payload[0] = YANXIN_FUNC_HANDSHAKE;
    payload[1] = YANXIN_FUNC_REALTIME;
    payload[2] = current;
    payload[3] = previous;
    *out_len = wrap_frame(payload, sizeof(payload), out);
    return true;
}

void esp_bms_ctrl_yanxin_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->rpm_valid = s_rpm_valid;
    out->rpm = s_rpm;
    out->speed_valid = s_speed_valid;
    out->speed_deci_kmh = s_speed_deci_kmh;
    out->current_valid = s_current_valid;
    out->current_centi_a = s_current_centi_a;
    out->power_valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    out->power_w = s_power_w;
    out->voltage_valid = s_voltage_valid;
    out->voltage_deci_v = s_voltage_deci_v;
    out->controller_temp_valid = s_controller_temp_valid;
    out->controller_temp_c = s_controller_temp_c;
    out->motor_temp_valid = s_motor_temp_valid;
    out->motor_temp_c = s_motor_temp_c;
    out->throttle_valid = s_throttle_valid;
    out->throttle_percent = s_throttle_percent;
    out->fault_valid = s_fault_valid;
    out->fault_code = s_fault_code;
    out->controller_speed_params_valid = s_speed_params_valid;
    out->gear_valid = false;
    out->gear = 0U;
    out->tire_rim_inch = 0U;
    out->tire_aspect_percent = 0U;
    out->tire_width_mm = 0U;
    out->wheel_circumference_mm = 0U;
    out->gear_ratio_centi = 0U;
}
