#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_bms_gps_stream.h"
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

static void test_gps_speed_field_parse(void)
{
    uint32_t speed_knots_milli = UINT32_MAX;
    assert(esp_bms_gps_speed_knots_milli_parse("", 0U, &speed_knots_milli));
    assert(speed_knots_milli == 0U);
    assert(esp_bms_gps_speed_knots_milli_parse("1.234", 5U, &speed_knots_milli));
    assert(speed_knots_milli == 1234U);
    assert(esp_bms_gps_speed_knots_milli_parse("0.0019", 6U, &speed_knots_milli));
    assert(speed_knots_milli == 1U);
    assert(!esp_bms_gps_speed_knots_milli_parse("1.2x", 4U, &speed_knots_milli));
    assert(!esp_bms_gps_speed_knots_milli_parse(".", 1U, &speed_knots_milli));
}

static void test_gps_motion_hysteresis(void)
{
    esp_bms_gps_motion_filter_t filter = { 0 };

    assert(esp_bms_gps_motion_filter_apply(&filter, true, 2105U) == 0U);
    assert(!filter.moving);
    assert(esp_bms_gps_motion_filter_apply(&filter, true, 2160U) == 2160U);
    assert(filter.moving);
    assert(esp_bms_gps_motion_filter_apply(&filter, true, 1134U) == 1134U);
    assert(filter.moving);
    assert(esp_bms_gps_motion_filter_apply(&filter, true, 1079U) == 0U);
    assert(!filter.moving);

    assert(esp_bms_gps_motion_filter_apply(&filter, true, 2160U) == 2160U);
    assert(esp_bms_gps_motion_filter_apply(&filter, false, 2160U) == 0U);
    assert(!filter.moving);
    assert(esp_bms_gps_motion_filter_apply(&filter, true, 2105U) == 0U);
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

static void test_remaining_range(void)
{
    uint16_t range_km = UINT16_MAX;
    uint16_t kmh_range_km = UINT16_MAX;
    uint16_t mph_range_km = UINT16_MAX;

    assert(esp_bms_remaining_range_km(ESP_BMS_PRESET_RANGE_DEFAULT_KM,
                                      true, 76U, false, 0U, 0U, 0, &range_km));
    assert(range_km == 76U);
    assert(esp_bms_remaining_range_km(0U, true, 76U, false, 0U, 0U, 0, &range_km));
    assert(range_km == 0U);
    assert(esp_bms_remaining_range_km(101U, true, 75U, false, 0U, 0U, 0, &range_km));
    assert(range_km == 76U);

    assert(esp_bms_remaining_range_km(100U, true, 76U, true,
                                      76000U, 76000U, 238, &range_km));
    assert(range_km == 243U);
    assert(esp_bms_remaining_range_km(100U, true, 76U, true,
                                      76000U, 76000U, 238, &kmh_range_km));
    assert(esp_bms_remaining_range_km(100U, true, 76U, true,
                                      76000U, 76000U, 238, &mph_range_km));
    assert(kmh_range_km == mph_range_km);
    assert(esp_bms_remaining_range_km(100U, true, 76U, true,
                                      76000U, 76000U, 0, &range_km));
    assert(range_km == 76U);
    assert(esp_bms_remaining_range_km(100U, true, 76U, true,
                                      76000U, 76000U, -1, &range_km));
    assert(range_km == 76U);
    assert(!esp_bms_remaining_range_km(100U, false, 0U, true,
                                       76000U, 76000U, 0, &range_km));

    assert(esp_bms_remaining_range_km(9999U, true, 100U, true,
                                      UINT32_MAX, UINT32_MAX, 1, &range_km));
    assert(range_km == ESP_BMS_REMAINING_RANGE_MAX_KM);
    assert(!esp_bms_remaining_range_km(10000U, true, 100U, false,
                                       0U, 0U, 0, &range_km));
}

int main(void)
{
    test_utc8_rollover();
    test_gps_speed_field_parse();
    test_gps_motion_hysteresis();
    test_trip_threshold_alignment_and_units();
    test_regeneration_offsets_discharge();
    test_remaining_range();
    puts("speed dashboard self-test passed");
    return 0;
}
