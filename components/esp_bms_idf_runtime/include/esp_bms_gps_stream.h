#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_GPS_STREAM_CAPACITY 96U
#define ESP_BMS_GPS_CASBIN_OVERHEAD 10U
#define ESP_BMS_GPS_CASBIN_MAX_PAYLOAD 524U
#define ESP_BMS_GPS_CASBIN_MAX_FRAME \
    (ESP_BMS_GPS_CASBIN_MAX_PAYLOAD + ESP_BMS_GPS_CASBIN_OVERHEAD)

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

typedef enum {
    ESP_BMS_GPS_CASBIN_EVENT_NONE = 0,
    ESP_BMS_GPS_CASBIN_EVENT_FRAME,
    ESP_BMS_GPS_CASBIN_EVENT_ERROR,
} esp_bms_gps_casbin_event_t;

typedef struct {
    uint8_t frame[ESP_BMS_GPS_CASBIN_MAX_FRAME];
    uint16_t frame_len;
    uint16_t expected_len;
    uint16_t payload_len;
    uint8_t message_class;
    uint8_t message_id;
    bool collecting;
} esp_bms_gps_casbin_stream_t;

void esp_bms_gps_stream_reset(esp_bms_gps_stream_t *stream);

bool esp_bms_gps_stream_line_is_rmc(const uint8_t *line, size_t line_len);

bool esp_bms_gps_stream_nmea_checksum_valid(const uint8_t *line, size_t line_len);

esp_bms_gps_stream_event_t esp_bms_gps_stream_feed(esp_bms_gps_stream_t *stream,
                                                    uint8_t byte);

void esp_bms_gps_casbin_stream_reset(esp_bms_gps_casbin_stream_t *stream);

bool esp_bms_gps_casbin_stream_active(const esp_bms_gps_casbin_stream_t *stream);

esp_bms_gps_casbin_event_t
esp_bms_gps_casbin_stream_feed(esp_bms_gps_casbin_stream_t *stream, uint8_t byte);

bool esp_bms_gps_casbin_agnss_payload_valid(uint8_t message_class,
                                            uint8_t message_id,
                                            const uint8_t *payload,
                                            size_t payload_len);

size_t esp_bms_gps_casbin_build(uint8_t message_class,
                                uint8_t message_id,
                                const uint8_t *payload,
                                size_t payload_len,
                                uint8_t *frame,
                                size_t frame_capacity);

#ifdef __cplusplus
}
#endif
