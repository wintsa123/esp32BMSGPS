#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_GPS_STREAM_CAPACITY 96U

typedef enum {
    ESP_BMS_GPS_STREAM_EVENT_NONE = 0,
    ESP_BMS_GPS_STREAM_EVENT_LINE,
    ESP_BMS_GPS_STREAM_EVENT_OVERFLOW,
} esp_bms_gps_stream_event_t;

typedef struct {
    uint8_t line[ESP_BMS_GPS_STREAM_CAPACITY];
    uint16_t line_len;
    bool collecting;
    bool discarding;
} esp_bms_gps_stream_t;

void esp_bms_gps_stream_reset(esp_bms_gps_stream_t *stream);

bool esp_bms_gps_stream_line_is_rmc(const uint8_t *line, size_t line_len);

esp_bms_gps_stream_event_t esp_bms_gps_stream_feed(esp_bms_gps_stream_t *stream,
                                                    uint8_t byte);

#ifdef __cplusplus
}
#endif
