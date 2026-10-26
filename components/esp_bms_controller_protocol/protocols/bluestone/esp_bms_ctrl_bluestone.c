#include "esp_bms_ctrl_bluestone.h"

#include <string.h>

/* 蓝石 BlueStone：移植自 Motexa 2.7.0 的 com.motexaqixing.app.j0。
 *
 * 通知帧（设备 -> App）
 *   55 | 页类型 | 02 | 数据 | SUM8
 *   - 页类型 0xE0：实时数据页，总长 56 字节（j0.F：i9 == 224 -> 56）
 *   - 页类型 0x70：命令应答页，总长 16 字节（j0.F：i9 == 112 -> 16）
 *   - SUM8：末字节 == 前 N-1 字节的算术和低 8 位（j0.G / j0.H / j0.C）
 *
 * 实时页里的模拟量都是【非对齐的小端 IEEE754 float32】（j0.K / j0.L）：
 *   偏移 11 转速 rpm、15 母线电压 V、19 母线电流 A、23 相电流 A、
 *   偏移 31 控制器温度 ℃、39 转把电压 V。
 *   j0.L 会按区间校验，越界返回 null（视为该字段无效），本文件保留同样的语义。
 *
 * 下行帧（App -> 设备）
 *   55 | 70 | 06 | 00 00 00 | 9 字节命令 | SUM8，总长 16 字节（j0.x）
 *   命令串第 2 字节为功能码：0x42 开启录像机、0x40 读实时页、0x15 0x19 补发录像机请求。 */

#define BLUESTONE_HEADER 0x55U
#define BLUESTONE_PAGE_MARK 0x02U
#define BLUESTONE_PAGE_REALTIME 0xE0U
#define BLUESTONE_PAGE_ACK 0x70U
#define BLUESTONE_FRAME_REALTIME_LEN 56U
#define BLUESTONE_FRAME_ACK_LEN 16U
#define BLUESTONE_COMMAND_LEN 16U
#define BLUESTONE_COMMAND_PAYLOAD_LEN 9U
#define BLUESTONE_COMMAND_DATA_OFFSET 6U
#define BLUESTONE_STREAM_CAPACITY 128U

/* 实时页字段偏移 */
#define BLUESTONE_OFF_RPM 11U
#define BLUESTONE_OFF_VOLTAGE 15U
#define BLUESTONE_OFF_BUS_CURRENT 19U
#define BLUESTONE_OFF_PHASE_CURRENT 23U
#define BLUESTONE_OFF_CONTROLLER_TEMP 31U
#define BLUESTONE_OFF_THROTTLE_VOLTAGE 39U

/* j0.L 的区间校验上下限（越界即丢弃该字段） */
#define BLUESTONE_RPM_MIN (-2000.0)
#define BLUESTONE_RPM_MAX 2000.0
#define BLUESTONE_VOLTAGE_MIN 0.0
#define BLUESTONE_VOLTAGE_MAX 200.0
#define BLUESTONE_BUS_CURRENT_MIN (-200.0)
#define BLUESTONE_BUS_CURRENT_MAX 200.0
#define BLUESTONE_CONTROLLER_TEMP_MIN (-60.0)
#define BLUESTONE_CONTROLLER_TEMP_MAX 200.0
#define BLUESTONE_THROTTLE_VOLTAGE_MIN 0.0
#define BLUESTONE_THROTTLE_VOLTAGE_MAX 10.0

/* x3.a() 的应用侧默认整车参数 / 转把标定（蓝石不下发标定帧） */
#define BLUESTONE_WHEEL_CIRCUMFERENCE_MM 1414.0
#define BLUESTONE_THROTTLE_IDLE_V 0.85
#define BLUESTONE_THROTTLE_FULL_V 4.2
/* kl / j0 判定转把电压超过 4.95V 时标记 INVALID */
#define BLUESTONE_THROTTLE_ADC_LIMIT_V 4.95
/* i0 运动滤波器只接受不超过 160km/h 的车速 */
#define BLUESTONE_SPEED_LIMIT_KMH 160.0

