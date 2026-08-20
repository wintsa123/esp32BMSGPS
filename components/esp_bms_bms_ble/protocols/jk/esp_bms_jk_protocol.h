#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

#define ESP_BMS_JK_FRAME_LEN 300U

typedef enum {
    ESP_BMS_JK_PROTOCOL_UNKNOWN = 0,
    ESP_BMS_JK_PROTOCOL_JK04,
    ESP_BMS_JK_PROTOCOL_JK02_24S,
    ESP_BMS_JK_PROTOCOL_JK02_32S,
} esp_bms_jk_protocol_t;

typedef struct {
    char vendor[17];
    char hardware_version[17];
    char software_version[17];
    char device_name[17];
} esp_bms_jk_device_info_t;

bool esp_bms_jk_poll_request(uint8_t poll_index, uint8_t out[20]);
void esp_bms_jk_reset(void);
esp_bms_jk_protocol_t esp_bms_jk_protocol(void);
bool esp_bms_jk_decode_device_info(const uint8_t *frame,
                                   size_t frame_len,
                                   esp_bms_jk_device_info_t *info);
bool esp_bms_jk_feed(uint8_t *stream,
                     size_t *stream_len,
                     size_t stream_capacity,
                     const uint8_t *chunk,
                     size_t chunk_len,
                     esp_bms_bms_telemetry_t *telemetry);
