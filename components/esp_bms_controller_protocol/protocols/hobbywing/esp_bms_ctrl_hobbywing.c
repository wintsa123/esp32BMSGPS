#include "esp_bms_ctrl_hobbywing.h"

#include <string.h>

/* 好盈 SPD（移植自 Motexa 2.7.0 v6.x）。
 *
 * 读请求：0A 55 | 01 01 02 | 功能号 | 寄存器数 | CRC16(Modbus, 小端，覆盖功能号前的 5 字节)
 *   功能号 1=实时(6 个寄存器) 7=故障(3) 10=车辆数据(2) 12=整车状态(3)
 * 应答：0A 55 10 | byte3 | byte4 | 功能号 | 数据(寄存器数 × 2，小端) | 2 字节尾
 *   应答按源码 "0A5510-envelope-no-crc" 处理：控制器不返回 CRC，只能做结构校验。
 *
 * 实时（功能号 1）寄存器序号 1..6：转速 rpm、图表原始值、母线电压 0.1V、母线电流 0.1A(有符号)、
 *   相电流 0.1A(有符号)、MOS 温度 ℃。
 * 故障（功能号 7）序号 7..9：电机温度 ℃、故障位图 1、故障位图 2。
 * 整车状态（功能号 12）序号 12..14：状态位图、档位、油门开度 %。
 */

#define HOBBYWING_STREAM_CAPACITY 128U
#define HOBBYWING_SYNC_1 0x0AU
#define HOBBYWING_SYNC_2 0x55U
#define HOBBYWING_MARKER 0x10U
#define HOBBYWING_DATA_OFFSET 6U
#define HOBBYWING_TRAILER_LEN 2U
#define HOBBYWING_REQUEST_LEN 9U
#define HOBBYWING_POLL_SLOT_MS 200U

#define HOBBYWING_FEATURE_REALTIME 1U
#define HOBBYWING_FEATURE_FAULTS 7U
#define HOBBYWING_FEATURE_VEHICLE_DATA 10U
#define HOBBYWING_FEATURE_VEHICLE_STATUS 12U

typedef struct {
    bool rpm_valid;
    bool current_valid;
    bool power_valid;
    bool voltage_valid;
    bool gear_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool throttle_valid;
    bool fault_valid;
    uint16_t rpm;
    int32_t current_centi_a;
    int32_t power_w;
    uint16_t voltage_deci_v;
    uint8_t gear;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint8_t throttle_percent;
    uint16_t fault_code;
} hobbywing_state_t;

static hobbywing_state_t s_state;
static uint8_t s_stream[HOBBYWING_STREAM_CAPACITY];
static size_t s_stream_len;

static uint16_t hobbywing_crc16_modbus(const uint8_t *data, size_t len)
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

static uint8_t hobbywing_register_count(uint8_t feature)
{
    switch (feature) {
    case HOBBYWING_FEATURE_REALTIME:
        return 6U;
    case HOBBYWING_FEATURE_FAULTS:
        return 3U;
    case HOBBYWING_FEATURE_VEHICLE_DATA:
        return 2U;
    case HOBBYWING_FEATURE_VEHICLE_STATUS:
        return 3U;
    default:
        return 0U;
    }
}

static bool hobbywing_build_read(uint8_t feature, uint8_t *out, size_t *out_len)
{
    const uint8_t count = hobbywing_register_count(feature);
    if (!out || !out_len || count == 0U) {
        return false;
    }
    uint8_t body[5] = { 0x01U, 0x01U, 0x02U, feature, count };
    const uint16_t crc = hobbywing_crc16_modbus(body, sizeof(body));
    out[0] = HOBBYWING_SYNC_1;
    out[1] = HOBBYWING_SYNC_2;
    memcpy(out + 2U, body, sizeof(body));
    out[7] = (uint8_t)(crc & 0xFFU);
    out[8] = (uint8_t)(crc >> 8U);
    *out_len = HOBBYWING_REQUEST_LEN;
    return true;
}

static void hobbywing_stream_compact(size_t consumed)
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

static uint16_t hobbywing_read_u16_le(const uint8_t *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U));
}

