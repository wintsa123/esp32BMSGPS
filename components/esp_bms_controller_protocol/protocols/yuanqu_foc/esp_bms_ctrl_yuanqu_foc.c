#include "esp_bms_ctrl_yuanqu_foc.h"

#include <string.h>

/* 远驱 YuanQu FOC：移植自 Motexa 2.7.0 的 com.motexaqixing.app.jl（适配器）
 * + v6.n1（帧引擎/分页解析）+ kl（字段解释）。
 *
 * 通知帧（设备 -> App，固定 16 字节，v6.n1.D）
 *   AA | 页号 | 12 字节数据 | SUM16(大端)
 *   SUM16 = 前 14 字节算术和低 16 位；页号决定这 12 字节的含义（v6.n1.H）。
 *     0x00 转速/驱动状态/故障位图     0x01 母线电压/电流/调制比/转把深度
 *     0x02 相电流（sqrt 编码）        0x04 控制器(MOS)温度
 *     0x08 极对数                     0x0A 电池容量原始值
 *     0x0D 电机温度原始值 + 转把电压  0x13/0x14 设备标识分片
 *     0x16/0x17 发现流程              0x35 备用连接状态
 *
 * 下行帧（App -> 设备，8 字节已审查保活，v6.n1.g + v6.n1.f19xxx 常量）
 *   AA 17 E8 F1 13 51 FE 01   发现/保活（未验证发现结果时）
 *   AA 13 EC 07 5F 5F 6E 91   发现已验证且连接状态码 == 0
 *   AA 13 EC 07 09 6F 28 D7   发现已验证且连接状态码 >  0
 *
 * 本模块不下发身份、参数、曲线、升级类写入；也不做 v6.n1.Q 的设备标识比对
 * （那需要完整的发现流程与厂商输入），只把页号 0x00/0x35 的连接状态码作为
 * 选择保活变体的依据。 */

#define YUANQU_HEADER 0xAAU
#define YUANQU_FRAME_LEN 16U
#define YUANQU_DATA_OFFSET 2U
#define YUANQU_DATA_LEN 12U
#define YUANQU_SUM_OFFSET 14U
#define YUANQU_STREAM_CAPACITY 128U
#define YUANQU_KEEPALIVE_LEN 8U

#define YUANQU_PAGE_STATUS 0x00U
#define YUANQU_PAGE_POWER 0x01U
#define YUANQU_PAGE_PHASE 0x02U
#define YUANQU_PAGE_TEMPERATURE 0x04U
#define YUANQU_PAGE_POLES 0x08U
#define YUANQU_PAGE_CAPACITY 0x0AU
#define YUANQU_PAGE_MOTOR 0x0DU
#define YUANQU_PAGE_ID_LOW 0x13U
#define YUANQU_PAGE_ID_HIGH 0x14U
#define YUANQU_PAGE_DISCOVERY_A 0x16U
#define YUANQU_PAGE_DISCOVERY_B 0x17U
#define YUANQU_PAGE_PASS_ALTERNATE 0x35U

/* 页内偏移（绝对帧偏移） */
#define YUANQU_OFF_STATE 4U
#define YUANQU_OFF_PASS_CODE 5U
#define YUANQU_OFF_RPM 6U
#define YUANQU_OFF_FAULT_0 8U
#define YUANQU_OFF_FAULT_1 9U
#define YUANQU_OFF_VOLTAGE 2U
#define YUANQU_OFF_CURRENT 4U
#define YUANQU_OFF_MODULATION 6U
#define YUANQU_OFF_WEAK_FIELD 7U
#define YUANQU_OFF_THROTTLE_DEPTH 12U
#define YUANQU_OFF_MOS_TEMP 4U
#define YUANQU_OFF_MOTOR_TEMP 2U
#define YUANQU_OFF_THROTTLE_VOLTAGE 4U
#define YUANQU_OFF_POLE_PAIRS 10U
#define YUANQU_OFF_PASS_ALTERNATE 10U

