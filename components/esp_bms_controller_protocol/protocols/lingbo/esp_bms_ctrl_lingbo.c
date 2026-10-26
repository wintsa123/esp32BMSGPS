#include "esp_bms_ctrl_lingbo.h"

#include <string.h>

/* 凌博 Lingbo：移植自 Motexa 2.7.0 的 com.motexaqixing.app.a9。
 *
 * 帧格式（双向一致，a9.E / a9.G）
 *   AA | 55 | LEN | CMD | 数据(LEN-1 字节) | SUM8      总长 = LEN + 3
 *   LEN  >= 2，SUM8 = 前 N-1 字节算术和低 8 位。
 *
 * 下行请求（a9.b 枚举，只读白名单 "AA55022021,AA55022223"）
 *   AA 55 02 20 21   读标定参数（握手帧，必须先拿到才能换算出物理量）
 *   AA 55 02 22 23   读实时数据
 *
 * 上行应答 CMD
 *   0x60 标定参数：@4 相电流基准、@6 相电压基准、@8 母线电流换算分母、
 *                  @10 母线电压换算分母、@12 控制模式、@13 扭矩上限（a9.y）
 *   0x62 实时数据：@4 有符号转速、@10 母线电压原始值、@12 有符号母线电流原始值、
 *                  @14 控制器温度、@16 电机温度（推断）、@18 32 位故障位图、
 *                  @22 转把电压(0.01V)、@27 低 4 位挡位（a9.z）
 *   换算（a9.z）：电压 = raw/电压分母、电流 = raw/电流分母、功率 = 电压*电流。 */

#define LINGBO_HEADER_0 0xAAU
#define LINGBO_HEADER_1 0x55U
#define LINGBO_HEADER_LEN 2U
#define LINGBO_LENGTH_OFFSET 2U
#define LINGBO_COMMAND_OFFSET 3U
#define LINGBO_MIN_FRAME_LEN 5U
#define LINGBO_STREAM_CAPACITY 128U

#define LINGBO_CMD_CALIBRATION_REQUEST 0x20U
#define LINGBO_CMD_REALTIME_REQUEST 0x22U
#define LINGBO_CMD_CALIBRATION_REPLY 0x60U
#define LINGBO_CMD_REALTIME_REPLY 0x62U

#define LINGBO_CALIBRATION_MIN_LEN 16U
#define LINGBO_REALTIME_MIN_LEN 23U
/* 转把电压字段（@22）只有在总长 >= 25 时才是数据（否则读到的是校验位） */
#define LINGBO_REALTIME_THROTTLE_MIN_LEN 25U
/* 挡位字节（@27）需要总长 >= 29 */
#define LINGBO_REALTIME_GEAR_MIN_LEN 29U

#define LINGBO_OFF_CAL_PHASE_CURRENT_BASE 4U
#define LINGBO_OFF_CAL_PHASE_VOLTAGE_BASE 6U
#define LINGBO_OFF_CAL_BUS_CURRENT_DIV 8U
#define LINGBO_OFF_CAL_BUS_VOLTAGE_DIV 10U
#define LINGBO_OFF_CAL_CONTROL_MODE 12U
#define LINGBO_OFF_CAL_TORQUE_MAX 13U

#define LINGBO_OFF_RPM 4U
#define LINGBO_OFF_PHASE_CURRENT_SQUARE 6U
#define LINGBO_OFF_BUS_VOLTAGE 10U
#define LINGBO_OFF_BUS_CURRENT 12U
#define LINGBO_OFF_CONTROLLER_TEMP 14U
#define LINGBO_OFF_MOTOR_TEMP 16U
#define LINGBO_OFF_FAULT 18U
#define LINGBO_OFF_THROTTLE 22U
#define LINGBO_OFF_STATUS_FLAGS 24U
#define LINGBO_OFF_UNKNOWN_26 26U
#define LINGBO_OFF_GEAR 27U

/* 转把标定：x3.a() 的应用侧默认值；kl 把 > 4.95V 判为 INVALID */
#define LINGBO_THROTTLE_IDLE_V 0.85
#define LINGBO_THROTTLE_FULL_V 4.2
#define LINGBO_THROTTLE_ADC_LIMIT_V 4.95

