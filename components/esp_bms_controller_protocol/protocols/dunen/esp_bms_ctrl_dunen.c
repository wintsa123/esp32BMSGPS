#include "esp_bms_ctrl_dunen.h"

#include <string.h>

/* 顿恩 DN（Motexa 2.7.0 的 v6.u，"dn-dunen"）：标准 Modbus RTU，Function 0x03。
 *   请求：01 | 03 | addrHi addrLo | cntHi cntLo | crcLo crcHi（CRC16/MODBUS，前 6 字节）
 *   回包：01 | 03 | byteCount | payload[byteCount] | crcLo crcHi，整帧 = byteCount + 5
 * 实时读 0x03FE/28 → 56 字节 payload（14 组 4 字节，每组两个大端 16 位字）。
 * 身份读 0xFFEC/15（型号）、0xFFEE/2（软件版本）、0xFFED/2（硬件版本）。 */

#define DN_SLAVE_ADDR 0x01U
#define DN_FUNC_READ 0x03U
#define DN_REQUEST_LEN 8U
#define DN_HEADER_LEN 3U
#define DN_TRAILER_LEN 2U
#define DN_STREAM_CAPACITY 192U
#define DN_MAX_PAYLOAD_LEN 59U /* byteCount + 5 <= 64（ESP_BMS_CTRL_FRAME_MAX_LEN） */

#define DN_REALTIME_ADDR 1022U /* 0x03FE */
#define DN_REALTIME_COUNT 28U
#define DN_REALTIME_PAYLOAD_LEN 56U
#define DN_GROUP_COUNT 14U

#define DN_ID_MODEL_ADDR 65516U  /* 0xFFEC */
#define DN_ID_MODEL_COUNT 15U
#define DN_ID_HW_ADDR 65517U     /* 0xFFED */
#define DN_ID_HW_COUNT 2U
#define DN_ID_SW_ADDR 65518U     /* 0xFFEE */
#define DN_ID_SW_COUNT 2U

/* 实时组索引（每组 4 字节 = wordA | wordB，均大端） */
#define DN_GROUP_RPM 0U
#define DN_GROUP_BUS_VOLTAGE 2U
#define DN_GROUP_PHASE_CURRENT 3U
#define DN_GROUP_MOS_TEMP 4U
#define DN_GROUP_MOTOR_TEMP 5U
#define DN_GROUP_BUS_CURRENT 9U
#define DN_GROUP_FAULT_PRIMARY 13U
#define DN_GROUP_FAULT_FALLBACK 12U

/* 转把标定默认值（Motexa n.a()：0.85V..4.2V），0.01V 单位。 */
#define DN_THROTTLE_MIN_CV 85U
#define DN_THROTTLE_MAX_CV 420U
/* 轮周长默认值（Motexa n.f19680a 的默认 1414mm）。 */
#define DN_DEFAULT_CIRCUMFERENCE_MM 1414U

static uint8_t s_stream[DN_STREAM_CAPACITY];
static size_t s_stream_len;

static bool s_voltage_valid;
static bool s_current_valid;
static bool s_rpm_valid;
static bool s_controller_temp_valid;
static bool s_motor_temp_valid;
static bool s_throttle_valid;
static bool s_fault_valid;
static bool s_speed_valid;

static uint16_t s_voltage_deci_v;
static int32_t s_current_centi_a;
static uint16_t s_rpm;
static int16_t s_controller_temp_c;
static int16_t s_motor_temp_c;
static uint8_t s_throttle_percent;
static uint16_t s_fault_code;
static int32_t s_power_w;
static uint16_t s_speed_deci_kmh;

/* Motexa 批准的只读参数地址表（parameters-basic），轮询时低频轮转读取，只验证回包。 */
static const uint16_t DN_PARAM_ADDRS[] = { 492U, 2U, 66U, 68U, 22U, 504U, 0U, 206U, 94U };
#define DN_PARAM_COUNT (sizeof(DN_PARAM_ADDRS) / sizeof(DN_PARAM_ADDRS[0]))

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

/* Motexa 的 w0.d：大端 16 位。 */
static uint16_t read_u16_be(const uint8_t *data, size_t index)
{
    return ((uint16_t)data[index] << 8U) | data[index + 1U];
}

static uint8_t throttle_percent_from_centivolt(uint16_t centivolt)
{
    if (centivolt <= DN_THROTTLE_MIN_CV) {
        return 0U;
    }
    if (centivolt >= DN_THROTTLE_MAX_CV) {
        return 100U;
    }
    const uint32_t span = (uint32_t)(DN_THROTTLE_MAX_CV - DN_THROTTLE_MIN_CV);
    const uint32_t offset = (uint32_t)(centivolt - DN_THROTTLE_MIN_CV);
    return (uint8_t)((offset * 100U + span / 2U) / span);
}

