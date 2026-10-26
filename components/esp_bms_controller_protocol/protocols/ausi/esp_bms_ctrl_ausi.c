#include "esp_bms_ctrl_ausi.h"

#include <string.h>

/* AUSI 玉铖（移植自 Motexa 2.7.0 v6.i）。
 *
 * 帧：0xAA | 长度(=载荷+4) | 帧序号 | 标志 | 载荷(0..18) | CRC16(Modbus, 小端)
 *   - 标志低两位：bit1 = 后续仍有分片，bit0 保留；
 *     合法标志需满足 (flags & 0xCC) == 0 且 (flags & 0x30) != 0（源码 L()）。
 *   - 逻辑载荷可能跨多帧：首帧序号为 0，后续逐帧 +1。
 *
 * 握手：固定 14 字节帧，复位后发送；控制器回 载荷[0]=1 且 载荷[1]=31/15（长度>=8）表示接受。
 * 心跳：载荷 {1,1,seq,prev,0,0,0,0}，帧序号固定 0，标志 0x31。
 * 实时载荷 44 字节（小端）：+4 故障位图 u32、+8 母线电压 0.1V、+10 母线电流(0.1A-1500)、
 *   +12 转速(+(-20000))、+14 相电流、+16 弱磁电流、+18 MOS 温度、+20 电机温度、+22 转把电压。
 */

#define AUSI_STREAM_CAPACITY 256U
#define AUSI_PAYLOAD_CAPACITY 64U

#define AUSI_HEAD 0xAAU
#define AUSI_MIN_LEN_FIELD 4U
#define AUSI_MAX_LEN_FIELD 22U
#define AUSI_HEADER_LEN 4U
#define AUSI_TRAILER_LEN 2U
#define AUSI_FLAG_RESERVED 0xCCU
#define AUSI_FLAG_DIRECTION 0x30U
#define AUSI_FLAG_MORE 0x02U

#define AUSI_CMD_HANDSHAKE 0x1FU
#define AUSI_CMD_HANDSHAKE_ALT 0x0FU
#define AUSI_CMD_HEARTBEAT 0x01U
#define AUSI_PACKET_ID 0x01U

#define AUSI_REALTIME_MIN_PAYLOAD 40U
#define AUSI_CURRENT_OFFSET_CENTI 150000
#define AUSI_RPM_OFFSET 20000
/* 温度 = raw*0.1℃ - 60℃，即 raw - 600（0.1℃ 计数）。 */
#define AUSI_TEMP_OFFSET_DECI 600

typedef struct {
    bool rpm_valid;
    bool current_valid;
    bool power_valid;
    bool voltage_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool fault_valid;
    uint16_t rpm;
    int32_t current_centi_a;
    int32_t power_w;
    uint16_t voltage_deci_v;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint16_t fault_code;
} ausi_state_t;

static ausi_state_t s_state;
static uint8_t s_stream[AUSI_STREAM_CAPACITY];
static size_t s_stream_len;
static uint8_t s_payload[AUSI_PAYLOAD_CAPACITY];
static size_t s_payload_len;
static bool s_assembling;
static uint8_t s_next_id;
static bool s_ready;
static uint8_t s_seq;
static uint8_t s_prev_seq;
static bool s_have_seq;

static uint16_t ausi_crc16_modbus(const uint8_t *data, size_t len)
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

static bool ausi_read_u16_le(const uint8_t *data, size_t len, size_t offset, uint16_t *out)
{
    if (!data || !out || offset > len || len - offset < 2U) {
        return false;
    }
    *out = (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U));
    return true;
}

static bool ausi_read_u32_le(const uint8_t *data, size_t len, size_t offset, uint32_t *out)
{
    if (!data || !out || offset > len || len - offset < 4U) {
        return false;
    }
    *out = (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8U) |
           ((uint32_t)data[offset + 2U] << 16U) | ((uint32_t)data[offset + 3U] << 24U);
    return true;
}

static int16_t ausi_deci_to_celsius(int32_t deci)
{
    /* 四舍五入到 1℃，与 Java 侧 W(d,1) 后再取整一致。 */
    if (deci >= 0) {
        return (int16_t)((deci + 5) / 10);
    }
    return (int16_t)(-((-deci + 5) / 10));
}