/* 下行命令 9 字节参数串（j0.A / j0.z / j0.y） */
static const uint8_t BLUESTONE_CMD_RECORDER_OPEN[BLUESTONE_COMMAND_PAYLOAD_LEN] = {
    0x08U, 0x42U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
};
static const uint8_t BLUESTONE_CMD_REALTIME_READ[BLUESTONE_COMMAND_PAYLOAD_LEN] = {
    0x08U, 0x40U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
};
static const uint8_t BLUESTONE_CMD_RECORDER_RETRY[BLUESTONE_COMMAND_PAYLOAD_LEN] = {
    0x08U, 0x15U, 0x19U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
};

typedef struct {
    bool voltage_valid;
    uint16_t voltage_deci_v;
    bool current_valid;
    int32_t current_centi_a;
    bool power_valid;
    int32_t power_w;
    bool controller_temp_valid;
    int16_t controller_temp_c;
    bool rpm_valid;
    uint16_t rpm;
    bool speed_valid;
    uint16_t speed_deci_kmh;
    bool throttle_valid;
    uint8_t throttle_percent;
} bluestone_state_t;

static bluestone_state_t s_state;
static uint8_t s_stream[BLUESTONE_STREAM_CAPACITY];
static size_t s_stream_len;

static uint8_t bluestone_sum8(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum += data[index];
    }
    return (uint8_t)(sum & 0xFFU);
}

static bool bluestone_checksum_ok(const uint8_t *frame, size_t len)
{
    if (!frame || len < 2U) {
        return false;
    }
    return frame[len - 1U] == bluestone_sum8(frame, len - 1U);
}

