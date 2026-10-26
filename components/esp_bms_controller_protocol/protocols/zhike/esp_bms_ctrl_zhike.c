#include "esp_bms_ctrl_zhike.h"

#include <string.h>

/* 智科 GEKOO（Motexa 2.7.0 / com.motexaqixing.app.ll + v6.p1）。
 *
 * 实时帧固定 67 字节：
 *   [0..2]   AA 13 00                帧头（v6.p1.f19734f）
 *   [3..64]  31 个小端 16 位数据字    字 0..30，K(bArr, 3 + 2 * n)
 *   [65..66] 校验字（小端）           sum(word0..word31) & 0xFFFF == 0xFFFF（v6.p1.w()）
 *
 * 写入帧：
 *   开启帧 01 02（v6.p1.x()，写到 FFE2）
 *   轮询帧 AA 13 FF 01 AA 13 00 01（v6.p1.C()，写到 FFE1）
 *   短应答 AA 13 00 01（v6.p1.f19737i）不带遥测，只用于让状态机前进。
 *
 * 单位换算全部沿用 p1.d 的比例常数（见各字段注释）。 */

#define ZHIKE_FRAME_LEN 67U
#define ZHIKE_HEADER_LEN 3U
#define ZHIKE_WORD_COUNT 32U
#define ZHIKE_SHORT_RESPONSE_LEN 4U
/* 两帧余量，保证分片/粘包时总能凑出完整帧 */
#define ZHIKE_STREAM_CAPACITY (ZHIKE_FRAME_LEN * 3U)

/* 温度合理性上限：raw 为百分之一摄氏度（无符号解析），> 200.00 °C 视为越界 */
#define ZHIKE_TEMP_RAW_MAX 20000U

static const uint8_t ZHIKE_HEADER[ZHIKE_HEADER_LEN] = { 0xAAU, 0x13U, 0x00U };
static const uint8_t ZHIKE_SHORT_RESPONSE[ZHIKE_SHORT_RESPONSE_LEN] = { 0xAAU, 0x13U, 0x00U, 0x01U };
static const uint8_t ZHIKE_INIT_COMMAND[] = { 0x01U, 0x02U };
static const uint8_t ZHIKE_POLL_COMMAND[] = { 0xAAU, 0x13U, 0xFFU, 0x01U, 0xAAU, 0x13U, 0x00U, 0x01U };

typedef struct {
    bool rpm_valid;
    uint16_t rpm;
    bool current_valid;
    int32_t current_centi_a;
    bool voltage_valid;
    uint16_t voltage_deci_v;
    bool power_valid;
    int32_t power_w;
    bool controller_temp_valid;
    int16_t controller_temp_c;
    bool gear_valid;
    uint8_t gear;
    bool fault_valid;
    uint16_t fault_code;
} zhike_state_t;

static zhike_state_t s_state;
static uint8_t s_stream[ZHIKE_STREAM_CAPACITY];
static size_t s_stream_len;

static void zhike_compact_stream(size_t consumed)
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

/* 字 n 位于 [3 + 2n, 4 + 2n]，小端（v6.p1.K） */
static uint16_t zhike_word(const uint8_t *frame, size_t index)
{
    const size_t offset = ZHIKE_HEADER_LEN + (index * 2U);
    return (uint16_t)(((uint16_t)frame[offset + 1U] << 8U) | (uint16_t)frame[offset]);
}

static bool zhike_checksum_ok(const uint8_t *frame)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < ZHIKE_WORD_COUNT; ++index) {
        sum += zhike_word(frame, index);
    }
    return (uint16_t)(sum & 0xFFFFU) == 0xFFFFU;
}

