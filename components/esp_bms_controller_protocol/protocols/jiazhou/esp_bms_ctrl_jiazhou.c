#include "esp_bms_ctrl_jiazhou.h"

#include <string.h>

/* 甲胄动力 DS11/DS15（移植自 Motexa 2.7.0 v6.g0 + v6.e0，桥接协议）。
 *
 * 传输层是「桥接芯片中转」的信封帧：
 *   0x60 | 命令 | 载荷长度(大端 16 位) | 载荷(MessagePack)
 * 命令：0xA2 控制器发现、0xA0 设备信息、0xA3 DCF、0xA4 Monitor 读取、0xA7 限制表。
 * 源码对信封不做 CRC 校验（v6.e0 只校验 0x60 与长度上限）。
 *
 * 会话时序（本模块的 attempt 分段，对应 Motexa 的 200ms→A2、A0、A3、A4 节奏）：
 *   attempt 0：0xA2 发现（载荷 = MTU 请求 500 → 01 F4），等 A2 应答拿节点 ID
 *   attempt 1：0xA0 设备信息（载荷 = 节点 ID），等 A0 应答
 *   attempt 2：0xA3 读取 DCF 元数据（载荷 = 01 节点ID），等 A3 应答
 *   attempt ≥3：0xA4 轮询 Monitor 寄存器块（本轮固定 8 项，见下）
 * 未收到对应应答时 build_poll 会按阶段重发同一帧，不会跳过握手。
 *
 * 关于 Monitor 取值：Motexa 先下载 DCF 再按其量程/缩放解释 A4 返回的原始整数；
 * 本模块不回读 DCF，因此固定请求 Motexa 遥测映射里出现的 8 个参数地址，并把原始整数
 * 按 DCF 缩放 = 1 处理（低置信度，需实机标定，见报告"不确定项"）。
 */

#define JIAZHOU_STREAM_CAPACITY 256U
#define JIAZHOU_ENVELOPE_LEN 4U
#define JIAZHOU_MAX_PAYLOAD 192U
#define JIAZHOU_MARKER 0x60U
#define JIAZHOU_MTU_REQUEST 500U

#define JIAZHOU_CMD_DISCOVERY 0xA2U
#define JIAZHOU_CMD_DEVICE_INFO 0xA0U
#define JIAZHOU_CMD_DCF 0xA3U
#define JIAZHOU_CMD_MONITOR 0xA4U

#define JIAZHOU_MONITOR_PARAM_COUNT 8U
#define JIAZHOU_PARAM_ENTRY_LEN 4U
#define JIAZHOU_READ_ERROR 0x00FFFFFF

/* Monitor 参数地址来自 Motexa v6.g0.b() 的字段映射（"param.XXXX" 后缀）。 */
static const uint16_t JIAZHOU_MONITOR_REGS[JIAZHOU_MONITOR_PARAM_COUNT] = {
    0x0448U, /* 母线电压 V        */
    0x0450U, /* 母线电流 A        */
    0x0452U, /* 电机转速 rpm      */
    0x0456U, /* 电机温度 ℃        */
    0x0458U, /* 相线电流 A        */
    0x0462U, /* MOS/控制器温度 ℃  */
    0x0465U, /* 故障码            */
    0x0463U, /* 档位              */
};

enum {
    JIAZHOU_STAGE_DISCOVERY = 0,
    JIAZHOU_STAGE_DEVICE_INFO = 1,
    JIAZHOU_STAGE_DCF = 2,
    JIAZHOU_STAGE_MONITOR = 3,
};

typedef struct {
    bool rpm_valid;
    bool current_valid;
    bool power_valid;
    bool voltage_valid;
    bool gear_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool fault_valid;
    uint16_t rpm;
    int32_t current_centi_a;
    int32_t power_w;
    uint16_t voltage_deci_v;
    uint8_t gear;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint16_t fault_code;
} jiazhou_state_t;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} jiazhou_mp_t;

static jiazhou_state_t s_state;
static uint8_t s_stream[JIAZHOU_STREAM_CAPACITY];
static size_t s_stream_len;
static uint16_t s_node_id;
static uint8_t s_stage;

static void jiazhou_stream_compact(size_t consumed)
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

static size_t jiazhou_build_envelope(uint8_t command, const uint8_t *payload, size_t payload_len, uint8_t *out)
{
    out[0] = JIAZHOU_MARKER;
    out[1] = command;
    out[2] = (uint8_t)((payload_len >> 8U) & 0xFFU);
    out[3] = (uint8_t)(payload_len & 0xFFU);
    if (payload_len > 0U && payload) {
        memcpy(out + JIAZHOU_ENVELOPE_LEN, payload, payload_len);
    }
    return JIAZHOU_ENVELOPE_LEN + payload_len;
}

