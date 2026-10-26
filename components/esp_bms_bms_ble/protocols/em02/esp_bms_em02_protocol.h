#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

#define ESP_BMS_EM02_REQUEST_LEN 8U
#define ESP_BMS_EM02_STREAM_CAPACITY 512U

bool esp_bms_em02_poll_request(uint8_t poll_index, uint8_t out[ESP_BMS_EM02_REQUEST_LEN]);
bool esp_bms_em02_feed(uint8_t *stream, size_t *stream_len, size_t stream_capacity,
                       const uint8_t *chunk, size_t chunk_len, uint8_t poll_index,
                       bool *identity_known, esp_bms_bms_telemetry_t *telemetry);