/* 母线电压 1 个 count = 0.1V、母线电流 1 个 count = 0.25A（v6.n1.J） */
#define YUANQU_VOLTAGE_SCALE 10.0
#define YUANQU_CURRENT_SCALE 4.0
/* 转把 ADC：raw * 3.3 * 1.5 / 4096 V（v6.n1.O） */
#define YUANQU_THROTTLE_ADC_GAIN ((3.3 * 1.5) / 4096.0)

/* 转把标定（x3.a() 默认值）与 kl 的 4.95V 有效上限 */
#define YUANQU_THROTTLE_IDLE_V 0.85
#define YUANQU_THROTTLE_FULL_V 4.2
#define YUANQU_THROTTLE_ADC_LIMIT_V 4.95

/* 物理区间（与蓝石基线一致的合理性闸门，越界即丢弃该字段） */
#define YUANQU_VOLTAGE_MAX_COUNTS 2000 /* 200.0V */
#define YUANQU_CURRENT_MAX_COUNTS 800  /* ±200A */
#define YUANQU_TEMP_MAX 200

static const uint8_t YUANQU_KEEPALIVE_DISCOVERY[YUANQU_KEEPALIVE_LEN] = { 0xAAU, 0x17U, 0xE8U, 0xF1U,
                                                                         0x13U, 0x51U, 0xFEU, 0x01U };
static const uint8_t YUANQU_KEEPALIVE_PRIMARY_A[YUANQU_KEEPALIVE_LEN] = { 0xAAU, 0x13U, 0xECU, 0x07U,
                                                                         0x5FU, 0x5FU, 0x6EU, 0x91U };
static const uint8_t YUANQU_KEEPALIVE_PRIMARY_B[YUANQU_KEEPALIVE_LEN] = { 0xAAU, 0x13U, 0xECU, 0x07U,
                                                                         0x09U, 0x6FU, 0x28U, 0xD7U };

typedef struct {
    bool state_valid;
    uint8_t drive_code;
    uint16_t pass_code;
    bool fault_valid;
    uint16_t fault_code;
    bool stop_brake;
    bool gear_valid;
    uint8_t gear;
    bool rpm_valid;
    uint16_t rpm;
    bool voltage_valid;
    uint16_t voltage_deci_v;
    bool current_valid;
    int32_t current_centi_a;
    bool power_valid;
    int32_t power_w;
    bool controller_temp_valid;
    int16_t controller_temp_c;
    bool throttle_valid;
    uint8_t throttle_percent;
} yuanqu_foc_state_t;

static yuanqu_foc_state_t s_state;
static uint8_t s_stream[YUANQU_STREAM_CAPACITY];
static size_t s_stream_len;
static bool s_pass_code_known;

static uint16_t yuanqu_sum16(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum += data[index];
    }
    return (uint16_t)(sum & 0xFFFFU);
}

static bool yuanqu_checksum_ok(const uint8_t *frame)
{
    const uint16_t declared =
        (uint16_t)(((uint16_t)frame[YUANQU_SUM_OFFSET] << 8U) | frame[YUANQU_SUM_OFFSET + 1U]);
    return yuanqu_sum16(frame, YUANQU_SUM_OFFSET) == declared;
}

static int16_t yuanqu_i16_be(const uint8_t *data, size_t offset)
{
    return (int16_t)(((uint16_t)data[offset] << 8U) | data[offset + 1U]);
}

static uint16_t yuanqu_u16_be(const uint8_t *data, size_t offset)
{
    return (uint16_t)(((uint16_t)data[offset] << 8U) | data[offset + 1U]);
}