static bool hobbywing_decode_frame(const uint8_t *frame, size_t len)
{
    if (len < HOBBYWING_DATA_OFFSET || frame[0U] != HOBBYWING_SYNC_1 || frame[1U] != HOBBYWING_SYNC_2 ||
        frame[2U] != HOBBYWING_MARKER) {
        return false;
    }
    const uint8_t feature = frame[5U];
    const uint8_t count = hobbywing_register_count(feature);
    if (count == 0U || len < (size_t)HOBBYWING_DATA_OFFSET + (size_t)count * 2U + HOBBYWING_TRAILER_LEN) {
        return false;
    }
    const uint8_t *data = frame + HOBBYWING_DATA_OFFSET;

    if (feature == HOBBYWING_FEATURE_REALTIME) {
        const uint16_t rpm = hobbywing_read_u16_le(data, 0U);
        const uint16_t voltage_raw = hobbywing_read_u16_le(data, 4U);
        const int16_t current_raw = (int16_t)hobbywing_read_u16_le(data, 6U);
        const int16_t mos_raw = (int16_t)hobbywing_read_u16_le(data, 10U);
        s_state.rpm = rpm;
        s_state.rpm_valid = true;
        /* 母线电压 raw/10 V → raw 即 0.1V 计数。 */
        s_state.voltage_deci_v = voltage_raw;
        s_state.voltage_valid = true;
        /* 母线电流 raw/10 A → 0.01A 计数 = raw*10。 */
        s_state.current_centi_a = (int32_t)current_raw * 10;
        s_state.current_valid = true;
        s_state.controller_temp_c = mos_raw;
        s_state.controller_temp_valid = true;
        s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
        s_state.power_valid = true;
        return true;
    }
    if (feature == HOBBYWING_FEATURE_FAULTS) {
        s_state.motor_temp_c = (int16_t)hobbywing_read_u16_le(data, 0U);
        s_state.motor_temp_valid = true;
        s_state.fault_code = hobbywing_read_u16_le(data, 2U);
        s_state.fault_valid = true;
        return true;
    }
    if (feature == HOBBYWING_FEATURE_VEHICLE_STATUS) {
        const uint16_t gear = hobbywing_read_u16_le(data, 2U);
        uint16_t throttle = hobbywing_read_u16_le(data, 4U);
        if (throttle > 100U) {
            throttle = 100U;
        }
        s_state.gear = (uint8_t)(gear & 0xFFU);
        s_state.gear_valid = true;
        s_state.throttle_percent = (uint8_t)throttle;
        s_state.throttle_valid = true;
        return true;
    }
    /* 车辆数据（功能号 10）语义未标定，仅确认帧结构。 */
    return true;
}

void esp_bms_ctrl_hobbywing_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_hobbywing_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    (void)attempt;
    /* 开启帧：先发实时读取请求，控制器随后按功能号推送 0A 55 10 应答。 */
    return hobbywing_build_read(HOBBYWING_FEATURE_REALTIME, out, out_len);
}

bool esp_bms_ctrl_hobbywing_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)step;
    if (!out || !out_len) {
        return false;
    }
    *out_len = 0U;
    uint8_t feature = 0U;
    if (elapsed_ms != 0U && (elapsed_ms % 5000U) == 0U) {
        /* 故障/电机温度 5s 一轮。 */
        feature = HOBBYWING_FEATURE_FAULTS;
    } else {
        const uint32_t slot = elapsed_ms / HOBBYWING_POLL_SLOT_MS;
        switch (slot % 5U) {
        case 0U:
            feature = HOBBYWING_FEATURE_REALTIME;
            break;
        case 1U:
            feature = HOBBYWING_FEATURE_VEHICLE_STATUS;
            break;
        case 2U:
            feature = HOBBYWING_FEATURE_VEHICLE_DATA;
            break;
        default:
            return false;
        }
    }
    return hobbywing_build_read(feature, out, out_len);
}

bool esp_bms_ctrl_hobbywing_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > HOBBYWING_STREAM_CAPACITY) {
        chunk += len - HOBBYWING_STREAM_CAPACITY;
        len = HOBBYWING_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > HOBBYWING_STREAM_CAPACITY) {
        hobbywing_stream_compact(s_stream_len + len - HOBBYWING_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= 3U) {
        if (s_stream[offset] != HOBBYWING_SYNC_1 || s_stream[offset + 1U] != HOBBYWING_SYNC_2 ||
            s_stream[offset + 2U] != HOBBYWING_MARKER) {
            ++offset;
            continue;
        }
        if (s_stream_len - offset < HOBBYWING_DATA_OFFSET) {
            break;
        }
        const uint8_t count = hobbywing_register_count(s_stream[offset + 5U]);
        if (count == 0U) {
            /* 未知功能号：整帧丢弃并逐字节重同步。 */
            ++offset;
            continue;
        }
        const size_t total = (size_t)HOBBYWING_DATA_OFFSET + (size_t)count * 2U + HOBBYWING_TRAILER_LEN;
        if (s_stream_len - offset < total) {
            break;
        }
        if (hobbywing_decode_frame(s_stream + offset, total)) {
            parsed = true;
        }
        offset += total;
    }
    hobbywing_stream_compact(offset);
    return parsed;
}

void esp_bms_ctrl_hobbywing_telemetry(esp_bms_ctrl_telemetry_t *out)
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
    out->throttle_valid = s_state.throttle_valid;
    out->throttle_percent = s_state.throttle_percent;
    out->fault_valid = s_state.fault_valid;
    out->fault_code = s_state.fault_code;
}
