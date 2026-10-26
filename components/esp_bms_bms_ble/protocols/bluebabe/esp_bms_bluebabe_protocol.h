#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../esp_bms_bms_protocol.h"

/*
 * Bluebabe (蓝宝电量模块) meter protocol, ported from Motexa 2.7.0
 * (l0.java / m0.java / k0.java / o0.java).
 *
 * Link: BLE service 0000ffe0, notify 0000ffe1, write 0000ffe2,
 *       write-without-response, requested ATT MTU 200.
 *
 * Request (19 bytes, poll index selects the table):
 *   [0]=0xFA [1]=0x50 [2]=0x28 [3]=0xA0 [4]=0x15
 *   [5..6] payload length field, big endian, always 0x0009
 *   [7..12] six-digit matching code, one digit per byte (value 0..9, not ASCII)
 *   [13] table index (1 = state table, 2 = identity/capacity table)
 *   [14] table offset (always 0)
 *   [15] table length in bytes (111 or 134)
 *   [16..17] CRC16, big endian, over bytes [0..15]
 *   [18]=0xE5
 *
 * Response (total = length field + 10 bytes):
 *   [0]=0xFA [1]=0x50 [2]=0xA0 [3]=0x28 [4]=0x15
 *   [5..6] payload length field, big endian
 *   [7..12] matching code echo
 *   [13] table index [14] table offset [15] table length
 *   [16 .. total-4] payload
 *   [total-3..total-2] CRC16, big endian, over bytes [0 .. total-4)
 *   [total-1]=0xE5
 * A length field of 7 marks a device error frame with the error code in [13].
 */
#define ESP_BMS_BLUEBABE_UUID_LEN 16U
#define ESP_BMS_BLUEBABE_MATCHING_CODE_LEN 6U
#define ESP_BMS_BLUEBABE_REQUEST_LEN 19U
/* A TABLE_2 frame is 153 bytes on the wire; leave room for one partial frame. */
#define ESP_BMS_BLUEBABE_STREAM_CAPACITY 192U
#define ESP_BMS_BLUEBABE_TABLE_STATE 1U
#define ESP_BMS_BLUEBABE_TABLE_INFO 2U
#define ESP_BMS_BLUEBABE_STATE_PAYLOAD_LEN 111U
#define ESP_BMS_BLUEBABE_INFO_PAYLOAD_LEN 134U

/* NimBLE byte order (least significant 16 bits first). */
extern const uint8_t esp_bms_bluebabe_service_uuid[ESP_BMS_BLUEBABE_UUID_LEN];
extern const uint8_t esp_bms_bluebabe_notify_uuid[ESP_BMS_BLUEBABE_UUID_LEN];
extern const uint8_t esp_bms_bluebabe_write_uuid[ESP_BMS_BLUEBABE_UUID_LEN];

uint16_t esp_bms_bluebabe_crc16(const uint8_t *bytes, size_t len);

bool esp_bms_bluebabe_matching_code_valid(const uint8_t code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN]);
/* Stores the six-digit code used by esp_bms_bluebabe_poll_request(). Pass NULL to clear. */
bool esp_bms_bluebabe_set_matching_code(const uint8_t code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN]);
bool esp_bms_bluebabe_matching_code_configured(void);

/* Pure request builder: poll_index 0 -> TABLE_1 (111 B), 1 -> TABLE_2 (134 B).
   matching_code may be NULL when the meter runs without a code. */
bool esp_bms_bluebabe_build_request(uint8_t poll_index,
                                    const uint8_t matching_code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN],
                                    uint8_t out[ESP_BMS_BLUEBABE_REQUEST_LEN]);

/* Dispatch entry point. A bluebabe poll is 19 bytes, so the agreed out[8] shape
   cannot carry it: this writes ESP_BMS_BLUEBABE_REQUEST_LEN bytes and fails
   while no matching code is configured. */
bool esp_bms_bluebabe_poll_request(uint8_t poll_index, uint8_t out[ESP_BMS_BLUEBABE_REQUEST_LEN]);

bool esp_bms_bluebabe_feed(uint8_t *stream,
                           size_t *stream_len,
                           size_t stream_capacity,
                           const uint8_t *chunk,
                           size_t chunk_len,
                           esp_bms_bms_telemetry_t *telemetry);
