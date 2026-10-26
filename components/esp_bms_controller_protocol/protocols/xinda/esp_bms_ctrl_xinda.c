#include "esp_bms_ctrl_xinda.h"

#include <string.h>

/* 昕达动能（移植自 Motexa 2.7.0 v6.f1）。
 *
 * 启动：写单字节 0x96（设备类型请求），实时数据由通知推送。
 * 通知帧 25 字节：EC 9F | 故障码 | MOS温度+40 | 母线电压(BE/100V) | 母线电流(int16 BE/10A) |
 *   转速原始值(BE, /极对数) | 相电流(BE, A) | 状态 | 状态原始 | 极对数 | 电机温度+40 |
 *   转把原始(/10V) | 辅助电流 | 轮径候选 | 4 字节未标定 | XOR(前 24 字节)
 * 极对数默认 30，实时帧里出现非零值时更新。
 */

#define XINDA_STREAM_CAPACITY 128U
#define XINDA_FRAME_LEN 25U
#define XINDA_SYNC_1 0xECU
#define XINDA_SYNC_2 0x9FU
#define XINDA_START_REQUEST 0x96U
#define XINDA_DEFAULT_POLE_PAIRS 30U
#define XINDA_TEMP_OFFSET 40
#define XINDA_REFRESH_STEPS 15U

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
    uint8_t pole_pairs;
} xinda_state_t;

static xinda_state_t s_state;
static uint8_t s_stream[XINDA_STREAM_CAPACITY];
static size_t s_stream_len;

static uint16_t xinda_read_u16_be(const uint8_t *data, size_t offset)
{
    return (uint16_t)(((uint16_t)data[offset] << 8U) | data[offset + 1U]);
}

static void xinda_stream_compact(size_t consumed)
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

static void xinda_decode_frame(const uint8_t *frame)
{
    const uint8_t pole_pairs = frame[14U];
    if (pole_pairs > 0U) {
        s_state.pole_pairs = pole_pairs;
    }
    const uint8_t poles = s_state.pole_pairs > 0U ? s_state.pole_pairs : XINDA_DEFAULT_POLE_PAIRS;

    s_state.fault_code = frame[2U];
    s_state.fault_valid = true;

    s_state.controller_temp_c = (int16_t)frame[3U] - XINDA_TEMP_OFFSET;
    s_state.controller_temp_valid = true;
    s_state.motor_temp_c = (int16_t)frame[15U] - XINDA_TEMP_OFFSET;
    s_state.motor_temp_valid = true;

    /* 母线电压 raw/100 V → 0.1V 计数 = raw/10。 */
    s_state.voltage_deci_v = (uint16_t)(xinda_read_u16_be(frame, 4U) / 10U);
    s_state.voltage_valid = true;

    /* 母线电流 int16 BE，单位 0.1A → 0.01A 计数 = raw*10。 */
    const int16_t current_raw = (int16_t)xinda_read_u16_be(frame, 6U);
    s_state.current_centi_a = (int32_t)current_raw * 10;
    s_state.current_valid = true;

    /* 转速 = 原始值 / 极对数。 */
    s_state.rpm = (uint16_t)(xinda_read_u16_be(frame, 8U) / poles);
    s_state.rpm_valid = true;

    s_state.power_w = ((int32_t)s_state.voltage_deci_v * s_state.current_centi_a) / 1000;
    s_state.power_valid = true;
}

void esp_bms_ctrl_xinda_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.pole_pairs = XINDA_DEFAULT_POLE_PAIRS;
    s_stream_len = 0U;
}

bool esp_bms_ctrl_xinda_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    (void)attempt;
    if (!out || !out_len) {
        return false;
    }
    /* 空 UUID 型号：registration/探测后必须由协议自己发起设备类型请求。 */
    out[0] = XINDA_START_REQUEST;
    *out_len = 1U;
    return true;
}

bool esp_bms_ctrl_xinda_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    *out_len = 0U;
    if ((step % XINDA_REFRESH_STEPS) != 0U) {
        /* 设备类型请求只是流开关，按 200ms 节拍约 3s 补发一次即可。 */
        return false;
    }
    out[0] = XINDA_START_REQUEST;
    *out_len = 1U;
    return true;
}

bool esp_bms_ctrl_xinda_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > XINDA_STREAM_CAPACITY) {
        chunk += len - XINDA_STREAM_CAPACITY;
        len = XINDA_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > XINDA_STREAM_CAPACITY) {
        xinda_stream_compact(s_stream_len + len - XINDA_STREAM_CAPACITY);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= 2U) {
        if (s_stream[offset] != XINDA_SYNC_1 || s_stream[offset + 1U] != XINDA_SYNC_2) {
            /* 身份响应（ASCII/0x01/0x54）与噪点一律按单字节重同步丢弃。 */
            ++offset;
            continue;
        }
        if (s_stream_len - offset < XINDA_FRAME_LEN) {
            break;
        }
        uint8_t checksum = 0U;
        for (size_t index = 0U; index < XINDA_FRAME_LEN - 1U; ++index) {
            checksum = (uint8_t)(checksum ^ s_stream[offset + index]);
        }
        if (checksum != s_stream[offset + XINDA_FRAME_LEN - 1U]) {
            ++offset;
            continue;
        }
        xinda_decode_frame(s_stream + offset);
        parsed = true;
        offset += XINDA_FRAME_LEN;
    }
    xinda_stream_compact(offset);
    return parsed;
}

void esp_bms_ctrl_xinda_telemetry(esp_bms_ctrl_telemetry_t *out)
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