static bool jiazhou_build_discovery(uint8_t *out, size_t *out_len)
{
    const uint16_t requested = JIAZHOU_MTU_REQUEST;
    const uint8_t payload[2] = { (uint8_t)(requested >> 8U), (uint8_t)(requested & 0xFFU) };
    *out_len = jiazhou_build_envelope(JIAZHOU_CMD_DISCOVERY, payload, sizeof(payload), out);
    return true;
}

static bool jiazhou_build_device_info(uint8_t *out, size_t *out_len)
{
    const uint8_t payload[2] = { (uint8_t)(s_node_id >> 8U), (uint8_t)(s_node_id & 0xFFU) };
    *out_len = jiazhou_build_envelope(JIAZHOU_CMD_DEVICE_INFO, payload, sizeof(payload), out);
    return true;
}

static bool jiazhou_build_dcf(uint8_t *out, size_t *out_len)
{
    const uint8_t payload[2] = { 0x01U, (uint8_t)(s_node_id & 0xFFU) };
    *out_len = jiazhou_build_envelope(JIAZHOU_CMD_DCF, payload, sizeof(payload), out);
    return true;
}

static bool jiazhou_build_monitor(uint8_t *out, size_t *out_len)
{
    uint8_t payload[JIAZHOU_MONITOR_PARAM_COUNT * JIAZHOU_PARAM_ENTRY_LEN] = { 0 };
    size_t offset = 0U;
    for (size_t index = 0U; index < JIAZHOU_MONITOR_PARAM_COUNT; ++index) {
        const uint16_t reg = JIAZHOU_MONITOR_REGS[index];
        payload[offset] = (uint8_t)(reg >> 8U);
        payload[offset + 1U] = (uint8_t)(reg & 0xFFU);
        /* 子索引取 h0.f19455a[0] = 0（DCF 未回读，按默认项请求）。 */
        payload[offset + 2U] = 0U;
        payload[offset + 3U] = 0U;
        offset += JIAZHOU_PARAM_ENTRY_LEN;
    }
    *out_len = jiazhou_build_envelope(JIAZHOU_CMD_MONITOR, payload, offset, out);
    return true;
}

/* ------------------------- 极简 MessagePack 读取 ------------------------- */

static bool jiazhou_mp_byte(jiazhou_mp_t *parser, uint8_t *out)
{
    if (!parser || !out || parser->pos >= parser->len) {
        return false;
    }
    *out = parser->data[parser->pos++];
    return true;
}

static bool jiazhou_mp_skip_payload(jiazhou_mp_t *parser, size_t count)
{
    if (!parser || count > parser->len - parser->pos) {
        return false;
    }
    parser->pos += count;
    return true;
}