static int32_t yuanqu_round_i32(double value)
{
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

/* s3.d：转把电压 -> 0..100% */
static uint8_t yuanqu_throttle_percent(double volts)
{
    const double span = YUANQU_THROTTLE_FULL_V - YUANQU_THROTTLE_IDLE_V;
    if (!(span > 0.0)) {
        return 0U;
    }
    double percent = ((volts - YUANQU_THROTTLE_IDLE_V) / span) * 100.0;
    if (percent < 0.0) {
        percent = 0.0;
    }
    if (percent > 100.0) {
        percent = 100.0;
    }
    return (uint8_t)(percent + 0.5);
}

/* v6.n1.I：页 0x00 的转速/驱动状态/故障位图 */
static void yuanqu_parse_status(const uint8_t *frame)
{
    const uint8_t state_byte = frame[YUANQU_OFF_STATE];
    s_state.state_valid = true;
    s_state.drive_code = (uint8_t)(state_byte & 0x03U);
    /* xs-control-code = ((b >> 2) ^ 2) & 3，再映射成 1..3 挡（kl.m） */
    const uint8_t xs_code = (uint8_t)(((state_byte >> 2U) ^ 0x02U) & 0x03U);
    s_state.gear = (xs_code == 0U) ? 1U : ((xs_code == 1U) ? 2U : 3U);
    s_state.gear_valid = true;

    const int16_t raw_rpm = yuanqu_i16_be(frame, YUANQU_OFF_RPM);
    s_state.rpm_valid = true;
    s_state.rpm = raw_rpm < 0 ? (uint16_t)(-raw_rpm) : (uint16_t)raw_rpm;

    const uint16_t fault_0 = frame[YUANQU_OFF_FAULT_0];
    const uint16_t fault_1 = (uint16_t)(frame[YUANQU_OFF_FAULT_1] & 0x7FU);
    s_state.fault_valid = true;
    s_state.fault_code = (uint16_t)((fault_0 << 8U) | fault_1);
    s_state.stop_brake = (frame[YUANQU_OFF_FAULT_1] & 0x80U) != 0U;

    s_state.pass_code = (uint16_t)((frame[YUANQU_OFF_PASS_CODE] & 0x0CU) >> 2U);
    s_pass_code_known = true;
}

/* v6.n1.J：页 0x01 的母线电压/电流，功率由两者相乘得到 */
static void yuanqu_parse_power(const uint8_t *frame)
{
    const int16_t raw_voltage = yuanqu_i16_be(frame, YUANQU_OFF_VOLTAGE);
    const int16_t raw_current = yuanqu_i16_be(frame, YUANQU_OFF_CURRENT);

    if (raw_voltage > 0 && raw_voltage <= YUANQU_VOLTAGE_MAX_COUNTS) {
        s_state.voltage_valid = true;
        s_state.voltage_deci_v = (uint16_t)raw_voltage;
    } else {
        s_state.voltage_valid = false;
        s_state.voltage_deci_v = 0U;
    }

    if (raw_current >= -YUANQU_CURRENT_MAX_COUNTS && raw_current <= YUANQU_CURRENT_MAX_COUNTS) {
        s_state.current_valid = true;
        s_state.current_centi_a =
            yuanqu_round_i32(((double)raw_current / YUANQU_CURRENT_SCALE) * 100.0);
    } else {
        s_state.current_valid = false;
        s_state.current_centi_a = 0;
    }

    if (s_state.voltage_valid && s_state.current_valid) {
        s_state.power_valid = true;
        s_state.power_w =
            (int32_t)(((int64_t)s_state.voltage_deci_v * (int64_t)s_state.current_centi_a) / 1000);
    } else {
        s_state.power_valid = false;
        s_state.power_w = 0;
    }
}

/* v6.n1.M：页 0x04 的控制器（MOS）温度；0..200 之外视为无效 */
static void yuanqu_parse_temperature(const uint8_t *frame)
{
    const uint8_t raw = frame[YUANQU_OFF_MOS_TEMP];
    if (raw <= YUANQU_TEMP_MAX) {
        s_state.controller_temp_valid = true;
        s_state.controller_temp_c = (int16_t)raw;
    } else {
        s_state.controller_temp_valid = false;
        s_state.controller_temp_c = 0;
    }
}

/* v6.n1.O：页 0x0D 的转把电压（电机温度原始值因缺少温度传感器类型而不对外发布） */
static void yuanqu_parse_motor(const uint8_t *frame)
{
    const double volts = (double)yuanqu_u16_be(frame, YUANQU_OFF_THROTTLE_VOLTAGE) * YUANQU_THROTTLE_ADC_GAIN;
    if (volts >= 0.0 && volts <= YUANQU_THROTTLE_ADC_LIMIT_V) {
        s_state.throttle_valid = true;
        s_state.throttle_percent = yuanqu_throttle_percent(volts);
    } else {
        s_state.throttle_valid = false;
        s_state.throttle_percent = 0U;
    }
}

static void yuanqu_parse_frame(const uint8_t *frame)
{
    switch (frame[1]) {
    case YUANQU_PAGE_STATUS:
        yuanqu_parse_status(frame);
        break;
    case YUANQU_PAGE_POWER:
        yuanqu_parse_power(frame);
        break;
    case YUANQU_PAGE_TEMPERATURE:
        yuanqu_parse_temperature(frame);
        break;
    case YUANQU_PAGE_MOTOR:
        yuanqu_parse_motor(frame);
        break;
    case YUANQU_PAGE_PASS_ALTERNATE:
        s_state.pass_code = (uint16_t)(frame[YUANQU_OFF_PASS_ALTERNATE] & 0x03U);
        s_pass_code_known = true;
        break;
    default:
        /* 相电流（sqrt 编码）、极对数、容量、标识分片、发现页：本模块不产出遥测字段。 */
        break;
    }
}

static void yuanqu_compact_stream(size_t consumed)
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

static void yuanqu_copy_keepalive(const uint8_t *source, uint8_t *out, size_t *out_len)
{
    memcpy(out, source, YUANQU_KEEPALIVE_LEN);
    *out_len = YUANQU_KEEPALIVE_LEN;
}

/* 发现已验证后按连接状态码选择官方保活变体（v6.n1.g） */
static void yuanqu_build_active_keepalive(uint8_t *out, size_t *out_len)
{
    const bool positive = s_pass_code_known && s_state.pass_code > 0U;
    yuanqu_copy_keepalive(positive ? YUANQU_KEEPALIVE_PRIMARY_B : YUANQU_KEEPALIVE_PRIMARY_A, out, out_len);
}

void esp_bms_ctrl_yuanqu_foc_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_stream, 0, sizeof(s_stream));
    s_stream_len = 0U;
    s_pass_code_known = false;
}

