#include "esp_bms_cast_protocol.h"

static uint32_t cast_u32(const uint8_t *value)
{
    return ((uint32_t)value[0] << 24U) | ((uint32_t)value[1] << 16U) |
           ((uint32_t)value[2] << 8U) | value[3];
}

bool esp_bms_cast_protocol_rotation_valid(uint8_t rotation)
{
    return rotation <= 3U;
}

bool esp_bms_cast_protocol_is_heartbeat(const uint8_t *message, size_t message_len)
{
    return message && message_len == 1U && message[0] == ESP_BMS_CAST_TYPE_HEARTBEAT;
}

bool esp_bms_cast_protocol_parse_jpeg_frame(const uint8_t *message,
                                            size_t message_len,
                                            esp_bms_cast_jpeg_frame_t *frame)
{
    if (!message || !frame || message_len <= ESP_BMS_CAST_FRAME_HEADER_BYTES ||
        message_len > ESP_BMS_CAST_MESSAGE_MAX_BYTES ||
        message[0] != ESP_BMS_CAST_TYPE_JPEG_FRAME ||
        message[1] != ESP_BMS_CAST_PROTOCOL_VERSION ||
        !esp_bms_cast_protocol_rotation_valid(message[6])) {
        return false;
    }

    frame->sequence = cast_u32(&message[2]);
    frame->rotation = message[6];
    frame->jpeg = &message[ESP_BMS_CAST_FRAME_HEADER_BYTES];
    frame->jpeg_bytes = message_len - ESP_BMS_CAST_FRAME_HEADER_BYTES;
    return true;
}

bool esp_bms_cast_protocol_encode_ack(uint32_t sequence,
                                      uint8_t *ack,
                                      size_t ack_bytes)
{
    if (!ack || ack_bytes != ESP_BMS_CAST_ACK_BYTES) {
        return false;
    }
    ack[0] = ESP_BMS_CAST_TYPE_ACK;
    ack[1] = (uint8_t)(sequence >> 24U);
    ack[2] = (uint8_t)(sequence >> 16U);
    ack[3] = (uint8_t)(sequence >> 8U);
    ack[4] = (uint8_t)sequence;
    return true;
}

bool esp_bms_cast_protocol_encode_input(uint8_t phase,
                                        uint16_t x,
                                        uint16_t y,
                                        uint8_t *input,
                                        size_t input_bytes)
{
    if (!input || input_bytes != ESP_BMS_CAST_INPUT_FRAME_BYTES ||
        phase > ESP_BMS_CAST_INPUT_PHASE_UP) {
        return false;
    }
    input[0] = ESP_BMS_CAST_TYPE_INPUT_TOUCH;
    input[1] = ESP_BMS_CAST_PROTOCOL_VERSION;
    input[2] = phase;
    input[3] = (uint8_t)(x >> 8U);
    input[4] = (uint8_t)x;
    input[5] = (uint8_t)(y >> 8U);
    input[6] = (uint8_t)y;
    return true;
}

bool esp_bms_cast_protocol_encode_exit(uint8_t *exit_frame, size_t exit_bytes)
{
    if (!exit_frame || exit_bytes != ESP_BMS_CAST_EXIT_FRAME_BYTES) {
        return false;
    }
    exit_frame[0] = ESP_BMS_CAST_TYPE_EXIT_CAST;
    exit_frame[1] = ESP_BMS_CAST_PROTOCOL_VERSION;
    return true;
}