/* 读取下一个值；*is_int 为 true 时 *out 是整数值。支持整数/字符串/nil/布尔/单双精度浮点。 */
static bool jiazhou_mp_next(jiazhou_mp_t *parser, int32_t *out, bool *is_int)
{
    uint8_t marker = 0U;
    if (!is_int || !jiazhou_mp_byte(parser, &marker)) {
        return false;
    }
    *is_int = false;
    if (marker <= 0x7FU) {
        if (out) {
            *out = (int32_t)marker;
        }
        *is_int = true;
        return true;
    }
    if (marker >= 0xE0U) {
        if (out) {
            *out = (int32_t)(int8_t)marker;
        }
        *is_int = true;
        return true;
    }
    if (marker >= 0xA0U && marker <= 0xBFU) {
        return jiazhou_mp_skip_payload(parser, (size_t)(marker & 0x1FU));
    }
    if (marker >= 0x90U && marker <= 0x9FU) {
        return jiazhou_mp_skip_payload(parser, 0U); /* 嵌套数组不在本协议里出现 */
    }
    switch (marker) {
    case 0xC0U: /* nil */
    case 0xC2U: /* false */
    case 0xC3U: /* true */
        return true;
    case 0xCCU: { /* uint8 */
        uint8_t value = 0U;
        if (!jiazhou_mp_byte(parser, &value)) {
            return false;
        }
        if (out) {
            *out = (int32_t)value;
        }
        *is_int = true;
        return true;
    }
    case 0xCDU: { /* uint16 */
        uint8_t hi = 0U;
        uint8_t lo = 0U;
        if (!jiazhou_mp_byte(parser, &hi) || !jiazhou_mp_byte(parser, &lo)) {
            return false;
        }
        if (out) {
            *out = (int32_t)(((uint16_t)hi << 8U) | lo);
        }
        *is_int = true;
        return true;
    }
    case 0xCEU: { /* uint32 */
        uint32_t value = 0U;
        for (uint8_t index = 0U; index < 4U; ++index) {
            uint8_t byte = 0U;
            if (!jiazhou_mp_byte(parser, &byte)) {
                return false;
            }
            value = (value << 8U) | byte;
        }
        if (out) {
            *out = (int32_t)value;
        }
        *is_int = true;
        return true;
    }
    case 0xD0U: { /* int8 */
        uint8_t value = 0U;
        if (!jiazhou_mp_byte(parser, &value)) {
            return false;
        }
        if (out) {
            *out = (int32_t)(int8_t)value;
        }
        *is_int = true;
        return true;
    }
    case 0xD1U: { /* int16 */
        uint8_t hi = 0U;
        uint8_t lo = 0U;
        if (!jiazhou_mp_byte(parser, &hi) || !jiazhou_mp_byte(parser, &lo)) {
            return false;
        }
        if (out) {
            *out = (int32_t)(int16_t)(((uint16_t)hi << 8U) | lo);
        }
        *is_int = true;
        return true;
    }
    case 0xD2U: { /* int32 */
        uint32_t value = 0U;
        for (uint8_t index = 0U; index < 4U; ++index) {
            uint8_t byte = 0U;
            if (!jiazhou_mp_byte(parser, &byte)) {
                return false;
            }
            value = (value << 8U) | byte;
        }
        if (out) {
            *out = (int32_t)value;
        }
        *is_int = true;
        return true;
    }
    case 0xCAU: /* float32（协议里不用于 Monitor，跳过） */
        return jiazhou_mp_skip_payload(parser, 4U);
    case 0xCBU: /* float64 */
        return jiazhou_mp_skip_payload(parser, 8U);
    case 0xD9U: { /* str8 */
        uint8_t length = 0U;
        return jiazhou_mp_byte(parser, &length) && jiazhou_mp_skip_payload(parser, length);
    }
    case 0xDAU: { /* str16 */
        uint8_t hi = 0U;
        uint8_t lo = 0U;
        if (!jiazhou_mp_byte(parser, &hi) || !jiazhou_mp_byte(parser, &lo)) {
            return false;
        }
        return jiazhou_mp_skip_payload(parser, ((size_t)hi << 8U) | lo);
    }
    default:
        return false;
    }
}

static bool jiazhou_mp_array_header(jiazhou_mp_t *parser, size_t *count)
{
    uint8_t marker = 0U;
    if (!parser || !count || !jiazhou_mp_byte(parser, &marker)) {
        return false;
    }
    if (marker >= 0x90U && marker <= 0x9FU) {
        *count = (size_t)(marker & 0x0FU);
        return true;
    }
    if (marker == 0xDCU) {
        uint8_t hi = 0U;
        uint8_t lo = 0U;
        if (!jiazhou_mp_byte(parser, &hi) || !jiazhou_mp_byte(parser, &lo)) {
            return false;
        }
        *count = ((size_t)hi << 8U) | lo;
        return true;
    }
    return false;
}

static void jiazhou_decode_monitor(const int32_t *values, const bool *valid)
{
    if (valid[0]) {
        if (values[0] < 0) {
            s_state.voltage_valid = false;
        } else {
            s_state.voltage_deci_v = (uint16_t)(values[0] > 6553 ? 6553 : values[0]) * 10U;
            s_state.voltage_valid = true;
        }
    }
    if (valid[1]) {
        s_state.current_centi_a = values[1] * 100;
        s_state.current_valid = true;
    }
    if (valid[2]) {
        if (values[2] < 0) {
            s_state.rpm_valid = false;
        } else {
            s_state.rpm = (uint16_t)(values[2] > 65535 ? 65535 : values[2]);
            s_state.rpm_valid = true;
        }
    }
    if (valid[3]) {
        s_state.motor_temp_c = (int16_t)values[3];
        s_state.motor_temp_valid = true;
    }
    if (valid[5]) {
        s_state.controller_temp_c = (int16_t)values[5];
        s_state.controller_temp_valid = true;
    }
    if (valid[6]) {
        s_state.fault_code = (uint16_t)(values[6] & 0xFFFF);
        s_state.fault_valid = true;
    }
    if (valid[7]) {
        s_state.gear = (uint8_t)(values[7] & 0xFF);
        s_state.gear_valid = true;
    }
    if (s_state.voltage_valid && s_state.current_valid) {
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
        s_state.power_valid = true;
    }
}

