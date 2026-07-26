#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

bool esp_bms_ant_protocol_decode(const uint8_t *data,
                                 size_t len,
                                 esp_bms_bms_telemetry_t *telemetry,
                                 bool *device_info);