static int32_t bluestone_round_i32(double value)
{
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

/* j0.L：按小端 float32 读取，越界或非有限值返回 false（对应 Java 返回 null） */
static bool bluestone_read_f32(const uint8_t *data, size_t len, size_t offset, double min, double max,
                               double *out)
{
    if (!data || !out || offset > len || len - offset < 4U) {
        return false;
    }
    const uint32_t raw = (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8U) |
                         ((uint32_t)data[offset + 2U] << 16U) | ((uint32_t)data[offset + 3U] << 24U);
    if ((raw & 0x7F800000U) == 0x7F800000U) {
        /* Inf / NaN 一律丢弃，不参与换算 */
        return false;
    }
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    const double wide = (double)value;
    if (!(wide >= min) || !(wide <= max)) {
        return false;
    }
    *out = wide;
    return true;
}

/* v6.o.d + i0.f：speed = |rpm| * 周长(mm) * 60 / 1e6 (km/h) */
static bool bluestone_speed_from_rpm(double rpm, uint16_t *out_deci_kmh)
{
    if (!out_deci_kmh) {
        return false;
    }
    const double magnitude = rpm < 0.0 ? -rpm : rpm;
    if (magnitude > BLUESTONE_RPM_MAX) {
        return false;
    }
    const double kmh = (magnitude * BLUESTONE_WHEEL_CIRCUMFERENCE_MM * 60.0) / 1000000.0;
    if (!(kmh <= BLUESTONE_SPEED_LIMIT_KMH)) {
        return false;
    }
    const double deci = kmh * 10.0;
    if (deci > 65535.0) {
        return false;
    }
    *out_deci_kmh = (uint16_t)(deci + 0.5);
    return true;
}

/* s3.d：转把电压 -> 0..100%（低于怠速电压为 0，高于满量程为 100） */
static uint8_t bluestone_throttle_percent(double volts)
{
    const double span = BLUESTONE_THROTTLE_FULL_V - BLUESTONE_THROTTLE_IDLE_V;
    if (!(span > 0.0)) {
        return 0U;
    }
    double percent = ((volts - BLUESTONE_THROTTLE_IDLE_V) / span) * 100.0;
    if (percent < 0.0) {
        percent = 0.0;
    }
    if (percent > 100.0) {
        percent = 100.0;
    }
    return (uint8_t)(percent + 0.5);
}

static void bluestone_parse_realtime(const uint8_t *frame, size_t len)
{
    double value = 0.0;

    if (bluestone_read_f32(frame, len, BLUESTONE_OFF_VOLTAGE, BLUESTONE_VOLTAGE_MIN,
                           BLUESTONE_VOLTAGE_MAX, &value)) {
        s_state.voltage_valid = true;
        s_state.voltage_deci_v = (uint16_t)bluestone_round_i32(value * 10.0);
    } else {
        s_state.voltage_valid = false;
        s_state.voltage_deci_v = 0U;
    }

    if (bluestone_read_f32(frame, len, BLUESTONE_OFF_BUS_CURRENT, BLUESTONE_BUS_CURRENT_MIN,
                           BLUESTONE_BUS_CURRENT_MAX, &value)) {
        s_state.current_valid = true;
        s_state.current_centi_a = bluestone_round_i32(value * 100.0);
    } else {
        s_state.current_valid = false;
        s_state.current_centi_a = 0;
    }

    if (s_state.voltage_valid && s_state.current_valid) {
        /* s3.b：功率 = 母线电压 * 母线电流 */
        s_state.power_valid = true;
        s_state.power_w =
            (int32_t)(((int64_t)s_state.voltage_deci_v * (int64_t)s_state.current_centi_a) / 1000);
    } else {
        s_state.power_valid = false;
        s_state.power_w = 0;
    }

    if (bluestone_read_f32(frame, len, BLUESTONE_OFF_CONTROLLER_TEMP, BLUESTONE_CONTROLLER_TEMP_MIN,
                           BLUESTONE_CONTROLLER_TEMP_MAX, &value)) {
        s_state.controller_temp_valid = true;
        s_state.controller_temp_c = (int16_t)bluestone_round_i32(value);
    } else {
        s_state.controller_temp_valid = false;
        s_state.controller_temp_c = 0;
    }

    if (bluestone_read_f32(frame, len, BLUESTONE_OFF_RPM, BLUESTONE_RPM_MIN, BLUESTONE_RPM_MAX, &value)) {
        const double magnitude = value < 0.0 ? -value : value;
        uint16_t speed_deci = 0U;
        s_state.rpm_valid = true;
        s_state.rpm = (uint16_t)bluestone_round_i32(magnitude);
        s_state.speed_valid = bluestone_speed_from_rpm(value, &speed_deci);
        s_state.speed_deci_kmh = s_state.speed_valid ? speed_deci : 0U;
    } else {
        s_state.rpm_valid = false;
        s_state.rpm = 0U;
        s_state.speed_valid = false;
        s_state.speed_deci_kmh = 0U;
    }

    if (bluestone_read_f32(frame, len, BLUESTONE_OFF_THROTTLE_VOLTAGE, BLUESTONE_THROTTLE_VOLTAGE_MIN,
                           BLUESTONE_THROTTLE_VOLTAGE_MAX, &value) &&
        value <= BLUESTONE_THROTTLE_ADC_LIMIT_V) {
        s_state.throttle_valid = true;
        s_state.throttle_percent = bluestone_throttle_percent(value);
    } else {
        s_state.throttle_valid = false;
        s_state.throttle_percent = 0U;
    }
}

static void bluestone_compact_stream(size_t consumed)
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

static void bluestone_build_command(const uint8_t payload[BLUESTONE_COMMAND_PAYLOAD_LEN], uint8_t *out)
{
    memset(out, 0, BLUESTONE_COMMAND_LEN);
    out[0] = BLUESTONE_HEADER;
    out[1] = BLUESTONE_PAGE_ACK;
    out[2] = 0x06U;
    memcpy(out + BLUESTONE_COMMAND_DATA_OFFSET, payload, BLUESTONE_COMMAND_PAYLOAD_LEN);
    out[BLUESTONE_COMMAND_LEN - 1U] = bluestone_sum8(out, BLUESTONE_COMMAND_LEN - 1U);
}

void esp_bms_ctrl_bluestone_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_stream, 0, sizeof(s_stream));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_bluestone_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* 首拍开启录像机（j0.A，功能码 0x42）；重试拍补发录像机请求（j0.y，0x15 0x19），
     * 覆盖只认第二种写法的固件版本。 */
    const uint8_t *payload =
        ((attempt % 2U) == 0U) ? BLUESTONE_CMD_RECORDER_OPEN : BLUESTONE_CMD_RECORDER_RETRY;
    bluestone_build_command(payload, out);
    *out_len = BLUESTONE_COMMAND_LEN;
    return true;
}

