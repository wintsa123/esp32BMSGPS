#include "esp_bms_ctrl_jh.h"

#include <string.h>

/* 晶汇 JH 控制器（移植自 Motexa 2.7.0 v6.c0）。
 *
 * 上行请求是 Modbus-RTU 风格帧：从机地址 0x04 + 功能码 0x03 + 寄存器 + 数量 + CRC16；
 * 控制器用 0x04 0x03 <字节数> <数据> CRC16 应答，异常应答为 0x04 0x83 <异常码> CRC16。
 * 另外用 JN 私有帧（0x4A 0x4E ... 0x4B 0x4A，sum8 校验）读取型号/软件版本。
 *
 * 实时寄存器块 0xA100 起 7 个寄存器（14 字节数据，大端）：
 *   +0  状态/故障位图        +2  电机转速 rpm       +4  母线电压 0.1V
 *   +6  母线电流 0.1A(有符号) +8  相线电流 0.1A      +10 MOS 温度 °C
 *   +12 电机温度 °C
 */

#define JH_STREAM_CAPACITY 256U

#define JH_ADDR 0x04U
#define JH_FUNC_READ 0x03U
#define JH_FUNC_EXCEPTION 0x83U
#define JH_MODBUS_HEAD_LEN 3U
#define JH_MODBUS_TRAILER_LEN 2U
#define JH_MODBUS_EXCEPTION_LEN 5U
#define JH_MODBUS_MAX_DATA 250U

#define JH_REG_REALTIME 0xA100U
#define JH_REALTIME_REG_COUNT 7U
#define JH_REALTIME_DATA_LEN 14U
#define JH_REG_INFO 0xA110U
#define JH_INFO_REG_COUNT 27U

#define JH_INFO_POLL_INTERVAL 25U

#define JH_JN_HEAD_1 0x4AU
#define JH_JN_HEAD_2 0x4EU
#define JH_JN_DIR_QUERY 0x27U
#define JH_JN_DIR_ANSWER 0x2FU
#define JH_JN_TAIL_1 0x4BU
#define JH_JN_TAIL_2 0x4AU
#define JH_JN_MODEL_ID 0x87U
#define JH_JN_VERSION_ID 0x95U
#define JH_JN_OVERHEAD 7U
#define JH_JN_MIN_LEN 8U
#define JH_JN_FIXED_SUM 317U

/* Motexa 侧 status & 0xFF81 得到故障码，bit15..bit7 与 bit0 属于故障类。 */
#define JH_FAULT_MASK 0xFF81U

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
} jh_state_t;

static jh_state_t s_state;
static uint8_t s_stream[JH_STREAM_CAPACITY];
static size_t s_stream_len;

static uint16_t jh_crc16_modbus(const uint8_t *data, size_t len)
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

static bool jh_read_u16_be(const uint8_t *data, size_t len, size_t offset, uint16_t *out)
{
    if (!data || !out || offset > len || len - offset < 2U) {
        return false;
    }
    *out = (uint16_t)(((uint16_t)data[offset] << 8U) | data[offset + 1U]);
    return true;
}

static void jh_stream_compact(size_t consumed)
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

static bool jh_build_modbus_read(uint16_t reg, uint16_t count, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len || count == 0U || count > JH_MODBUS_MAX_DATA / 2U) {
        return false;
    }
    out[0] = JH_ADDR;
    out[1] = JH_FUNC_READ;
    out[2] = (uint8_t)(reg >> 8U);
    out[3] = (uint8_t)(reg & 0xFFU);
    out[4] = (uint8_t)(count >> 8U);
    out[5] = (uint8_t)(count & 0xFFU);
    const uint16_t crc = jh_crc16_modbus(out, 6U);
    out[6] = (uint8_t)(crc & 0xFFU);
    out[7] = (uint8_t)(crc >> 8U);
    *out_len = 8U;
    return true;
}

