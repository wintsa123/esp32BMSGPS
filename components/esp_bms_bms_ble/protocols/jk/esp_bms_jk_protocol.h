#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

#define ESP_BMS_JK_FRAME_LEN 300U

bool esp_bms_jk_poll_request(uint8_t poll_index, uint8_t out[20]);
bool esp_bms_jk_feed(uint8_t *stream,
                     size_t *stream_len,
                     size_t stream_capacity,
                     const uint8_t *chunk,
                     size_t chunk_len,
                     esp_bms_bms_telemetry_t *telemetry);
