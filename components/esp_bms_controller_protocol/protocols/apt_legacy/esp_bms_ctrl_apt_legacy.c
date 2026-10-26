#include "esp_bms_ctrl_apt_legacy.h"

#include <string.h>

/* APT 安培旧版（Motexa 2.7.0 的 v6.g，"apt-anpei"）：
 * 控制器被动推固定 32 字节帧，没有帧长/校验字段，靠首字节识别帧类型、
 * 末字节 0xDA 作为帧尾标记。
 *   - 请求：实时读取 FD 48 55（重试时 FD 48 60）；参数读取 FB ED DA 等。
 *   - 回包：0xDF = 实时，0xFB = IDC 参数；其余 0xF0/0xF2/0xD2/0xF6/0xFE/0xD5/0xD6/0xD7
 *     也在白名单里（Motexa 的 y()），但字段布局未标定，只当有效帧吞掉。
 * 字段为小端（Motexa 用 w0.e/w0.b），偏移来自 A() 的逐字段读取。 */

#define ANPEI_FRAME_LEN 32U
#define ANPEI_STREAM_CAPACITY 160U

#define ANPEI_TYPE_REALTIME 0xDFU
#define ANPEI_TYPE_IDC 0xFBU
#define ANPEI_TAIL 0xDAU

#define ANPEI_OFF_VOLTAGE 2U
#define ANPEI_OFF_BUS_CURRENT 4U
#define ANPEI_OFF_MOTOR_RPM 6U
#define ANPEI_OFF_MOS_TEMP 8U
#define ANPEI_OFF_CONTROLLER_STATE 9U
#define ANPEI_OFF_HISTORY_FAULT 10U
#define ANPEI_OFF_MOTOR_TEMP 12U
#define ANPEI_OFF_CURRENT_FAULT 19U
#define ANPEI_OFF_GEAR 30U

#define ANPEI_MOS_TEMP_OFFSET 10
#define ANPEI_GEAR_OFFSET 2U

static uint8_t s_stream[ANPEI_STREAM_CAPACITY];
static size_t s_stream_len;

static bool s_voltage_valid;
static bool s_current_valid;
static bool s_rpm_valid;
static bool s_controller_temp_valid;
static bool s_motor_temp_valid;
static bool s_gear_valid;
static bool s_fault_valid;

static uint16_t s_voltage_deci_v;
static int32_t s_current_centi_a;
static uint16_t s_rpm;
static int16_t s_controller_temp_c;
static int16_t s_motor_temp_c;
static uint8_t s_gear;
static uint16_t s_fault_code;
static int32_t s_power_w;

/* Motexa 的 g.y()：这些首字节都是合法 APT 安培帧。 */
static bool frame_type_known(uint8_t type)
{
    switch (type) {
    case ANPEI_TYPE_REALTIME:
    case ANPEI_TYPE_IDC:
    case 0xF0U:
    case 0xF2U:
    case 0xD2U:
    case 0xF6U:
    case 0xFEU:
    case 0xD5U:
    case 0xD6U:
    case 0xD7U:
        return true;
    default:
        return false;
    }
}

static uint16_t read_u16_le(const uint8_t *data, size_t index)
{
    return (uint16_t)data[index] | ((uint16_t)data[index + 1U] << 8U);
}

static int16_t read_i16_le(const uint8_t *data, size_t index)
{
    return (int16_t)read_u16_le(data, index);
}

