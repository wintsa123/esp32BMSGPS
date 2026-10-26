#include "esp_bms_ctrl_fardriver.h"

#include <string.h>

#include "esp_fardriver_protocol.h"

/* FarDriver 的通知帧固定 16 字节，但 BLE 通知可能粘包/分片，这里做滑动缓冲。 */
#define FARDRIVER_STREAM_CAPACITY 128U

static esp_fardriver_state_t s_state;
static uint8_t s_stream[FARDRIVER_STREAM_CAPACITY];
static size_t s_stream_len;
static uint8_t s_poll_index;

void esp_bms_ctrl_fardriver_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_stream_len = 0U;
    s_poll_index = 0U;
}

bool esp_bms_ctrl_fardriver_build_start(uint8_t attempt, uint8_t *out, size_t *out_len)
{
    if (!out || !out_len) {
        return false;
    }
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN] = { 0 };
    bool built = false;
    switch (attempt % 4U) {
    case 0U:
        built = esp_fardriver_build_open_command(command);
        break;
    case 1U:
        built = esp_fardriver_build_control_command(0x13U, 0x07U, 0x01U, 0x5FU, command);
        break;
    case 2U:
        built = esp_fardriver_build_control_command(0x17U, 0xF1U, 0x13U, 0x51U, command);
        break;
    default: {
        uint8_t address = 0U;
        const size_t count = esp_fardriver_poll_address_count();
        if (count == 0U) {
            return false;
        }
        built = esp_fardriver_poll_address(s_poll_index % count, &address) &&
                esp_fardriver_build_read_request(address, command);
        if (built) {
            s_poll_index = (uint8_t)((s_poll_index + 1U) % count);
        }
        break;
    }
    }
    if (!built) {
        return false;
    }
    memcpy(out, command, sizeof(command));
    *out_len = sizeof(command);
    return true;
}

bool esp_bms_ctrl_fardriver_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len)
{
    (void)elapsed_ms;
    if (!out || !out_len) {
        return false;
    }
    uint8_t command[ESP_FARDRIVER_COMMAND_LEN] = { 0 };
    bool built = false;
    if ((step % 5U) == 4U) {
        /* 每 5 拍（约 1s）保活一次，其余拍轮询读包，与改造前的节奏一致 */
        built = esp_fardriver_build_keepalive_command(command);
    } else {
        const size_t count = esp_fardriver_poll_address_count();
        uint8_t address = 0U;
        if (count == 0U) {
            return false;
        }
        built = esp_fardriver_poll_address(s_poll_index % count, &address) &&
                esp_fardriver_build_read_request(address, command);
        if (built) {
            s_poll_index = (uint8_t)((s_poll_index + 1U) % count);
        }
    }
    if (!built) {
        return false;
    }
    memcpy(out, command, sizeof(command));
    *out_len = sizeof(command);
    return true;
}

static void fardriver_compact_stream(size_t consumed)
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

bool esp_bms_ctrl_fardriver_feed(const uint8_t *chunk, size_t len)
{
    if (!chunk || len == 0U) {
        return false;
    }
    if (len > FARDRIVER_STREAM_CAPACITY) {
        /* 超长通知只保留尾部，避免把老数据挤进解析窗口 */
        chunk += len - FARDRIVER_STREAM_CAPACITY;
        len = FARDRIVER_STREAM_CAPACITY;
        s_stream_len = 0U;
    }
    if (s_stream_len + len > FARDRIVER_STREAM_CAPACITY) {
        const size_t overflow = s_stream_len + len - FARDRIVER_STREAM_CAPACITY;
        fardriver_compact_stream(overflow);
    }
    memcpy(s_stream + s_stream_len, chunk, len);
    s_stream_len += len;

    bool parsed = false;
    size_t offset = 0U;
    while (s_stream_len - offset >= ESP_FARDRIVER_FRAME_LEN) {
        if (esp_fardriver_parse_frame(&s_state, s_stream + offset, s_stream_len - offset)) {
            parsed = true;
            offset += ESP_FARDRIVER_FRAME_LEN;
            continue;
        }
        offset++;
    }
    fardriver_compact_stream(offset);
    if (parsed) {
        esp_fardriver_refresh_derived(&s_state);
    }
    return parsed;
}

void esp_bms_ctrl_fardriver_telemetry(esp_bms_ctrl_telemetry_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->rpm_valid = s_state.rpm_valid;
    out->speed_valid = s_state.speed_valid;
    out->gear_valid = s_state.gear_valid;
    out->current_valid = s_state.current_valid;
    out->power_valid = s_state.power_valid;
    out->controller_temp_valid = s_state.controller_temp_valid;
    out->motor_temp_valid = s_state.motor_temp_valid;
    out->controller_speed_params_valid = s_state.controller_speed_params_valid;
    out->rpm = s_state.rpm;
    out->speed_deci_kmh = s_state.speed_deci_kmh;
    out->gear = s_state.gear;
    out->current_centi_a = s_state.current_centi_a;
    out->power_w = s_state.power_w;
    out->voltage_deci_v = s_state.voltage_deci_v;
    out->controller_temp_c = s_state.controller_temp_c;
    out->motor_temp_c = s_state.motor_temp_c;
    out->tire_rim_inch = s_state.tire_rim_inch;
    out->tire_aspect_percent = s_state.tire_aspect_percent;
    out->tire_width_mm = s_state.tire_width_mm;
    out->wheel_circumference_mm = s_state.wheel_circumference_mm;
    out->gear_ratio_centi = s_state.gear_ratio_centi;
}