/* i0 / v6.o 的车速换算与可信区间 */
#define LINGBO_WHEEL_CIRCUMFERENCE_MM 1414.0
#define LINGBO_RPM_LIMIT 2000.0
#define LINGBO_SPEED_LIMIT_KMH 160.0

/* 温度合理性区间（Java 侧未做区间校验，这里只挡住明显无效的原始值） */
#define LINGBO_TEMP_MIN (-60)
#define LINGBO_TEMP_MAX 200

typedef struct {
    bool calibrated;
    uint16_t phase_current_base;
    uint16_t phase_voltage_base;
    uint16_t bus_current_div;
    uint16_t bus_voltage_div;
    uint8_t control_mode;
    uint16_t torque_max;
} lingbo_calibration_t;

typedef struct {
    bool voltage_valid;
    uint16_t voltage_deci_v;
    bool current_valid;
    int32_t current_centi_a;
    bool power_valid;
    int32_t power_w;
    bool controller_temp_valid;
    int16_t controller_temp_c;
    bool motor_temp_valid;
    int16_t motor_temp_c;
    bool rpm_valid;
    uint16_t rpm;
    bool speed_valid;
    uint16_t speed_deci_kmh;
    bool gear_valid;
    uint8_t gear;
    bool throttle_valid;
    uint8_t throttle_percent;
    bool fault_valid;
    uint16_t fault_code;
} lingbo_state_t;

static lingbo_calibration_t s_calibration;
static lingbo_state_t s_state;
static uint8_t s_stream[LINGBO_STREAM_CAPACITY];
static size_t s_stream_len;

static uint8_t lingbo_sum8(const uint8_t *data, size_t len)
{
    uint32_t sum = 0U;
    for (size_t index = 0U; index < len; ++index) {
        sum += data[index];
    }
    return (uint8_t)(sum & 0xFFU);
}

static bool lingbo_checksum_ok(const uint8_t *frame, size_t len)
{
    if (!frame || len < 2U) {
        return false;
    }
    return frame[len - 1U] == lingbo_sum8(frame, len - 1U);
}

static uint16_t lingbo_u16_le(const uint8_t *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U));
}

static int16_t lingbo_i16_le(const uint8_t *data, size_t offset)
{
    return (int16_t)lingbo_u16_le(data, offset);
}

static uint32_t lingbo_u32_le(const uint8_t *data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8U) |
           ((uint32_t)data[offset + 2U] << 16U) | ((uint32_t)data[offset + 3U] << 24U);
}