/* 返回 true 表示这一帧更新了遥测。 */
static bool jiazhou_handle_frame(uint8_t command, const uint8_t *payload, size_t payload_len)
{
    jiazhou_mp_t parser = { payload, payload_len, 0U };
    size_t count = 0U;
    if (command == JIAZHOU_CMD_DISCOVERY) {
        if (!jiazhou_mp_array_header(&parser, &count)) {
            return false;
        }
        for (size_t index = 0U; index < count; ++index) {
            int32_t value = 0;
            bool is_int = false;
            if (!jiazhou_mp_next(&parser, &value, &is_int)) {
                return false;
            }
            if (is_int) {
                /* 数组形如 [型号字符串, 节点 ID, ...]：第一个整数就是节点 ID。 */
                s_node_id = (uint16_t)(value & 0xFFFF);
                break;
            }
        }
        s_stage = JIAZHOU_STAGE_DEVICE_INFO;
        return false;
    }
    if (command == JIAZHOU_CMD_DEVICE_INFO) {
        s_stage = JIAZHOU_STAGE_DCF;
        return false;
    }
    if (command == JIAZHOU_CMD_DCF) {
        /* DCF 内容不回读（缩放表），仅确认握手完成。 */
        s_stage = JIAZHOU_STAGE_MONITOR;
        return false;
    }
    if (command != JIAZHOU_CMD_MONITOR) {
        return false;
    }
    if (!jiazhou_mp_array_header(&parser, &count) || count != JIAZHOU_MONITOR_PARAM_COUNT) {
        return false;
    }
    int32_t values[JIAZHOU_MONITOR_PARAM_COUNT] = { 0 };
    bool valid[JIAZHOU_MONITOR_PARAM_COUNT] = { false };
    for (size_t index = 0U; index < JIAZHOU_MONITOR_PARAM_COUNT; ++index) {
        int32_t value = 0;
        bool is_int = false;
        if (!jiazhou_mp_next(&parser, &value, &is_int)) {
            return false;
        }
        if (!is_int || value == JIAZHOU_READ_ERROR) {
            continue; /* 0xFFFFFF = 读取错误（源码判定为整体失败），该参数保持无效 */
        }
        values[index] = value;
        valid[index] = true;
    }
    jiazhou_decode_monitor(values, valid);
    return true;
}

void esp_bms_ctrl_jiazhou_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
    s_node_id = 0U;
    s_stage = JIAZHOU_STAGE_DISCOVERY;
}

bool esp_bms_ctrl_jiazhou_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* attempt 0：A2，1：A0，2：A3，≥3：A4（握手不循环，重发由 build_poll 按阶段负责）。 */
    if (attempt == 0U) {
        return jiazhou_build_discovery(out, out_len);
    }
    if (attempt == 1U) {
        return jiazhou_build_device_info(out, out_len);
    }
    if (attempt == 2U) {
        return jiazhou_build_dcf(out, out_len);
    }
    return jiazhou_build_monitor(out, out_len);
}

bool esp_bms_ctrl_jiazhou_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    switch (s_stage) {
    case JIAZHOU_STAGE_DISCOVERY:
        /* 还没拿到节点 ID：继续发现，不跳步。 */
        return jiazhou_build_discovery(out, out_len);
    case JIAZHOU_STAGE_DEVICE_INFO:
        return jiazhou_build_device_info(out, out_len);
    case JIAZHOU_STAGE_DCF:
        return jiazhou_build_dcf(out, out_len);
    default:
        return jiazhou_build_monitor(out, out_len);
    }
}

bool esp_bms_ctrl_jiazhou_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > JIAZHOU_STREAM_CAPACITY) {
        chunk += len - JIAZHOU_STREAM_CAPACITY;
        len = JIAZHOU_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > JIAZHOU_STREAM_CAPACITY) {
        jiazhou_stream_compact(s_stream_len + len - JIAZHOU_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        if (s_stream[offset] != JIAZHOU_MARKER) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < JIAZHOU_ENVELOPE_LEN) {
            break;
        }
        const size_t payload_len =
            ((size_t)s_stream[offset + 2U] << 8U) | (size_t)s_stream[offset + 3U];
        if (payload_len > JIAZHOU_MAX_PAYLOAD) {
            ++offset;
            continue;
        }
        const size_t total = JIAZHOU_ENVELOPE_LEN + payload_len;
        if (s_stream_len - offset < total) {
            break;
        }
        if (jiazhou_handle_frame(s_stream[offset + 1U], s_stream + offset + JIAZHOU_ENVELOPE_LEN, payload_len)) {
            parsed = true;
        }
        offset += total;
    }
    jiazhou_stream_compact(offset);
    return parsed;
}

void esp_bms_ctrl_jiazhou_telemetry(esp_bms_ctrl_telemetry_t *out)
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
    out->gear_valid = s_state.gear_valid;
    out->gear = s_state.gear;
    out->controller_temp_valid = s_state.controller_temp_valid;
    out->controller_temp_c = s_state.controller_temp_c;
    out->motor_temp_valid = s_state.motor_temp_valid;
    out->motor_temp_c = s_state.motor_temp_c;
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
}