static void refresh_power(void)
{
    const bool valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    s_power_w = valid ? (int32_t)(((int64_t)s_voltage_deci_v * s_current_centi_a) / 1000) : 0;
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

static void parse_realtime(const uint8_t *frame)
{
    /* 电压：u16 / 100 V，0.1V 单位就是 raw/10 */
    s_voltage_valid = true;
    s_voltage_deci_v = (uint16_t)(read_u16_le(frame, ANPEI_OFF_VOLTAGE) / 10U);

    /* 母线电流：s16 / 10 A → 0.01A */
    s_current_valid = true;
    s_current_centi_a = (int32_t)read_i16_le(frame, ANPEI_OFF_BUS_CURRENT) * 10;

    const int16_t rpm = read_i16_le(frame, ANPEI_OFF_MOTOR_RPM);
    s_rpm_valid = true;
    s_rpm = rpm >= 0 ? (uint16_t)rpm : (uint16_t)(-rpm);

    s_controller_temp_valid = true;
    s_controller_temp_c = (int16_t)((int16_t)frame[ANPEI_OFF_MOS_TEMP] - ANPEI_MOS_TEMP_OFFSET);

    /* 电机温度候选 = byte12 * 2，Motexa 只接受 -40..180 °C 的区间。 */
    const int32_t motor_temp = (int32_t)frame[ANPEI_OFF_MOTOR_TEMP] * 2;
    s_motor_temp_valid = motor_temp >= -40 && motor_temp <= 180;
    s_motor_temp_c = s_motor_temp_valid ? (int16_t)motor_temp : 0;

    const uint16_t fault_bitmap = read_u16_le(frame, ANPEI_OFF_CURRENT_FAULT);
    s_fault_code = fault_bitmap;
    s_fault_valid = fault_bitmap != 0U;

    /* 档位候选 = byte30 - 2（Motexa 标为 experimental，这里只在合理区间采信）。 */
    const uint8_t gear_raw = frame[ANPEI_OFF_GEAR];
    s_gear_valid = gear_raw >= ANPEI_GEAR_OFFSET && gear_raw <= (ANPEI_GEAR_OFFSET + 12U);
    s_gear = s_gear_valid ? (uint8_t)(gear_raw - ANPEI_GEAR_OFFSET) : 0U;

    refresh_power();
}

bool esp_bms_ctrl_apt_legacy_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > ANPEI_STREAM_CAPACITY) {
        chunk += len - ANPEI_STREAM_CAPACITY;
        len = ANPEI_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > ANPEI_STREAM_CAPACITY) {
        compact_stream(s_stream_len + len - ANPEI_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= ANPEI_FRAME_LEN) {
        const uint8_t *frame = s_stream + offset;
        if (!frame_type_known(frame[0])) {
            ++offset;
            continue;
        }
        if (frame[0] == ANPEI_TYPE_REALTIME) {
            parse_realtime(frame);
            parsed = true;
        }
        /* 0xFB 及其它白名单帧只证明链路正常，字段布局未标定，不参与遥测。 */
        offset += ANPEI_FRAME_LEN;
    }
    compact_stream(offset);
    return parsed;
}

void esp_bms_ctrl_apt_legacy_reset(void)
{
    s_stream_len = 0U;
    s_voltage_valid = false;
    s_current_valid = false;
    s_rpm_valid = false;
    s_controller_temp_valid = false;
    s_motor_temp_valid = false;
    s_gear_valid = false;
    s_fault_valid = false;
    s_voltage_deci_v = 0U;
    s_current_centi_a = 0;
    s_rpm = 0U;
    s_controller_temp_c = 0;
    s_motor_temp_c = 0;
    s_gear = 0U;
    s_fault_code = 0U;
    s_power_w = 0;
}

/* 首帧就是实时读取；Motexa 在超时后改用 FD 48 60 重试。 */
static const uint8_t ANPEI_REALTIME_REQUEST[3] = { 0xFDU, 0x48U, 0x55U };
static const uint8_t ANPEI_FALLBACK_REQUEST[3] = { 0xFDU, 0x48U, 0x60U };
static const uint8_t ANPEI_IDC_REQUEST[3] = { 0xFBU, 0xEDU, 0xDAU };

bool esp_bms_ctrl_apt_legacy_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    const uint8_t *request = ((attempt & 1U) != 0U) ? ANPEI_FALLBACK_REQUEST : ANPEI_REALTIME_REQUEST;
    memcpy(out, request, sizeof(ANPEI_REALTIME_REQUEST));
    *out_len = sizeof(ANPEI_REALTIME_REQUEST);
    return true;
}

bool esp_bms_ctrl_apt_legacy_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    /* 每 4 拍插一次 IDC 参数读取（FB ED DA），其余拍读实时帧。 */
    const uint8_t *request = ((step % 4U) == 3U) ? ANPEI_IDC_REQUEST : ANPEI_REALTIME_REQUEST;
    memcpy(out, request, sizeof(ANPEI_REALTIME_REQUEST));
    *out_len = sizeof(ANPEI_REALTIME_REQUEST);
    return true;
}

void esp_bms_ctrl_apt_legacy_telemetry(esp_bms_ctrl_telemetry_t *out)
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
    out->gear_valid = s_gear_valid;
    out->gear = s_gear;
    out->fault_valid = s_fault_valid;
    out->fault_code = s_fault_code;
    out->power_valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    out->power_w = s_power_w;
    /* 该型号没有转把电压和速度换算参数。 */
    out->throttle_valid = false;
    out->throttle_percent = 0U;
    out->speed_valid = false;
    out->speed_deci_kmh = 0U;
    out->controller_speed_params_valid = false;
    out->tire_rim_inch = 0U;
    out->tire_aspect_percent = 0U;
    out->tire_width_mm = 0U;
    out->wheel_circumference_mm = 0U;
    out->gear_ratio_centi = 0U;
}