bool esp_bms_ctrl_bluestone_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;
    if (!out || !out_len) {
        return false;
    }
    /* elapsed_ms = 距上次收到有效帧的毫秒数。
     * 超过 j0 的 1500ms 实时超时窗口时补发录像机请求（j0.J -> j0.y），否则读实时页（j0.z）。 */
    const uint8_t *payload =
        (elapsed_ms >= 1500U) ? BLUESTONE_CMD_RECORDER_RETRY : BLUESTONE_CMD_REALTIME_READ;
    bluestone_build_command(payload, out);
    *out_len = BLUESTONE_COMMAND_LEN;
    return true;
}

bool esp_bms_ctrl_bluestone_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > BLUESTONE_STREAM_CAPACITY) {
        /* 超长通知只保留尾部，避免把陈旧字节挤进解析窗口 */
        chunk += len - BLUESTONE_STREAM_CAPACITY;
        len = BLUESTONE_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > BLUESTONE_STREAM_CAPACITY) {
        bluestone_compact_stream(s_stream_len + len - BLUESTONE_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= 3U) {
        const uint8_t *candidate = s_stream + offset;
        const size_t available = s_stream_len - offset;
        if (candidate[0] != BLUESTONE_HEADER || candidate[2] != BLUESTONE_PAGE_MARK ||
            (candidate[1] != BLUESTONE_PAGE_REALTIME && candidate[1] != BLUESTONE_PAGE_ACK)) {
            offset++;
            continue;
        }
        const size_t frame_len = (candidate[1] == BLUESTONE_PAGE_REALTIME) ? BLUESTONE_FRAME_REALTIME_LEN
                                                                          : BLUESTONE_FRAME_ACK_LEN;
        if (frame_len > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            offset++;
            continue;
        }
        if (available < frame_len) {
            /* 分片：整个候选帧留在缓冲里等后半段（等价于 j0.F 的提前 return） */
            break;
        }
        if (!bluestone_checksum_ok(candidate, frame_len)) {
            offset++;
            continue;
        }
        if (candidate[1] == BLUESTONE_PAGE_REALTIME) {
            bluestone_parse_realtime(candidate, frame_len);
            parsed = true;
        }
        /* 0x70 命令应答页不含遥测（j0.F 只在 0xE0 调用 j0.v），按帧长消费即可 */
        offset += frame_len;
    }
    bluestone_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_bluestone_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->voltage_valid = s_state.voltage_valid;
    out->voltage_deci_v = s_state.voltage_deci_v;
    out->current_valid = s_state.current_valid;
    out->current_centi_a = s_state.current_centi_a;
    out->power_valid = s_state.power_valid;
    out->power_w = s_state.power_w;
    out->controller_temp_valid = s_state.controller_temp_valid;
    out->controller_temp_c = s_state.controller_temp_c;
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
    out->speed_valid = s_state.speed_valid;
    out->speed_deci_kmh = s_state.speed_deci_kmh;
    out->throttle_valid = s_state.throttle_valid;
    out->throttle_percent = s_state.throttle_percent;
    /* 车速按 Motexa 应用侧默认周长换算（蓝石自己不上报轮胎参数），
     * 因此 controller_speed_params_valid 保持 false。 */
    out->wheel_circumference_mm = 1414U;
}