static bool jh_build_jn_request(uint8_t identifier, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len || (identifier != JH_JN_MODEL_ID && identifier != JH_JN_VERSION_ID)) {
        return false;
    }
    out[0] = JH_JN_HEAD_1;
    out[1] = JH_JN_HEAD_2;
    out[2] = JH_JN_DIR_QUERY;
    out[3] = 0x03U;
    out[4] = 0x22U;
    out[5] = 0xF1U;
    out[6] = identifier;
    /* sum8 落在 len-3：源码固定前置字节和为 0x013D，故等于 identifier + 317。 */
    out[7] = (uint8_t)((uint16_t)identifier + JH_JN_FIXED_SUM);
    out[8] = JH_JN_TAIL_1;
    out[9] = JH_JN_TAIL_2;
    *out_len = 10U;
    return true;
}

void esp_bms_ctrl_jh_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
}

bool esp_bms_ctrl_jh_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    /* attempt 0：型号 JN，1：版本 JN，≥2：直接读实时块（避免 JN 反复循环）。 */
    if (attempt == 0U) {
        return jh_build_jn_request(JH_JN_MODEL_ID, out, out_len);
    }
    if (attempt == 1U) {
        return jh_build_jn_request(JH_JN_VERSION_ID, out, out_len);
    }
    return jh_build_modbus_read(JH_REG_REALTIME, JH_REALTIME_REG_COUNT, out, out_len);
}

bool esp_bms_ctrl_jh_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    if ((step % JH_INFO_POLL_INTERVAL) == (JH_INFO_POLL_INTERVAL - 1U)) {
        /* 每 25 拍（200ms 节拍下约 5s）补读一次控制器信息块，其余拍读实时块。 */
        return jh_build_modbus_read(JH_REG_INFO, JH_INFO_REG_COUNT, out, out_len);
    }
    return jh_build_modbus_read(JH_REG_REALTIME, JH_REALTIME_REG_COUNT, out, out_len);
}

static void jh_decode_realtime(const uint8_t *data, size_t len)
{
    uint16_t status = 0U;
    uint16_t rpm = 0U;
    uint16_t voltage_raw = 0U;
    uint16_t current_raw = 0U;
    uint16_t mos_raw = 0U;
    uint16_t motor_raw = 0U;
    if (!jh_read_u16_be(data, len, 0U, &status) || !jh_read_u16_be(data, len, 2U, &rpm) ||
        !jh_read_u16_be(data, len, 4U, &voltage_raw) || !jh_read_u16_be(data, len, 6U, &current_raw) ||
        !jh_read_u16_be(data, len, 10U, &mos_raw) || !jh_read_u16_be(data, len, 12U, &motor_raw)) {
        return;
    }
    s_state.rpm = rpm;
    s_state.rpm_valid = true;

    /* 母线电压原始值就是 0.1V 计数（Java 侧 /10 得到 V）。 */
    s_state.voltage_deci_v = voltage_raw;
    s_state.voltage_valid = true;

    /* 母线电流 0.1A 有符号：0.01A 计数 = raw * 10。 */
    const int16_t current_signed = (int16_t)current_raw;
    s_state.current_centi_a = (int32_t)current_signed * 10;
    s_state.current_valid = true;
    /* P = (raw_v/10) * (raw_i/10) = raw_v * raw_i / 100。 */
    s_state.power_w = ((int32_t)voltage_raw * (int32_t)current_signed) / 100;
    s_state.power_valid = true;

    /* MOS / 电机温度是整数字节温度（Java 侧无缩放）。 */
    s_state.controller_temp_c = (int16_t)(int8_t)(mos_raw & 0xFFU);
    s_state.controller_temp_valid = true;
    s_state.motor_temp_c = (int16_t)(int8_t)(motor_raw & 0xFFU);
    s_state.motor_temp_valid = true;

    s_state.fault_code = (uint16_t)(status & JH_FAULT_MASK);
    s_state.fault_valid = true;
}

/* 返回 true 表示这一帧是实时数据帧（已更新快照）。 */
static bool jh_handle_frame(const uint8_t *frame, size_t len)
{
    if (len >= JH_MODBUS_EXCEPTION_LEN && frame[0] == JH_ADDR && frame[1] == JH_FUNC_EXCEPTION) {
        /* 控制器异常应答：Java 侧只记录 Modbus exception，不产出遥测。 */
        return false;
    }
    if (len >= JH_MODBUS_HEAD_LEN + JH_REALTIME_DATA_LEN + JH_MODBUS_TRAILER_LEN && frame[0] == JH_ADDR &&
        frame[1] == JH_FUNC_READ) {
        const size_t data_len = frame[2];
        if (data_len == JH_REALTIME_DATA_LEN) {
            jh_decode_realtime(frame + JH_MODBUS_HEAD_LEN, data_len);
            return true;
        }
        /* 信息块 54 字节等其它长度：校验通过但当前不需要产出遥测。 */
    }
    return false;
}

