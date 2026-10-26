#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

/*
 * Mingtang (明唐) BMS protocol, ported from Motexa 2.7.0
 * (r9.java state machine, v6/m0.java frame layer, v6/n0.java tag tables, v6/q0.java link).
 *
 * Link: BLE service 0000fa00-0000-1000-8000-00805f9b34fb. The notify and write
 * characteristics are discovered by property (notify/indicate and write), and the
 * optional 0000180f battery service is used by the "verified-180f" profile when the
 * vendor service does not answer.
 *
 * Requests are plain ASCII commands written to the write characteristic with response:
 *   "stateread"   live telemetry (0xE1 frame)
 *   "settingread" settings block (0xE2 frame)
 *   "readhis"     history
 *   "readevent"   event log
 *
 * Responses are binary frames:
 *   [0] frame type: 0xE1 live state, 0xE2 settings, 0xF1..0xF3 history/event payloads
 *   [1] 0x11 [2] 0x00
 *   [3] unconstrained
 *   [4..5] big endian length field = index of the frame's last byte
 *   [6 .. length-1] TLV stream, each entry: tag (2 bytes BE), length (1 byte), value
 *   [length] trailing byte, unverified by the vendor app
 * The frame therefore occupies length+1 bytes. 0xE2 frames are terminated by the
 * ASCII marker "[END]" five bytes after the frame.
 *
 * 0xE1 tags used here (normalised key from the vendor field table):
 *    3 cell count        4 cell voltages (2 bytes each)   5 highest cell
 *    6 lowest cell       7 cell delta                    10 pack voltage
 *   13 charge current   15 discharge current             17 charge protection
 *   19 discharge protection                              21 short circuit protection
 *   23 balancer type    24 balancer switch              25 balancer bitmask
 *   26 cycle count      27..31 MOS/BAT1/BAT2/EXT1/EXT2 temperature
 *   36 design capacity  37 remaining capacity           38 state of charge
 *  170 alarm bitmask
 *
 * Scaling: tags 10,5,6,7 and the cell array are 0.001 V per count, 13 and 15 are
 * 0.001 A per count (two's complement), 27..31 are 0.01 degC per count (two's
 * complement), 36 and 37 are 0.001 Ah per count. Positive current means charging and
 * positive power means discharging, matching the vendor projection.
 */
#define ESP_BMS_MINGTANG_UUID_LEN 16U
/* Longest command is "settingread" (11 bytes) plus a NUL guard. */
#define ESP_BMS_MINGTANG_REQUEST_MAX_LEN 12U
/* One 0xE1 state frame with 32 cells plus 0xE2 settings fits comfortably. */
#define ESP_BMS_MINGTANG_STREAM_CAPACITY 384U

/* NimBLE byte order (least significant 16 bits first). */
extern const uint8_t esp_bms_mingtang_service_uuid[ESP_BMS_MINGTANG_UUID_LEN];

/* The live state frame has no checksum; it is validated by header, length and TLV tiling. */
bool esp_bms_mingtang_poll_request(uint8_t poll_index, uint8_t *out, size_t *out_len);

bool esp_bms_mingtang_feed(uint8_t *stream,
                           size_t *stream_len,
                           size_t stream_capacity,
                           const uint8_t *chunk,
                           size_t chunk_len,
                           esp_bms_bms_telemetry_t *telemetry);
