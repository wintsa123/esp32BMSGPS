#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Short run: (length - 1) << 4 | index. High nibble 15: next u16 LE is length. */
typedef struct {
    uint8_t *pixels;
    size_t capacity;
    size_t count;
    size_t written;
    size_t received;
    uint16_t run_length;
    uint8_t run_color;
    uint8_t run_state;
} esp_bms_companion_decoder_t;

bool esp_bms_companion_decode(esp_bms_companion_decoder_t *decoder,
                              size_t offset, const uint8_t *data, size_t length);
