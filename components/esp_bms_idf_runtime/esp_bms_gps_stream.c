#include "esp_bms_gps_stream.h"

#include <string.h>

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
