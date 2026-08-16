#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_CAST_PROTOCOL_VERSION 3U
#define ESP_BMS_CAST_MAX_FRAME_BYTES 262144U
#define ESP_BMS_CAST_FRAME_HEADER_BYTES 7U
#define ESP_BMS_CAST_MESSAGE_MAX_BYTES \
    (ESP_BMS_CAST_FRAME_HEADER_BYTES + ESP_BMS_CAST_MAX_FRAME_BYTES)
#define ESP_BMS_CAST_TYPE_JPEG_FRAME 1U
#define ESP_BMS_CAST_TYPE_HEARTBEAT 4U
#define ESP_BMS_CAST_TYPE_ACK 0x81U
#define ESP_BMS_CAST_ACK_BYTES 5U

typedef struct {
    uint32_t sequence;
    uint8_t rotation;
    const uint8_t *jpeg;
    size_t jpeg_bytes;
} esp_bms_cast_jpeg_frame_t;

bool esp_bms_cast_protocol_rotation_valid(uint8_t rotation);
bool esp_bms_cast_protocol_is_heartbeat(const uint8_t *message, size_t message_len);
bool esp_bms_cast_protocol_parse_jpeg_frame(const uint8_t *message,
                                            size_t message_len,
                                            esp_bms_cast_jpeg_frame_t *frame);
bool esp_bms_cast_protocol_encode_ack(uint32_t sequence,
                                      uint8_t *ack,
                                      size_t ack_bytes);

#ifdef __cplusplus
}
#endif