static size_t ausi_build_frame(const uint8_t *payload, size_t payload_len, uint8_t frame_id, uint8_t flags,
                              uint8_t *out)
{
    const size_t total = payload_len + AUSI_HEADER_LEN + AUSI_TRAILER_LEN;
    out[0] = AUSI_HEAD;
    out[1] = (uint8_t)(payload_len + AUSI_HEADER_LEN);
    out[2] = frame_id;
    out[3] = flags;
    if (payload_len > 0U) {
        memcpy(out + AUSI_HEADER_LEN, payload, payload_len);
    }
    const uint16_t crc = ausi_crc16_modbus(out + 1U, payload_len + AUSI_HEADER_LEN - 1U);
    out[total - 2U] = (uint8_t)(crc & 0xFFU);
    out[total - 1U] = (uint8_t)(crc >> 8U);
    return total;
}

static void ausi_stream_compact(size_t consumed)
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

void esp_bms_ctrl_ausi_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
    s_payload_len = 0U;
    s_assembling = false;
    s_next_id = 0U;
    s_ready = false;
    s_seq = 0U;
    s_prev_seq = 0U;
    s_have_seq = false;
}

bool esp_bms_ctrl_ausi_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    (void)attempt;
    static const uint8_t handshake[14] = {
        0xAAU, 0x0CU, 0x00U, 0x31U, 0x01U, 0x1FU, 0x01U,
        0x41U, 0x55U, 0x02U, 0x00U, 0x07U, 0x83U, 0x38U,
    };
    if (!out || !out_len) {
        return false;
    }
    /* Motexa 每次发握手前先 reset，这里保持同样的语义：握手是会话起点。 */
    esp_bms_ctrl_ausi_reset();
    memcpy(out, handshake, sizeof(handshake));
    *out_len = sizeof(handshake);
    return true;
}

bool esp_bms_ctrl_ausi_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    if (!s_ready) {
        /* 尚未收到握手应答：在线前由传输层重发开启帧。 */
        *out_len = 0U;
        return false;
    }
    const uint8_t sequence = s_seq;
    const uint8_t previous = s_have_seq ? s_prev_seq : 0U;
    s_prev_seq = sequence;
    s_seq = (uint8_t)((sequence + 1U) & 0xFFU);
    s_have_seq = true;
    const uint8_t payload[8] = { AUSI_PACKET_ID, AUSI_CMD_HEARTBEAT, sequence, previous, 0U, 0U, 0U, 0U };
    *out_len = ausi_build_frame(payload, sizeof(payload), 0U, 0x31U, out);
    return true;
}

static void ausi_decode_realtime(const uint8_t *payload, size_t len)
{
    uint32_t fault = 0U;
    uint16_t voltage_raw = 0U;
    uint16_t current_raw = 0U;
    uint16_t rpm_raw = 0U;
    uint16_t mos_raw = 0U;
    uint16_t motor_raw = 0U;

    if (ausi_read_u32_le(payload, len, 4U, &fault)) {
        s_state.fault_code = (uint16_t)(fault & 0xFFFFU);
        s_state.fault_valid = true;
    }
    if (ausi_read_u16_le(payload, len, 8U, &voltage_raw)) {
        /* 0.1V/单位：raw 即 0.1V 计数。 */
        s_state.voltage_deci_v = voltage_raw;
        s_state.voltage_valid = true;
    }
    if (ausi_read_u16_le(payload, len, 10U, &current_raw)) {
        /* 电流 = raw*0.1A - 1500A，转 0.01A 计数。 */
        s_state.current_centi_a = (int32_t)current_raw * 10 - AUSI_CURRENT_OFFSET_CENTI;
        s_state.current_valid = true;
    }
    if (ausi_read_u16_le(payload, len, 12U, &rpm_raw)) {
        /* 转速 = raw - 20000；负值按 0 处理（uint16 字段）。 */
        s_state.rpm = rpm_raw > AUSI_RPM_OFFSET ? (uint16_t)(rpm_raw - AUSI_RPM_OFFSET) : 0U;
        s_state.rpm_valid = true;
    }
    if (ausi_read_u16_le(payload, len, 18U, &mos_raw)) {
        s_state.controller_temp_c = ausi_deci_to_celsius((int32_t)mos_raw - AUSI_TEMP_OFFSET_DECI);
        s_state.controller_temp_valid = true;
    }
    if (ausi_read_u16_le(payload, len, 20U, &motor_raw)) {
        s_state.motor_temp_c = ausi_deci_to_celsius((int32_t)motor_raw - AUSI_TEMP_OFFSET_DECI);
        s_state.motor_temp_valid = true;
    }
    if (s_state.voltage_valid && s_state.current_valid) {
        /* P = V * A = (deci/10) * (centi/100)，即 deci*centi/1000。 */
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
        s_state.power_valid = true;
    }
}

