#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

#define ESP_BMS_YANYANG_UUID_LEN 16U

extern const uint8_t esp_bms_yanyang_service_uuid[ESP_BMS_YANYANG_UUID_LEN];
extern const uint8_t esp_bms_yanyang_write_uuid[ESP_BMS_YANYANG_UUID_LEN];
extern const uint8_t esp_bms_yanyang_notify_uuid[ESP_BMS_YANYANG_UUID_LEN];

uint16_t esp_bms_yanyang_crc16(const uint8_t *bytes, size_t len);
bool esp_bms_yanyang_poll_request(uint8_t poll_index, uint8_t out[8]);
bool esp_bms_yanyang_feed(uint8_t *stream,
                          size_t *stream_len,
                          size_t stream_capacity,
                          const uint8_t *chunk,
                          size_t chunk_len,
                          esp_bms_bms_telemetry_t *telemetry);