static int32_t lingbo_round_i32(double value)
{
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static bool lingbo_temp_valid(int32_t raw)
{
    return raw >= LINGBO_TEMP_MIN && raw <= LINGBO_TEMP_MAX;
}

/* v6.o.d：speed = |rpm| * 周长(mm) * 60 / 1e6 (km/h) */
static bool lingbo_speed_from_rpm(int16_t raw_rpm, uint16_t *out_deci_kmh)
{
    if (!out_deci_kmh) {
        return false;
    }
    const double magnitude = raw_rpm < 0 ? (double)(-raw_rpm) : (double)raw_rpm;
    if (magnitude > LINGBO_RPM_LIMIT) {
        return false;
    }
    const double kmh = (magnitude * LINGBO_WHEEL_CIRCUMFERENCE_MM * 60.0) / 1000000.0;
    if (!(kmh <= LINGBO_SPEED_LIMIT_KMH)) {
        return false;
    }
    *out_deci_kmh = (uint16_t)((kmh * 10.0) + 0.5);
    return true;
}

/* s3.d：转把电压 -> 0..100% */
static uint8_t lingbo_throttle_percent(double volts)
{
    const double span = LINGBO_THROTTLE_FULL_V - LINGBO_THROTTLE_IDLE_V;
    if (!(span > 0.0)) {
        return 0U;
    }
    double percent = ((volts - LINGBO_THROTTLE_IDLE_V) / span) * 100.0;
    if (percent < 0.0) {
        percent = 0.0;
    }
    if (percent > 100.0) {
        percent = 100.0;
    }
    return (uint8_t)(percent + 0.5);
}

/* a9.y：标定帧，必须两个分母都 > 0 才可用于换算 */
static void lingbo_parse_calibration(const uint8_t *frame, size_t len)
{
    if (len < LINGBO_CALIBRATION_MIN_LEN) {
        return;
    }
    const uint16_t bus_current_div = lingbo_u16_le(frame, LINGBO_OFF_CAL_BUS_CURRENT_DIV);
    const uint16_t bus_voltage_div = lingbo_u16_le(frame, LINGBO_OFF_CAL_BUS_VOLTAGE_DIV);
    if (bus_current_div == 0U || bus_voltage_div == 0U) {
        s_calibration.calibrated = false;
        return;
    }
    s_calibration.phase_current_base = lingbo_u16_le(frame, LINGBO_OFF_CAL_PHASE_CURRENT_BASE);
    s_calibration.phase_voltage_base = lingbo_u16_le(frame, LINGBO_OFF_CAL_PHASE_VOLTAGE_BASE);
    s_calibration.bus_current_div = bus_current_div;
    s_calibration.bus_voltage_div = bus_voltage_div;
    s_calibration.control_mode = frame[LINGBO_OFF_CAL_CONTROL_MODE];
    s_calibration.torque_max = lingbo_u16_le(frame, LINGBO_OFF_CAL_TORQUE_MAX);
    s_calibration.calibrated = true;
}

static void lingbo_parse_realtime(const uint8_t *frame, size_t len)
{
    if (!s_calibration.calibrated || len < LINGBO_REALTIME_MIN_LEN) {
        return;
    }

    const int16_t raw_rpm = lingbo_i16_le(frame, LINGBO_OFF_RPM);
    const uint16_t raw_voltage = lingbo_u16_le(frame, LINGBO_OFF_BUS_VOLTAGE);
    const int16_t raw_current = lingbo_i16_le(frame, LINGBO_OFF_BUS_CURRENT);

    const double volts = (double)raw_voltage / (double)s_calibration.bus_voltage_div;
    const double amps = (double)raw_current / (double)s_calibration.bus_current_div;

    s_state.voltage_valid = true;
    s_state.voltage_deci_v = (uint16_t)lingbo_round_i32(volts * 10.0);
    s_state.current_valid = true;
    s_state.current_centi_a = lingbo_round_i32(amps * 100.0);
    /* s3.b：功率 = 母线电压 * 母线电流 */
    s_state.power_valid = true;
    s_state.power_w = (int32_t)(((int64_t)s_state.voltage_deci_v * (int64_t)s_state.current_centi_a) / 1000);

    const int32_t controller_temp = (int16_t)lingbo_u16_le(frame, LINGBO_OFF_CONTROLLER_TEMP);
    s_state.controller_temp_valid = lingbo_temp_valid(controller_temp);
    s_state.controller_temp_c = s_state.controller_temp_valid ? (int16_t)controller_temp : 0;

    const int32_t motor_temp = (int16_t)lingbo_u16_le(frame, LINGBO_OFF_MOTOR_TEMP);
    s_state.motor_temp_valid = lingbo_temp_valid(motor_temp);
    s_state.motor_temp_c = s_state.motor_temp_valid ? (int16_t)motor_temp : 0;

    uint16_t speed_deci = 0U;
    s_state.rpm_valid = true;
    s_state.rpm = raw_rpm < 0 ? (uint16_t)(-raw_rpm) : (uint16_t)raw_rpm;
    s_state.speed_valid = lingbo_speed_from_rpm(raw_rpm, &speed_deci);
    s_state.speed_deci_kmh = s_state.speed_valid ? speed_deci : 0U;

    if (len >= LINGBO_REALTIME_THROTTLE_MIN_LEN) {
        const double throttle_v =
            ((double)lingbo_u16_le(frame, LINGBO_OFF_THROTTLE)) * 0.01;
        if (throttle_v >= 0.0 && throttle_v <= LINGBO_THROTTLE_ADC_LIMIT_V) {
            s_state.throttle_valid = true;
            s_state.throttle_percent = lingbo_throttle_percent(throttle_v);
        } else {
            s_state.throttle_valid = false;
            s_state.throttle_percent = 0U;
        }
    } else {
        s_state.throttle_valid = false;
        s_state.throttle_percent = 0U;
    }

    s_state.fault_valid = true;
    s_state.fault_code = (uint16_t)(lingbo_u32_le(frame, LINGBO_OFF_FAULT) & 0xFFFFU);

    if (len >= LINGBO_REALTIME_GEAR_MIN_LEN) {
        s_state.gear_valid = true;
        s_state.gear = (uint8_t)(frame[LINGBO_OFF_GEAR] & 0x0FU);
    } else {
        s_state.gear_valid = false;
        s_state.gear = 0U;
    }
}

static void lingbo_compact_stream(size_t consumed)
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

static void lingbo_build_request(uint8_t command, uint8_t *out, size_t *out_len)
{
    out[0] = LINGBO_HEADER_0;
    out[1] = LINGBO_HEADER_1;
    out[2] = 0x02U;
    out[3] = command;
    out[4] = lingbo_sum8(out, 4U);
    *out_len = 5U;
}

void esp_bms_ctrl_lingbo_reset(void)
{
    memset(&s_calibration, 0, sizeof(s_calibration));
    memset(&s_state, 0, sizeof(s_state));
    memset(s_stream, 0, sizeof(s_stream));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_lingbo_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* 凌博必须先读标定参数才能把原始值换成物理量（a9.h -> N(CALIBRATION)）。
     * 奇数拍改发实时页探测帧，兼容已经缓存过标定的固件版本。 */
    const uint8_t command = ((attempt % 2U) == 0U) ? LINGBO_CMD_CALIBRATION_REQUEST
                                                   : LINGBO_CMD_REALTIME_REQUEST;
    lingbo_build_request(command, out, out_len);
    return true;
}

bool esp_bms_ctrl_lingbo_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    /* elapsed_ms 由传输层提供，但凌博的请求节奏完全由“是否已标定”决定；
     * 不按时间丢弃标定状态，避免传输层时间语义变化后永久无法解码实时帧。 */
    (void)step;
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    const uint8_t command = s_calibration.calibrated ? LINGBO_CMD_REALTIME_REQUEST
                                                     : LINGBO_CMD_CALIBRATION_REQUEST;
    lingbo_build_request(command, out, out_len);
    return true;
}