bool esp_bms_ctrl_yuanqu_foc_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    if (attempt == 0U) {
        yuanqu_copy_keepalive(YUANQU_KEEPALIVE_DISCOVERY, out, out_len);
        return true;
    }
    yuanqu_build_active_keepalive(out, out_len);
    return true;
}

bool esp_bms_ctrl_yuanqu_foc_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    /* jl.E：保活按 350ms 周期无限重发，直到会话结束。 */
    yuanqu_build_active_keepalive(out, out_len);
    return true;
}

bool esp_bms_ctrl_yuanqu_foc_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > YUANQU_STREAM_CAPACITY) {
        chunk += len - YUANQU_STREAM_CAPACITY;
        len = YUANQU_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > YUANQU_STREAM_CAPACITY) {
        yuanqu_compact_stream(s_stream_len + len - YUANQU_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= YUANQU_FRAME_LEN) {
        const uint8_t *frame = s_stream + offset;
        if (frame[0] != YUANQU_HEADER) {
            offset++;
            continue;
        }
        if (!yuanqu_checksum_ok(frame)) {
            /* 校验失败（含另一族 CRC16 编码）：丢一个字节重新对齐（v6.n1.e） */
            offset++;
            continue;
        }
        yuanqu_parse_frame(frame);
        parsed = true;
        offset += YUANQU_FRAME_LEN;
    }
    yuanqu_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_yuanqu_foc_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
    out->gear_valid = s_state.gear_valid;
    out->gear = s_state.gear;
    out->voltage_valid = s_state.voltage_valid;
    out->voltage_deci_v = s_state.voltage_deci_v;
    out->current_valid = s_state.current_valid;
    out->current_centi_a = s_state.current_centi_a;
    out->power_valid = s_state.power_valid;
    out->power_w = s_state.power_w;
    out->controller_temp_valid = s_state.controller_temp_valid;
    out->controller_temp_c = s_state.controller_temp_c;
    out->throttle_valid = s_state.throttle_valid;
    out->throttle_percent = s_state.throttle_percent;
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
    /* 远驱 FOC 的旧 16 字节协议不上报轮胎/传动参数，kl 也不换算出车速：
     * 只给转速，speed_valid 保持 false。 */
}
