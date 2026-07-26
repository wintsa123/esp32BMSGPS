#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

bool esp_bms_jbd_poll_request(uint8_t poll_index, uint8_t out[7]);
bool esp_bms_jbd_feed(uint8_t *stream,
                      size_t *stream_len,
                      size_t stream_capacity,
                      const uint8_t *chunk,
                      size_t chunk_len,
                      esp_bms_bms_telemetry_t *telemetry);
