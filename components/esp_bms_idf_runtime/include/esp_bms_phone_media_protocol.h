#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ESP_BMS_PHONE_MEDIA_PROTOCOL_VERSION 1U
#define ESP_BMS_PHONE_MEDIA_TITLE_MAX_LEN 96U
#define ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN 2U

#define ESP_BMS_PHONE_MEDIA_STATE_READY (UINT8_C(1) << 0)
#define ESP_BMS_PHONE_MEDIA_STATE_ACTIVE (UINT8_C(1) << 1)
#define ESP_BMS_PHONE_MEDIA_STATE_PLAYING (UINT8_C(1) << 2)

typedef enum {
    ESP_BMS_PHONE_MEDIA_COMMAND_PREVIOUS = 1,
    ESP_BMS_PHONE_MEDIA_COMMAND_NEXT = 2,
    ESP_BMS_PHONE_MEDIA_COMMAND_VOLUME_DOWN = 3,
    ESP_BMS_PHONE_MEDIA_COMMAND_VOLUME_UP = 4,
} esp_bms_phone_media_command_t;

typedef struct {
    uint8_t flags;
    char title[ESP_BMS_PHONE_MEDIA_TITLE_MAX_LEN + 1U];
} esp_bms_phone_media_state_t;

static inline bool esp_bms_phone_media_utf8_valid(const uint8_t *text, size_t len)
{
    if (!text && len != 0U) {
        return false;
    }
    for (size_t index = 0U; index < len;) {
        const uint8_t first = text[index++];
        if (first < 0x80U) {
            continue;
        }
        uint32_t codepoint = 0U;
        size_t continuation = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            codepoint = first & 0x1FU;
            continuation = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codepoint = first & 0x0FU;
            continuation = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codepoint = first & 0x07U;
            continuation = 3U;
        } else {
            return false;
        }
        if (continuation > len - index) {
            return false;
        }
        for (size_t offset = 0U; offset < continuation; ++offset) {
            const uint8_t next = text[index++];
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if ((continuation == 2U && codepoint < 0x800U) ||
            (continuation == 3U && codepoint < 0x10000U) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
            return false;
        }
    }
    return true;
}

static inline bool esp_bms_phone_media_state_decode(const uint8_t *data,
                                                     size_t len,
                                                     esp_bms_phone_media_state_t *out)
{
    if (!data || !out || len < ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN ||
        len > ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN + ESP_BMS_PHONE_MEDIA_TITLE_MAX_LEN ||
        data[0] != ESP_BMS_PHONE_MEDIA_PROTOCOL_VERSION ||
        !esp_bms_phone_media_utf8_valid(data + ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN,
                                        len - ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN)) {
        return false;
    }
    out->flags = data[1];
    const size_t title_len = len - ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN;
    memcpy(out->title, data + ESP_BMS_PHONE_MEDIA_STATE_HEADER_LEN, title_len);
    out->title[title_len] = '\0';
    return true;
}