static void refresh_power(void)
{
    const bool valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    s_power_w = valid ? (int32_t)(((int64_t)s_voltage_deci_v * s_current_centi_a) / 1000) : 0;
}

/* 速度：Motexa o.d() → |rpm| * 周长(mm) * 60 / 1e6 km/h；0.1km/h 就是 *600/1e6。 */
static void refresh_speed(void)
{
    s_speed_valid = false;
    s_speed_deci_kmh = 0U;
    if (!s_rpm_valid) {
        return;
    }
    const uint64_t deci = ((uint64_t)s_rpm * DN_DEFAULT_CIRCUMFERENCE_MM * 600ULL) / 1000000ULL;
    if (deci <= UINT16_MAX) {
        s_speed_deci_kmh = (uint16_t)deci;
        s_speed_valid = true;
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

static void parse_realtime(const uint8_t *payload)
{
    /* 组 0 的高字是转速（组内 wordB），低字是 raw32 的低半部分。 */
    const int16_t rpm = (int16_t)read_u16_be(payload, DN_GROUP_RPM * 4U + 2U);
    s_rpm_valid = true;
    s_rpm = rpm >= 0 ? (uint16_t)rpm : (uint16_t)(-rpm);

    const int16_t bus_voltage = (int16_t)read_u16_be(payload, DN_GROUP_BUS_VOLTAGE * 4U + 2U);
    s_voltage_valid = bus_voltage > 0;
    s_voltage_deci_v = bus_voltage > 0 ? (uint16_t)((int32_t)bus_voltage * 10) : 0U;

    const int16_t bus_current = (int16_t)read_u16_be(payload, DN_GROUP_BUS_CURRENT * 4U + 2U);
    s_current_valid = true;
    s_current_centi_a = (int32_t)bus_current * 100;

    s_controller_temp_valid = true;
    s_controller_temp_c = (int16_t)read_u16_be(payload, DN_GROUP_MOS_TEMP * 4U + 2U);
    s_motor_temp_valid = true;
    s_motor_temp_c = (int16_t)read_u16_be(payload, DN_GROUP_MOTOR_TEMP * 4U + 2U);

    /* 油门电压 = (wordA & 0xFF) / 256 * 5V，换算成 0.01V。 */
    const uint16_t throttle_word = read_u16_be(payload, DN_GROUP_PHASE_CURRENT * 4U);
    const uint16_t throttle_cv = (uint16_t)(((uint32_t)(throttle_word & 0xFFU) * 500U) / 256U);
    s_throttle_valid = true;
    s_throttle_percent = throttle_percent_from_centivolt(throttle_cv);

    /* 故障：组 13 优先，为 0 时退回组 12（Motexa 的 j10<=0 分支）。 */
    const uint16_t fault_a = read_u16_be(payload, DN_GROUP_FAULT_PRIMARY * 4U);
    const uint16_t fault_b = read_u16_be(payload, DN_GROUP_FAULT_PRIMARY * 4U + 2U);
    int32_t fault_raw = (int32_t)(((uint32_t)fault_b << 16U) | fault_a);
    if (fault_raw <= 0) {
        const uint16_t fallback_a = read_u16_be(payload, DN_GROUP_FAULT_FALLBACK * 4U);
        const uint16_t fallback_b = read_u16_be(payload, DN_GROUP_FAULT_FALLBACK * 4U + 2U);
        fault_raw = (int32_t)(((uint32_t)fallback_b << 16U) | fallback_a);
    }
    s_fault_valid = fault_raw != 0;
    /* Motexa 只保留 raw32；ESP 侧 fault_code 是 16 位，优先取非零的那个字
     * （组内 wordB 高字通常是故障号，wordA 低字是状态位）。 */
    {
        const uint16_t high = (uint16_t)(((uint32_t)fault_raw >> 16U) & 0xFFFFU);
        const uint16_t low = (uint16_t)((uint32_t)fault_raw & 0xFFFFU);
        s_fault_code = high != 0U ? high : low;
    }

    refresh_power();
    refresh_speed();
}

/* 返回 true 表示这是被识别的 DN 回包。 */
static bool handle_frame(const uint8_t *frame, size_t total)
{
    const uint8_t payload_len = frame[2];
    const uint8_t *payload = frame + DN_HEADER_LEN;
    if (total != (size_t)payload_len + DN_HEADER_LEN + DN_TRAILER_LEN) {
        return false;
    }
    if (payload_len == DN_REALTIME_PAYLOAD_LEN) {
        parse_realtime(payload);
        return true;
    }
    /* 身份读（30 字节）和参数读（4 字节）只用于确认链路，不进遥测。 */
    return payload_len > 0U;
}

bool esp_bms_ctrl_dunen_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > DN_STREAM_CAPACITY) {
        chunk += len - DN_STREAM_CAPACITY;
        len = DN_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > DN_STREAM_CAPACITY) {
        compact_stream(s_stream_len + len - DN_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        if (s_stream[offset] != DN_SLAVE_ADDR) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < DN_HEADER_LEN) {
            break;
        }
        if (s_stream[offset + 1U] != DN_FUNC_READ) {
            ++offset;
            continue;
        }
        const uint8_t payload_len = s_stream[offset + 2U];
        if (payload_len == 0U || payload_len > DN_MAX_PAYLOAD_LEN) {
            ++offset;
            continue;
        }
        const size_t total = (size_t)payload_len + DN_HEADER_LEN + DN_TRAILER_LEN;
        if (s_stream_len - offset < total) {
            break;
        }
        const uint8_t *frame = s_stream + offset;
        const uint16_t wire_crc = (uint16_t)frame[total - 2U] | ((uint16_t)frame[total - 1U] << 8U);
        if (crc16_modbus(frame, total - 2U) != wire_crc) {
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

void esp_bms_ctrl_dunen_reset(void)
{
    s_stream_len = 0U;
    s_voltage_valid = false;
    s_current_valid = false;
    s_rpm_valid = false;
    s_controller_temp_valid = false;
    s_motor_temp_valid = false;
    s_throttle_valid = false;
    s_fault_valid = false;
    s_speed_valid = false;
    s_voltage_deci_v = 0U;
    s_current_centi_a = 0;
    s_rpm = 0U;
    s_controller_temp_c = 0;
    s_motor_temp_c = 0;
    s_throttle_percent = 0U;
    s_fault_code = 0U;
    s_power_w = 0;
    s_speed_deci_kmh = 0U;
}

static size_t build_read(uint16_t address, uint16_t count, uint8_t *out)
{
    out[0] = DN_SLAVE_ADDR;
    out[1] = DN_FUNC_READ;
    out[2] = (uint8_t)((address >> 8U) & 0xFFU);
    out[3] = (uint8_t)(address & 0xFFU);
    out[4] = (uint8_t)((count >> 8U) & 0xFFU);
    out[5] = (uint8_t)(count & 0xFFU);
    const uint16_t crc = crc16_modbus(out, 6U);
    out[6] = (uint8_t)(crc & 0xFFU);
    out[7] = (uint8_t)((crc >> 8U) & 0xFFU);
    return DN_REQUEST_LEN;
}

bool esp_bms_ctrl_dunen_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* DN 没有握手：先按 Motexa 的顺序读身份（型号/软件/硬件），失败重试轮换。 */
    switch (attempt % 4U) {
    case 1U:
        *out_len = build_read(DN_ID_MODEL_ADDR, DN_ID_MODEL_COUNT, out);
        break;
    case 2U:
        *out_len = build_read(DN_ID_SW_ADDR, DN_ID_SW_COUNT, out);
        break;
    case 3U:
        *out_len = build_read(DN_ID_HW_ADDR, DN_ID_HW_COUNT, out);
        break;
    default:
        *out_len = build_read(DN_REALTIME_ADDR, DN_REALTIME_COUNT, out);
        break;
    }
    return true;
}

bool esp_bms_ctrl_dunen_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    /* 实时帧 250ms 一次；每 16 拍插一次参数轮转读（只读已批准地址）。 */
    if ((step % 16U) == 15U) {
        const size_t index = (size_t)((step / 16U) % DN_PARAM_COUNT);
        *out_len = build_read(DN_PARAM_ADDRS[index], 2U, out);
        return true;
    }
    *out_len = build_read(DN_REALTIME_ADDR, DN_REALTIME_COUNT, out);
    return true;
}

void esp_bms_ctrl_dunen_telemetry(esp_bms_ctrl_telemetry_t *out)
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
    out->fault_valid = s_fault_valid;
    out->fault_code = s_fault_code;
    out->power_valid = s_voltage_valid && s_current_valid && s_voltage_deci_v > 0U;
    out->power_w = s_power_w;
    /* 速度用固定默认周长换算；控制器本身不返回轮胎/齿比参数。 */
    out->speed_valid = s_speed_valid;
    out->speed_deci_kmh = s_speed_deci_kmh;
    out->controller_speed_params_valid = false;
    out->gear_valid = false;
    out->gear = 0U;
    out->tire_rim_inch = 0U;
    out->tire_aspect_percent = 0U;
    out->tire_width_mm = 0U;
    out->wheel_circumference_mm = 0U;
    out->gear_ratio_centi = 0U;
}
