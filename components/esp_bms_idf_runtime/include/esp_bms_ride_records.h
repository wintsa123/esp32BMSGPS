#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_RIDE_RECORD_MAX_COUNT 5U
#define ESP_BMS_RIDE_RECORD_TEMP_MAX_COUNT 6U
#define ESP_BMS_RIDE_RECORDS_FORMAT_VERSION 1U

typedef struct {
    uint32_t pack_voltage_mv;
    int16_t current_deci_amps;
    uint16_t delta_cell_voltage_mv;
    uint16_t soc_percent;
    uint8_t temperature_valid_mask;
    int16_t temperatures_celsius[ESP_BMS_RIDE_RECORD_TEMP_MAX_COUNT];
} esp_bms_ride_record_snapshot_t;

typedef struct {
    esp_bms_ride_record_snapshot_t max_current;
    esp_bms_ride_record_snapshot_t max_delta;
} esp_bms_ride_record_t;

typedef struct {
    uint32_t format_version;
    uint8_t count;
    uint8_t reserved[3];
    esp_bms_ride_record_t records[ESP_BMS_RIDE_RECORD_MAX_COUNT];
} esp_bms_ride_records_t;

typedef struct {
    bool valid;
    esp_bms_ride_record_snapshot_t snapshot;
} esp_bms_ride_record_sample_t;

void esp_bms_ride_records_reset(esp_bms_ride_records_t *records);
bool esp_bms_ride_records_valid(const esp_bms_ride_records_t *records);
bool esp_bms_ride_records_apply(esp_bms_ride_records_t *records,
                                 bool *session_started,
                                 const esp_bms_ride_record_sample_t *sample);

#ifdef __cplusplus
}
#endif
