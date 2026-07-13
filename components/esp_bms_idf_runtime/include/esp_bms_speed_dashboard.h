#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BMS_TRIP_START_SPEED_DECI_KMH 50U
#define ESP_BMS_TRIP_MIN_DISTANCE_MM UINT64_C(100000)
#define ESP_BMS_TRIP_MAX_INTERVAL_US INT64_C(3000000)

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} esp_bms_gps_datetime_t;

typedef struct {
    uint64_t distance_mm;
    int64_t energy_uwh;
    int64_t anchor_time_us;
    int64_t anchor_power_mw;
    uint32_t anchor_speed_knots_milli;
    bool started;
    bool anchor_valid;
} esp_bms_trip_efficiency_t;

bool esp_bms_gps_utc_to_local_utc8(const esp_bms_gps_datetime_t *utc,
                                    esp_bms_gps_datetime_t *local);

void esp_bms_trip_efficiency_reset(esp_bms_trip_efficiency_t *trip);

void esp_bms_trip_efficiency_sample(esp_bms_trip_efficiency_t *trip,
                                    int64_t now_us,
                                    bool gps_fix_valid,
                                    uint32_t speed_knots_milli,
                                    bool bms_sample_valid,
                                    uint32_t pack_voltage_mv,
                                    int16_t bms_current_deci_amps);

bool esp_bms_trip_efficiency_consumption(const esp_bms_trip_efficiency_t *trip,
                                         bool imperial,
                                         int32_t *out_deci_wh_per_distance);

#ifdef __cplusplus
}
#endif