/* 返回 true 表示这一份逻辑载荷是实时数据。 */
static bool ausi_dispatch_payload(const uint8_t *payload, size_t len)
{
    if (len < 2U) {
        return false;
    }
    if (payload[0] != AUSI_PACKET_ID) {
        return false;
    }
    if (payload[1] == AUSI_CMD_HANDSHAKE || payload[1] == AUSI_CMD_HANDSHAKE_ALT) {
        if (len >= 8U) {
            s_ready = true;
        }
        return false;
    }
    if (payload[1] != AUSI_CMD_HEARTBEAT || !s_ready) {
        return false;
    }
    if (len < AUSI_REALTIME_MIN_PAYLOAD) {
        return false;
    }
    ausi_decode_realtime(payload, len);
    return true;
}

bool esp_bms_ctrl_ausi_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > AUSI_STREAM_CAPACITY) {
        chunk += len - AUSI_STREAM_CAPACITY;
        len = AUSI_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > AUSI_STREAM_CAPACITY) {
        ausi_stream_compact(s_stream_len + len - AUSI_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        if (s_stream[offset] != AUSI_HEAD) {
            /* 噪点：逐字节找帧头。 */
            ++offset;
            continue;
        }
        if (s_stream_len - offset < 2U) {
            break;
        }
        const uint8_t length_field = s_stream[offset + 1U];
        if (length_field < AUSI_MIN_LEN_FIELD || length_field > AUSI_MAX_LEN_FIELD) {
            ++offset;
            continue;
        }
        const size_t total = (size_t)length_field + AUSI_TRAILER_LEN;
        if (total > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < total) {
            break;
        }
        const uint16_t crc = ausi_crc16_modbus(s_stream + offset + 1U, total - 3U);
        const bool crc_ok = s_stream[offset + total - 2U] == (uint8_t)(crc & 0xFFU) &&
                            s_stream[offset + total - 1U] == (uint8_t)(crc >> 8U);
        if (!crc_ok) {
            ++offset;
            continue;
        }
        const uint8_t frame_id = s_stream[offset + 2U];
        const uint8_t flags = s_stream[offset + 3U];
        const size_t payload_len = total - AUSI_HEADER_LEN - AUSI_TRAILER_LEN;
        if ((flags & AUSI_FLAG_RESERVED) != 0U || (flags & AUSI_FLAG_DIRECTION) == 0U) {
            /* 标志非法：整帧丢弃并清空装配状态（源码 flag-invalid）。 */
            offset += total;
            s_payload_len = 0U;
            s_assembling = false;
            continue;
        }
        const bool expected_id = s_assembling ? (frame_id == s_next_id) : (frame_id == 0U);
        if (!expected_id) {
            offset += total;
            s_payload_len = 0U;
            s_assembling = false;
            continue;
        }
        if (s_payload_len + payload_len > AUSI_PAYLOAD_CAPACITY) {
            offset += total;
            s_payload_len = 0U;
            s_assembling = false;
            continue;
        }
        memcpy(s_payload + s_payload_len, s_stream + offset + AUSI_HEADER_LEN, payload_len);
        s_payload_len += payload_len;
        offset += total;
        if ((flags & AUSI_FLAG_MORE) != 0U) {
            s_assembling = true;
            s_next_id = (uint8_t)(frame_id + 1U);
            continue;
        }
        if (ausi_dispatch_payload(s_payload, s_payload_len)) {
            parsed = true;
        }
        s_payload_len = 0U;
        s_assembling = false;
    }
    ausi_stream_compact(offset);
    return parsed;
}

void esp_bms_ctrl_ausi_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
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
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
}