bool esp_bms_ctrl_lingbo_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > LINGBO_STREAM_CAPACITY) {
        chunk += len - LINGBO_STREAM_CAPACITY;
        len = LINGBO_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > LINGBO_STREAM_CAPACITY) {
        lingbo_compact_stream(s_stream_len + len - LINGBO_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= LINGBO_MIN_FRAME_LEN) {
        const uint8_t *frame = s_stream + offset;
        const size_t available = s_stream_len - offset;
        if (frame[0] != LINGBO_HEADER_0 || frame[1] != LINGBO_HEADER_1) {
            offset++;
            continue;
        }
        const size_t declared_len = frame[LINGBO_LENGTH_OFFSET];
        const size_t frame_len = declared_len + LINGBO_HEADER_LEN + 1U;
        if (declared_len < 2U || frame_len > ESP_BMS_CTRL_FRAME_MAX_LEN) {
            /* a9.A：长度非法直接丢弃一个字节重新找帧头 */
            offset++;
            continue;
        }
        if (available < frame_len) {
            break; /* 分片，等后续数据 */
        }
        if (!lingbo_checksum_ok(frame, frame_len)) {
            offset++;
            continue;
        }
        const uint8_t command = frame[LINGBO_COMMAND_OFFSET];
        if (command == LINGBO_CMD_CALIBRATION_REPLY) {
            lingbo_parse_calibration(frame, frame_len);
        } else if (command == LINGBO_CMD_REALTIME_REPLY) {
            if (s_calibration.calibrated && frame_len >= LINGBO_REALTIME_MIN_LEN) {
                lingbo_parse_realtime(frame, frame_len);
                parsed = true;
            }
        }
        offset += frame_len;
    }
    lingbo_compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_lingbo_telemetry(esp_bms_ctrl_telemetry_t *out)
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
    out->motor_temp_valid = s_state.motor_temp_valid;
    out->motor_temp_c = s_state.motor_temp_c;
    out->rpm_valid = s_state.rpm_valid;
    out->rpm = s_state.rpm;
    out->speed_valid = s_state.speed_valid;
    out->speed_deci_kmh = s_state.speed_deci_kmh;
    out->gear_valid = s_state.gear_valid;
    out->gear = s_state.gear;
    out->throttle_valid = s_state.throttle_valid;
    out->throttle_percent = s_state.throttle_percent;
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
    /* 凌博标定帧只给电流/电压换算分母，不含轮胎参数；车速用应用侧默认周长换算。 */
    out->wheel_circumference_mm = 1414U;
}
