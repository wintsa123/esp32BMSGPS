#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

#define ESP_BMS_ANT_OLD_FRAME_LEN 140U

bool esp_bms_ant_protocol_decode(const uint8_t *data,
                                 size_t len,
                                 esp_bms_bms_telemetry_t *telemetry,
                                 bool *device_info);
bool esp_bms_ant_protocol_old_poll_request(uint8_t out[6]);
bool esp_bms_ant_protocol_old_feed(uint8_t *stream,
                                   size_t *stream_len,
                                   size_t stream_capacity,
                                   const uint8_t *chunk,
                                   size_t chunk_len,
                                   esp_bms_bms_telemetry_t *telemetry);
