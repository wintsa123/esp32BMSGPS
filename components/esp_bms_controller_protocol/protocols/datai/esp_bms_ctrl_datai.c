#include "esp_bms_ctrl_datai.h"

#include <string.h>

/* 大泰 Datai（Motexa 2.7.0 / v6.s）。
 *
 * 帧格式：01 05 02 | type | sub | payload_len | payload... | sum8
 *   [0..2] 帧头常量 v6.s.f19758g = {1, 5, 2}
 *   [3]    type（0 = RTD 实时）
 *   [4]    sub（0 = RTD 实时）
 *   [5]    payload 长度
 *   [6..]  payload（长度为 [5]）
 *   末字节 sum8 = (byte[0] + ... + byte[n-2]) & 0xFF（v6.s.G()）
 *   整帧长度 = payload_len + 7（v6.s：iG = w0.g(bArr, 5) + 7）
 *
 * 请求帧 05 01 01 00 00 00 07（v6.s.f19759h）：读 RTD 块，地址 0x0000。
 * 同一帧既是"开启"帧也是周期轮询帧——Motexa 的 realtime profile 只发这一条读取请求。
 *
 * RTD payload 偏移（v6.s.K，小端）：
 *   0  vehicle_speed_kmh   s16  km/h       （Integer，不做小数缩放）
 *   2  bus_voltage_v       u16  /10 V
 *   4  bus_current_a       s16  /10 A
 *   6  phase_current_a     s16  /10 A      （遥测契约无对应字段）
 *   8  motor_temperature_c s16  /10 °C
 *   10 pcu_temperature_c   s16  /10 °C     → 控制器温度
 *   12 fault_1             u32  位图 1
 *   16 fault_2             u32  位图 2
 *   20 state_1             u32
 *   24 motor_rpm           u16  rpm
 *   26 throttle_percent    u16  0..100（Java 侧 Math.max(0, Math.min(100, v)) 截断）
 *   28 pitch/roll/yaw      s16  /32        （payload >= 34 才有，遥测契约无对应字段） */

#define DATAI_HEADER_LEN 3U
#define DATAI_LENGTH_OFFSET 5U
#define DATAI_PAYLOAD_OFFSET 6U
#define DATAI_MIN_FRAME_LEN 7U
#define DATAI_RTD_MIN_PAYLOAD 28U
#define DATAI_STREAM_CAPACITY 256U /* 覆盖 MTU 247 的通知整包（7 帧 × 35B） */
#define DATAI_TEMP_RAW_MIN (-500)  /* -50.0 °C */
#define DATAI_TEMP_RAW_MAX 2000    /* 200.0 °C */
#define DATAI_SPEED_KMH_MAX 6553   /* 6553 km/h → 65530 deci，超过 uint16 会溢出 */

static const uint8_t DATAI_HEADER[DATAI_HEADER_LEN] = { 0x01U, 0x05U, 0x02U };
static const uint8_t DATAI_RTD_REQUEST[] = { 0x05U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x07U };

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
    bool fault_valid;
    uint16_t fault_code;
} datai_state_t;

static datai_state_t s_state;
static uint8_t s_stream[DATAI_STREAM_CAPACITY];
static size_t s_stream_len;

static uint16_t datai_le16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[1] << 8U) | (uint16_t)data[0]);
}

static int16_t datai_sle16(const uint8_t *data)
{
    return (int16_t)datai_le16(data);
}

/* Java v6.p1.F 等价的"四舍五入到整数"：raw 为十分之一单位 */
static int16_t datai_deci_to_int(int16_t raw)
{
    if (raw >= 0) {
        return (int16_t)((raw + 5) / 10);
    }
    return (int16_t)(-((-raw + 5) / 10));
}

static void datai_compact_stream(size_t consumed)
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