bool esp_bms_ctrl_jh_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > JH_STREAM_CAPACITY) {
        chunk += len - JH_STREAM_CAPACITY;
        len = JH_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > JH_STREAM_CAPACITY) {
        jh_stream_compact(s_stream_len + len - JH_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (offset < s_stream_len) {
        const uint8_t first = s_stream[offset];
        if (first == JH_ADDR) {
            if (s_stream_len - offset < 2U) {
                break;
            }
            const uint8_t function = s_stream[offset + 1U];
            if (function == JH_FUNC_EXCEPTION) {
                if (s_stream_len - offset < JH_MODBUS_EXCEPTION_LEN) {
                    break;
                }
                const uint16_t crc = jh_crc16_modbus(s_stream + offset, 3U);
                if (s_stream[offset + 3U] != (uint8_t)(crc & 0xFFU) ||
                    s_stream[offset + 4U] != (uint8_t)(crc >> 8U)) {
                    ++offset;
                    continue;
                }
                (void)jh_handle_frame(s_stream + offset, JH_MODBUS_EXCEPTION_LEN);
                offset += JH_MODBUS_EXCEPTION_LEN;
                continue;
            }
            if (function != JH_FUNC_READ) {
                ++offset;
                continue;
            }
            if (s_stream_len - offset < JH_MODBUS_HEAD_LEN) {
                break;
            }
            const uint8_t data_len = s_stream[offset + 2U];
            if (data_len == 0U || data_len > 120U || (data_len & 1U) != 0U) {
                ++offset;
                continue;
            }
            const size_t total = JH_MODBUS_HEAD_LEN + (size_t)data_len + JH_MODBUS_TRAILER_LEN;
            if (total > ESP_BMS_CTRL_FRAME_MAX_LEN) {
                /* 超过单帧上限的应答（例如 54 字节信息块）逐字节重同步丢弃。 */
                ++offset;
                continue;
            }
            if (s_stream_len - offset < total) {
                break;
            }
            const uint16_t crc = jh_crc16_modbus(s_stream + offset, total - 2U);
            if (s_stream[offset + total - 2U] != (uint8_t)(crc & 0xFFU) ||
                s_stream[offset + total - 1U] != (uint8_t)(crc >> 8U)) {
                ++offset;
                continue;
            }
            if (jh_handle_frame(s_stream + offset, total)) {
                parsed = true;
            }
            offset += total;
            continue;
        }
        if (first == JH_JN_HEAD_1) {
            if (s_stream_len - offset < 2U) {
                break;
            }
            if (s_stream[offset + 1U] != JH_JN_HEAD_2) {
                ++offset;
                continue;
            }
            if (s_stream_len - offset < 4U) {
                break;
            }
            const size_t total = (size_t)s_stream[offset + 3U] + JH_JN_OVERHEAD;
            if (total < JH_JN_MIN_LEN || total > ESP_BMS_CTRL_FRAME_MAX_LEN) {
                ++offset;
                continue;
            }
            if (s_stream_len - offset < total) {
                break;
            }
            uint8_t sum = 0U;
            for (size_t index = offset + 2U; index < offset + total - 3U; ++index) {
                sum = (uint8_t)(sum + s_stream[index]);
            }
            const bool valid = s_stream[offset + 2U] == JH_JN_DIR_ANSWER &&
                               s_stream[offset + total - 3U] == sum &&
                               s_stream[offset + total - 2U] == JH_JN_TAIL_1 &&
                               s_stream[offset + total - 1U] == JH_JN_TAIL_2;
            if (!valid) {
                ++offset;
                continue;
            }
            /* 型号/版本帧只用于识别，不影响遥测。 */
            offset += total;
            continue;
        }
        ++offset;
    }
    jh_stream_compact(offset);
    return parsed;
}

void esp_bms_ctrl_jh_telemetry(esp_bms_ctrl_telemetry_t *out)
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
