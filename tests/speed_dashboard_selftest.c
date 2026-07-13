#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_bms_speed_dashboard.h"

static void test_utc8_rollover(void)
{
    esp_bms_gps_datetime_t local = { 0 };
    const esp_bms_gps_datetime_t ordinary = { 2026U, 7U, 13U, 15U, 59U, 58U };
    assert(esp_bms_gps_utc_to_local_utc8(&ordinary, &local));
    assert(local.year == 2026U && local.month == 7U && local.day == 13U);
    assert(local.hour == 23U && local.minute == 59U && local.second == 58U);

    const esp_bms_gps_datetime_t month_end = { 2026U, 7U, 31U, 16U, 0U, 1U };
    assert(esp_bms_gps_utc_to_local_utc8(&month_end, &local));
    assert(local.year == 2026U && local.month == 8U && local.day == 1U && local.hour == 0U);

    const esp_bms_gps_datetime_t leap_day = { 2024U, 2U, 29U, 20U, 1U, 2U };
    assert(esp_bms_gps_utc_to_local_utc8(&leap_day, &local));
    assert(local.year == 2024U && local.month == 3U && local.day == 1U && local.hour == 4U);

    const esp_bms_gps_datetime_t year_end = { 2026U, 12U, 31U, 23U, 1U, 2U };
    assert(esp_bms_gps_utc_to_local_utc8(&year_end, &local));
    assert(local.year == 2027U && local.month == 1U && local.day == 1U && local.hour == 7U);
}

static void test_trip_threshold_alignment_and_units(void)
{
    esp_bms_trip_efficiency_t trip;
    esp_bms_trip_efficiency_reset(&trip);

    esp_bms_trip_efficiency_sample(&trip, 0, true, 2000U, true, 50000U, -100);
    assert(!trip.started && trip.distance_mm == 0U);

    esp_bms_trip_efficiency_sample(&trip, 1000000, true, 10000U, true, 50000U, -100);
    assert(trip.started && trip.anchor_valid && trip.distance_mm == 0U);
    for (int index = 2; index <= 22; ++index) {
        esp_bms_trip_efficiency_sample(&trip,
                                       (int64_t)index * INT64_C(1000000),
                                       true,
                                       10000U,
                                       true,
                                       50000U,
                                       -100);
    }

    assert(trip.distance_mm >= ESP_BMS_TRIP_MIN_DISTANCE_MM);
    int32_t metric_deci = 0;
    int32_t imperial_deci = 0;
    assert(esp_bms_trip_efficiency_consumption(&trip, false, &metric_deci));
    assert(esp_bms_trip_efficiency_consumption(&trip, true, &imperial_deci));
    assert(metric_deci >= 269 && metric_deci <= 271);
    assert(imperial_deci >= 432 && imperial_deci <= 437);

    const uint64_t distance_before_invalid = trip.distance_mm;
    esp_bms_trip_efficiency_sample(&trip, 23000000, true, 10000U, false, 0U, 0);
    esp_bms_trip_efficiency_sample(&trip, 24000000, true, 10000U, true, 50000U, -100);
    assert(trip.distance_mm == distance_before_invalid);
    esp_bms_trip_efficiency_sample(&trip, 29000000, true, 10000U, true, 50000U, -100);
    assert(trip.distance_mm == distance_before_invalid);
}

static void test_regeneration_offsets_discharge(void)
{
    esp_bms_trip_efficiency_t trip;
    esp_bms_trip_efficiency_reset(&trip);

    esp_bms_trip_efficiency_sample(&trip, 0, true, 10000U, true, 50000U, -100);
    for (int index = 1; index <= 25; ++index) {
        esp_bms_trip_efficiency_sample(&trip,
                                       (int64_t)index * INT64_C(1000000),
                                       true,
                                       10000U,
                                       true,
                                       50000U,
                                       index <= 12 ? -100 : 100);
    }

    assert(trip.distance_mm >= ESP_BMS_TRIP_MIN_DISTANCE_MM);
    int32_t consumption_deci = 1;
    assert(esp_bms_trip_efficiency_consumption(&trip, false, &consumption_deci));
    assert(consumption_deci >= -1 && consumption_deci <= 1);
}

int main(void)
{
    test_utc8_rollover();
    test_trip_threshold_alignment_and_units();
    test_regeneration_offsets_discharge();
    puts("speed dashboard self-test passed");
    return 0;
}