/* 帧头下标；找不到返回 SIZE_MAX */
static size_t datai_find_header(const uint8_t *data, size_t len)
{
    if (len < DATAI_HEADER_LEN) {
        return SIZE_MAX;
    }
    for (size_t index = 0U; index + DATAI_HEADER_LEN <= len; ++index) {
        if (memcmp(data + index, DATAI_HEADER, DATAI_HEADER_LEN) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

static size_t datai_partial_header_suffix(const uint8_t *data, size_t len)
{
    for (size_t keep = DATAI_HEADER_LEN - 1U; keep > 0U; --keep) {
        if (len < keep) {
            continue;
        }
        if (memcmp(data + len - keep, DATAI_HEADER, keep) == 0) {
            return keep;
        }
    }
    return 0U;
}

static bool datai_checksum_ok(const uint8_t *frame, size_t frame_len)
{
    uint8_t sum = 0U;
    for (size_t index = 0U; index + 1U < frame_len; ++index) {
        sum = (uint8_t)(sum + frame[index]);
    }
    return sum == frame[frame_len - 1U];
}

static void datai_refresh_derived(void)
{
    /* 功率 = 电压 × 电流（0.1V × 0.01A / 1000 = W） */
    s_state.power_valid = s_state.voltage_valid && s_state.current_valid;
    if (s_state.power_valid) {
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
    } else {
        s_state.power_w = 0;
    }
}

static void datai_parse_rtd(const uint8_t *payload, size_t payload_len)
{
    if (payload_len < DATAI_RTD_MIN_PAYLOAD) {
        return;
    }

    /* 车速：s16 km/h，Java 直接展示整数 km/h */
    const int16_t speed_kmh = datai_sle16(payload);
    if (speed_kmh >= 0 && speed_kmh <= DATAI_SPEED_KMH_MAX) {
        s_state.speed_deci_kmh = (uint16_t)((uint16_t)speed_kmh * 10U);
        s_state.speed_valid = true;
    } else {
        s_state.speed_deci_kmh = 0U;
        s_state.speed_valid = false;
    }

    /* 母线电压：u16 / 10 V，0.1V 单位即原始值 */
    const uint16_t voltage_deci = datai_le16(payload + 2U);
    if (voltage_deci > 0U) {
        s_state.voltage_deci_v = voltage_deci;
        s_state.voltage_valid = true;
    } else {
        s_state.voltage_deci_v = 0U;
        s_state.voltage_valid = false;
    }

    /* 母线电流：s16 / 10 A → 0.01A */
    s_state.current_centi_a = (int32_t)datai_sle16(payload + 4U) * 10;
    s_state.current_valid = true;

    /* 电机温度 / 控制器（PCU）温度：s16 / 10 °C，越界丢弃 */
    const int16_t motor_temp_raw = datai_sle16(payload + 8U);
    if (motor_temp_raw >= DATAI_TEMP_RAW_MIN && motor_temp_raw <= DATAI_TEMP_RAW_MAX) {
        s_state.motor_temp_c = datai_deci_to_int(motor_temp_raw);
        s_state.motor_temp_valid = true;
    } else {
        s_state.motor_temp_c = 0;
        s_state.motor_temp_valid = false;
    }

    const int16_t pcu_temp_raw = datai_sle16(payload + 10U);
    if (pcu_temp_raw >= DATAI_TEMP_RAW_MIN && pcu_temp_raw <= DATAI_TEMP_RAW_MAX) {
        s_state.controller_temp_c = datai_deci_to_int(pcu_temp_raw);
        s_state.controller_temp_valid = true;
    } else {
        s_state.controller_temp_c = 0;
        s_state.controller_temp_valid = false;
    }

    /* 故障位图 1（u32）低 16 位；遥测契约只有 16 位故障码 */
    const uint32_t fault_1 = (uint32_t)payload[12U] | ((uint32_t)payload[13U] << 8U) |
                             ((uint32_t)payload[14U] << 16U) | ((uint32_t)payload[15U] << 24U);
    s_state.fault_code = (uint16_t)(fault_1 & 0xFFFFU);
    s_state.fault_valid = true;

    /* 电机转速：u16 rpm */
    s_state.rpm = datai_le16(payload + 24U);
    s_state.rpm_valid = true;

    /* 油门百分比：Java 侧截断到 0..100，这里保持一致 */
    const uint16_t throttle_raw = datai_le16(payload + 26U);
    s_state.throttle_percent = (throttle_raw > 100U) ? 100U : (uint8_t)throttle_raw;
    s_state.throttle_valid = true;

    datai_refresh_derived();
}

static void datai_parse_frame(const uint8_t *frame, size_t frame_len)
{
    const uint8_t type = frame[3U];
    const uint8_t sub = frame[4U];
    const size_t payload_len = frame_len - DATAI_MIN_FRAME_LEN;
    if (type == 0U && sub == 0U) {
        datai_parse_rtd(frame + DATAI_PAYLOAD_OFFSET, payload_len);
    }
    /* 其它 type（产品信息、参数组、被动自学习回包）在本移植里不产出遥测 */
}

void esp_bms_ctrl_datai_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_datai_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    (void)attempt; /* 只有一个 realtime 请求帧，没有固件变体轮换 */
    if (!out || !out_len) {
        return false;
    }
    memcpy(out, DATAI_RTD_REQUEST, sizeof(DATAI_RTD_REQUEST));
    *out_len = sizeof(DATAI_RTD_REQUEST);
    return true;
}

bool esp_bms_ctrl_datai_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;       /* realtime profile 每一拍重发同一条 RTD 读取 */
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    memcpy(out, DATAI_RTD_REQUEST, sizeof(DATAI_RTD_REQUEST));
    *out_len = sizeof(DATAI_RTD_REQUEST);
    return true;
}

bool esp_bms_ctrl_datai_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > DATAI_STREAM_CAPACITY) {
        /* 超长通知只保留尾部（Java 侧上限 2048 字节，这里按帧上限收紧） */
        chunk += len - DATAI_STREAM_CAPACITY;
        len = DATAI_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > DATAI_STREAM_CAPACITY) {
        datai_compact_stream(s_stream_len + len - DATAI_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        const uint8_t *window = s_stream + offset;
        const size_t avail = s_stream_len - offset;
        const size_t header = datai_find_header(window, avail);
        if (header == SIZE_MAX) {
            offset += avail - datai_partial_header_suffix(window, avail);
            break;
        }
        if (header > 0U) {
            offset += header;
            continue;
        }
        if (avail < DATAI_PAYLOAD_OFFSET) {
            break; /* 连长度字节都没到 */
        }
        const size_t frame_len = (size_t)window[DATAI_LENGTH_OFFSET] + DATAI_MIN_FRAME_LEN;
        if (frame_len < DATAI_MIN_FRAME_LEN || frame_len > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            /* 长度字段不可信或超长：丢 1 字节重新同步，绝不越界读 */
            offset += 1U;
            continue;
        }
        if (avail < frame_len) {
            break; /* 等下一片补齐 */
        }
        if (!datai_checksum_ok(window, frame_len)) {
            /* 校验失败：丢 1 字节，保留后续可能的完整帧 */
            offset += 1U;
            continue;
        }
        datai_parse_frame(window, frame_len);
        parsed = true;
        offset += frame_len;
    }
    datai_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_datai_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    /* 大泰 RTD 帧没有挡位、轮胎参数字段 */
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
    out->speed_valid = s_state.speed_valid;
    out->speed_deci_kmh = s_state.speed_deci_kmh;
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
}
