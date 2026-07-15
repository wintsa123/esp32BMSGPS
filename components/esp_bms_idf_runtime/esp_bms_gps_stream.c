#include "esp_bms_gps_stream.h"

#include <string.h>

static uint32_t casbin_read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void casbin_write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t casbin_checksum(uint8_t message_class,
                               uint8_t message_id,
                               const uint8_t *payload,
                               size_t payload_len)
{
    uint32_t checksum = (uint32_t)payload_len |
                        ((uint32_t)message_class << 16U) |
                        ((uint32_t)message_id << 24U);
    for (size_t offset = 0U; offset < payload_len; offset += 4U) {
        checksum += casbin_read_le32(payload + offset);
    }
    return checksum;
}

void esp_bms_gps_stream_reset(esp_bms_gps_stream_t *stream)
{
    if (stream) {
        memset(stream, 0, sizeof(*stream));
    }
}

bool esp_bms_gps_stream_line_is_rmc(const uint8_t *line, size_t line_len)
{
    static const char kinds[][6] = { "GPRMC", "GNRMC", "GARMC", "GLRMC", "BDRMC" };
    if (!line || line_len < 7U || line[0] != '$' || line[6] != ',') {
        return false;
    }
    for (size_t index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        if (memcmp(&line[1], kinds[index], 5U) == 0) {
            return true;
        }
    }
    return false;
}

esp_bms_gps_stream_event_t esp_bms_gps_stream_feed(esp_bms_gps_stream_t *stream,
                                                    uint8_t byte)
{
    if (!stream) {
        return ESP_BMS_GPS_STREAM_EVENT_NONE;
    }

    if (byte == '$') {
        stream->line[0] = byte;
        stream->line_len = 1U;
        stream->collecting = true;
        stream->discarding = false;
        return ESP_BMS_GPS_STREAM_EVENT_NONE;
    }

    if (byte == '\n') {
        if (stream->discarding) {
            stream->discarding = false;
            stream->line_len = 0U;
            return ESP_BMS_GPS_STREAM_EVENT_NONE;
        }
        if (!stream->collecting || stream->line_len == 0U) {
            return ESP_BMS_GPS_STREAM_EVENT_NONE;
        }
        stream->collecting = false;
        return ESP_BMS_GPS_STREAM_EVENT_LINE;
    }

    if (byte == '\r' || !stream->collecting) {
        return ESP_BMS_GPS_STREAM_EVENT_NONE;
    }

    if (stream->line_len >= ESP_BMS_GPS_STREAM_CAPACITY) {
        stream->line_len = 0U;
        stream->collecting = false;
        stream->discarding = true;
        return ESP_BMS_GPS_STREAM_EVENT_OVERFLOW;
    }

    stream->line[stream->line_len++] = byte;
    return ESP_BMS_GPS_STREAM_EVENT_NONE;
}

void esp_bms_gps_casbin_stream_reset(esp_bms_gps_casbin_stream_t *stream)
{
    if (stream) {
        memset(stream, 0, sizeof(*stream));
    }
}

bool esp_bms_gps_casbin_stream_active(const esp_bms_gps_casbin_stream_t *stream)
{
    return stream && stream->collecting;
}

esp_bms_gps_casbin_event_t
esp_bms_gps_casbin_stream_feed(esp_bms_gps_casbin_stream_t *stream, uint8_t byte)
{
    if (!stream) {
        return ESP_BMS_GPS_CASBIN_EVENT_NONE;
    }

    if (!stream->collecting) {
        if (byte != 0xBAU) {
            return ESP_BMS_GPS_CASBIN_EVENT_NONE;
        }
        stream->frame[0] = byte;
        stream->frame_len = 1U;
        stream->expected_len = 0U;
        stream->collecting = true;
        return ESP_BMS_GPS_CASBIN_EVENT_NONE;
    }

    if (stream->frame_len == 1U && byte != 0xCEU) {
        if (byte == 0xBAU) {
            stream->frame[0] = byte;
            return ESP_BMS_GPS_CASBIN_EVENT_NONE;
        }
        stream->collecting = false;
        stream->frame_len = 0U;
        return ESP_BMS_GPS_CASBIN_EVENT_ERROR;
    }

    if (stream->frame_len >= sizeof(stream->frame)) {
        stream->collecting = false;
        stream->frame_len = 0U;
        return ESP_BMS_GPS_CASBIN_EVENT_ERROR;
    }
    stream->frame[stream->frame_len++] = byte;

    if (stream->frame_len == 4U) {
        stream->payload_len = (uint16_t)stream->frame[2] |
                              ((uint16_t)stream->frame[3] << 8U);
        if (stream->payload_len > ESP_BMS_GPS_CASBIN_MAX_PAYLOAD ||
            (stream->payload_len & 3U) != 0U) {
            stream->collecting = false;
            stream->frame_len = 0U;
            return ESP_BMS_GPS_CASBIN_EVENT_ERROR;
        }
        stream->expected_len = (uint16_t)(stream->payload_len + 10U);
    }

    if (stream->expected_len == 0U || stream->frame_len < stream->expected_len) {
        return ESP_BMS_GPS_CASBIN_EVENT_NONE;
    }

    stream->collecting = false;
    stream->message_class = stream->frame[4];
    stream->message_id = stream->frame[5];
    const uint32_t expected_checksum =
        casbin_read_le32(&stream->frame[6U + stream->payload_len]);
    const uint32_t actual_checksum =
        casbin_checksum(stream->message_class,
                        stream->message_id,
                        &stream->frame[6],
                        stream->payload_len);
    return expected_checksum == actual_checksum ? ESP_BMS_GPS_CASBIN_EVENT_FRAME
                                                : ESP_BMS_GPS_CASBIN_EVENT_ERROR;
}

size_t esp_bms_gps_casbin_build(uint8_t message_class,
                                uint8_t message_id,
                                const uint8_t *payload,
                                size_t payload_len,
                                uint8_t *frame,
                                size_t frame_capacity)
{
    const size_t frame_len = payload_len + 10U;
    if (!frame || payload_len > ESP_BMS_GPS_CASBIN_MAX_PAYLOAD ||
        (payload_len & 3U) != 0U || frame_capacity < frame_len ||
        (payload_len > 0U && !payload)) {
        return 0U;
    }

    frame[0] = 0xBAU;
    frame[1] = 0xCEU;
    frame[2] = (uint8_t)payload_len;
    frame[3] = (uint8_t)(payload_len >> 8U);
    frame[4] = message_class;
    frame[5] = message_id;
    if (payload_len > 0U) {
        memcpy(&frame[6], payload, payload_len);
    }
    casbin_write_le32(&frame[6U + payload_len],
                      casbin_checksum(message_class, message_id, payload, payload_len));
    return frame_len;
}