/* 找到帧头下标；找不到返回 SIZE_MAX */
static size_t zhike_find_header(const uint8_t *data, size_t len)
{
    if (len < ZHIKE_HEADER_LEN) {
        return SIZE_MAX;
    }
    for (size_t index = 0U; index + ZHIKE_HEADER_LEN <= len; ++index) {
        if (memcmp(data + index, ZHIKE_HEADER, ZHIKE_HEADER_LEN) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

/* 缓冲尾部可能是半个帧头，保留这段前缀而不是丢掉 */
static size_t zhike_partial_header_suffix(const uint8_t *data, size_t len)
{
    for (size_t keep = ZHIKE_HEADER_LEN - 1U; keep > 0U; --keep) {
        if (len < keep) {
            continue;
        }
        if (memcmp(data + len - keep, ZHIKE_HEADER, keep) == 0) {
            return keep;
        }
    }
    return 0U;
}

/* AA 13 00 01：可能是独立的 4 字节短应答，也可能是某个真实帧的第 0 字恰为 0x0001 的开头，
 * 所以先看前缀，再等后续字节确认
 * （v6.p1.m() 用同样的"下一个帧头正好在第 4 字节"判据）。 */
static bool zhike_has_short_prefix(const uint8_t *window, size_t avail)
{
    return avail >= ZHIKE_SHORT_RESPONSE_LEN &&
           memcmp(window, ZHIKE_SHORT_RESPONSE, ZHIKE_SHORT_RESPONSE_LEN) == 0;
}

static bool zhike_short_response_confirmed(const uint8_t *window, size_t avail)
{
    if (!zhike_has_short_prefix(window, avail) || avail <= ZHIKE_SHORT_RESPONSE_LEN) {
        return false;
    }
    return zhike_find_header(window + ZHIKE_SHORT_RESPONSE_LEN, avail - ZHIKE_SHORT_RESPONSE_LEN) == 0U;
}

static void zhike_refresh_derived(void)
{
    /* 功率 = 电压 × 电流（与 fardriver 一致：0.1V × 0.01A / 1000 = W） */
    s_state.power_valid = s_state.voltage_valid && s_state.current_valid;
    if (s_state.power_valid) {
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
    } else {
        s_state.power_w = 0;
    }
}

static void zhike_parse_frame(const uint8_t *frame)
{
    /* 电机转速：p1.d.h() = z(word6) / 5.46 rpm（5.46 计数 = 1 rpm），z() 即 int16 */
    const int16_t raw_rpm = (int16_t)zhike_word(frame, 6U);
    if (raw_rpm >= 0) {
        const uint32_t rounded = ((uint32_t)raw_rpm * 100U + 273U) / 546U;
        s_state.rpm = (uint16_t)((rounded > UINT16_MAX) ? UINT16_MAX : rounded);
        s_state.rpm_valid = true;
    } else {
        s_state.rpm = 0U;
        s_state.rpm_valid = false;
    }

    /* 母线电压：p1.d.d() = word8 / 273.0666667 V，而 273.0666667 = 4096 / 15，
     * 所以 0.1V 单位值 = word8 * 150 / 4096 */
    const uint32_t deci = (((uint32_t)zhike_word(frame, 8U) * 150U) + 2047U) / 4096U;
    if (deci > 0U && deci <= UINT16_MAX) {
        s_state.voltage_deci_v = (uint16_t)deci;
        s_state.voltage_valid = true;
    } else {
        s_state.voltage_deci_v = 0U;
        s_state.voltage_valid = false;
    }

    /* 母线电流：p1.d.c() = z(word9) A（整安培），转 0.01A */
    s_state.current_centi_a = (int32_t)(int16_t)zhike_word(frame, 9U) * 100;
    s_state.current_valid = true;

    /* 控制器温度：p1.d.e() = word18 / 100 °C（百分之一摄氏度，无符号解析） */
    const uint16_t raw_temp = zhike_word(frame, 18U);
    if (raw_temp <= ZHIKE_TEMP_RAW_MAX) {
        s_state.controller_temp_c = (int16_t)((raw_temp + 50U) / 100U);
        s_state.controller_temp_valid = true;
    } else {
        s_state.controller_temp_c = 0;
        s_state.controller_temp_valid = false;
    }

    /* 状态字 word21：高字节 = 驾驶模式（1 才有效），bit4-5 = 挡位（p1.v()） */
    const uint16_t state_word = zhike_word(frame, 21U);
    if (((state_word >> 8U) & 0xFFU) == 1U) {
        const uint16_t bits = (state_word >> 4U) & 0x03U;
        /* p1.v()：1 = high、2 = low，其余 medium；映射到 1..3 挡 */
        s_state.gear = (bits == 1U) ? 3U : ((bits == 2U) ? 1U : 2U);
        s_state.gear_valid = true;
    } else {
        s_state.gear = 0U;
        s_state.gear_valid = false;
    }

    /* 故障位图 word22（p1.d.f()） */
    s_state.fault_code = zhike_word(frame, 22U);
    s_state.fault_valid = true;

    zhike_refresh_derived();
}

void esp_bms_ctrl_zhike_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_zhike_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    (void)attempt; /* Motexa 只定义了一条初始化命令，没有固件变体轮换 */
    if (!out || !out_len) {
        return false;
    }
    memcpy(out, ZHIKE_INIT_COMMAND, sizeof(ZHIKE_INIT_COMMAND));
    *out_len = sizeof(ZHIKE_INIT_COMMAND);
    return true;
}

bool esp_bms_ctrl_zhike_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;       /* 实时读取帧每一拍都相同 */
    (void)elapsed_ms; /* 周期由协议表的 poll_period_ms 控制 */
    if (!out || !out_len) {
        return false;
    }
    memcpy(out, ZHIKE_POLL_COMMAND, sizeof(ZHIKE_POLL_COMMAND));
    *out_len = sizeof(ZHIKE_POLL_COMMAND);
    return true;
}

bool esp_bms_ctrl_zhike_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > ZHIKE_STREAM_CAPACITY) {
        /* 超长通知只保留尾部，避免把老数据挤进解析窗口 */
        chunk += len - ZHIKE_STREAM_CAPACITY;
        len = ZHIKE_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > ZHIKE_STREAM_CAPACITY) {
        zhike_compact_stream(s_stream_len + len - ZHIKE_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        const uint8_t *window = s_stream + offset;
        const size_t avail = s_stream_len - offset;
        const size_t header = zhike_find_header(window, avail);
        if (header == SIZE_MAX) {
            /* 没有帧头：只保留可能是半个帧头的尾部 */
            offset += avail - zhike_partial_header_suffix(window, avail);
            break;
        }
        if (header > 0U) {
            offset += header;
            continue;
        }
        if (avail >= ZHIKE_FRAME_LEN) {
            if (zhike_checksum_ok(window)) {
                zhike_parse_frame(window);
                parsed = true;
                offset += ZHIKE_FRAME_LEN;
                continue;
            }
            if (zhike_short_response_confirmed(window, avail)) {
                /* 4 字节短应答本身合法，但不含遥测 */
                parsed = true;
                offset += ZHIKE_SHORT_RESPONSE_LEN;
                continue;
            }
            /* 67 字节窗口校验失败：丢 1 字节重新同步 */
            offset += 1U;
            continue;
        }
        if (zhike_has_short_prefix(window, avail)) {
            /* 短应答已到但还不能确认（后面没有帧头）：保持缓冲，等下一片再定 */
            parsed = true;
            break;
        }
        break; /* 数据不足，等下一片 */
    }
    zhike_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_zhike_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    /* 智科实时帧没有车速、电机温度、油门百分比（只有油门电压）字段 */
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
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
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
}
