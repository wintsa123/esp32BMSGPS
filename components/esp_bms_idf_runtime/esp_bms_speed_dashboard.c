#include "esp_bms_speed_dashboard.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

void esp_bms_trip_efficiency_reset(esp_bms_trip_efficiency_t *trip)
{
    if (trip) {
        memset(trip, 0, sizeof(*trip));
    }
}

static bool trip_start_speed_reached(uint32_t speed_knots_milli)
{
    return (uint64_t)speed_knots_milli * UINT64_C(1852) >=
           (uint64_t)ESP_BMS_TRIP_START_SPEED_DECI_KMH * UINT64_C(100000);
}

static int64_t trip_power_mw(uint32_t pack_voltage_mv, int16_t bms_current_deci_amps)
{
    const int64_t display_current_deci_amps = -(int64_t)bms_current_deci_amps;
    return ((int64_t)pack_voltage_mv * display_current_deci_amps) / INT64_C(10);
}

static uint64_t saturating_add_u64(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static int64_t saturating_add_i64(int64_t left, int64_t right)
{
    if (right > 0 && left > INT64_MAX - right) {
        return INT64_MAX;
    }
    if (right < 0 && left < INT64_MIN - right) {
        return INT64_MIN;
    }
    return left + right;
}

void esp_bms_trip_efficiency_sample(esp_bms_trip_efficiency_t *trip,
                                    int64_t now_us,
                                    bool gps_fix_valid,
                                    uint32_t speed_knots_milli,
                                    bool bms_sample_valid,
                                    uint32_t pack_voltage_mv,
                                    int16_t bms_current_deci_amps)
{
    if (!trip) {
        return;
    }

    if (!trip->started && gps_fix_valid && trip_start_speed_reached(speed_knots_milli)) {
        trip->started = true;
    }
    if (!trip->started || !gps_fix_valid || !bms_sample_valid || pack_voltage_mv == 0U) {
        trip->anchor_valid = false;
        return;
    }

    const int64_t power_mw = trip_power_mw(pack_voltage_mv, bms_current_deci_amps);
    if (!trip->anchor_valid) {
        trip->anchor_time_us = now_us;
        trip->anchor_speed_knots_milli = speed_knots_milli;
        trip->anchor_power_mw = power_mw;
        trip->anchor_valid = true;
        return;
    }

    const int64_t interval_us = now_us - trip->anchor_time_us;
    if (interval_us <= 0 || interval_us > ESP_BMS_TRIP_MAX_INTERVAL_US) {
        trip->anchor_time_us = now_us;
        trip->anchor_speed_knots_milli = speed_knots_milli;
        trip->anchor_power_mw = power_mw;
        return;
    }

    const uint64_t interval_ms = (uint64_t)interval_us / UINT64_C(1000);
    const uint64_t speed_sum = (uint64_t)trip->anchor_speed_knots_milli +
                               (uint64_t)speed_knots_milli;
    const uint64_t distance_mm = speed_sum * UINT64_C(1852) * interval_ms /
                                 UINT64_C(7200000);
    const int64_t power_sum = trip->anchor_power_mw + power_mw;
    const int64_t energy_uwh = (power_sum * (int64_t)interval_ms) / INT64_C(7200);

    trip->distance_mm = saturating_add_u64(trip->distance_mm, distance_mm);
    trip->energy_uwh = saturating_add_i64(trip->energy_uwh, energy_uwh);
    trip->anchor_time_us = now_us;
    trip->anchor_speed_knots_milli = speed_knots_milli;
    trip->anchor_power_mw = power_mw;
}

static uint64_t abs_i64_to_u64(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

static uint64_t metric_milli_wh_per_km(uint64_t energy_uwh, uint64_t distance_mm)
{
    const uint64_t whole = energy_uwh / distance_mm;
    const uint64_t remainder = energy_uwh % distance_mm;
    if (whole > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    const uint64_t scaled_whole = whole * UINT64_C(1000);
    const uint64_t scaled_remainder = remainder > UINT64_MAX / UINT64_C(1000)
                                          ? UINT64_MAX
                                          : remainder * UINT64_C(1000);
    const uint64_t rounded_fraction = scaled_remainder == UINT64_MAX
                                          ? UINT64_MAX
                                          : (scaled_remainder + distance_mm / 2U) / distance_mm;
    return saturating_add_u64(scaled_whole, rounded_fraction);
}

bool esp_bms_trip_efficiency_consumption(const esp_bms_trip_efficiency_t *trip,
                                         bool imperial,
                                         int32_t *out_deci_wh_per_distance)
{
    if (!trip || !out_deci_wh_per_distance ||
        trip->distance_mm < ESP_BMS_TRIP_MIN_DISTANCE_MM) {
        return false;
    }

    const bool negative = trip->energy_uwh < 0;
    const uint64_t metric_milli = metric_milli_wh_per_km(abs_i64_to_u64(trip->energy_uwh),
                                                          trip->distance_mm);
    uint64_t deci = 0U;
    if (imperial) {
        if (metric_milli > UINT64_MAX / UINT64_C(1609344)) {
            deci = UINT64_MAX;
        } else {
            deci = (metric_milli * UINT64_C(1609344) + UINT64_C(50000000)) /
                   UINT64_C(100000000);
        }
    } else {
        deci = (metric_milli + UINT64_C(50)) / UINT64_C(100);
    }

    const uint64_t limit = negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX;
    if (deci > limit) {
        *out_deci_wh_per_distance = negative ? INT32_MIN : INT32_MAX;
    } else if (negative) {
        *out_deci_wh_per_distance = deci == (uint64_t)INT32_MAX + 1U
                                        ? INT32_MIN
                                        : -(int32_t)deci;
    } else {
        *out_deci_wh_per_distance = (int32_t)deci;
    }
    return true;
}

static uint16_t rounded_range_km(uint64_t numerator, uint64_t denominator)
{
    uint64_t range_km = numerator / denominator;
    if (range_km < ESP_BMS_REMAINING_RANGE_MAX_KM &&
        numerator % denominator >= (denominator + 1U) / 2U) {
        range_km++;
    }
    return range_km > ESP_BMS_REMAINING_RANGE_MAX_KM
               ? ESP_BMS_REMAINING_RANGE_MAX_KM
               : (uint16_t)range_km;
}

bool esp_bms_remaining_range_km(uint16_t preset_range_km,
                                bool soc_valid,
                                uint16_t soc_percent,
                                bool measured_valid,
                                uint32_t pack_voltage_mv,
                                uint32_t capacity_remaining_mah,
                                int32_t consumption_deci_wh_per_km,
                                uint16_t *out_range_km)
{
    if (!out_range_km || preset_range_km > ESP_BMS_REMAINING_RANGE_MAX_KM) {
        return false;
    }

    if (measured_valid && pack_voltage_mv > 0U && capacity_remaining_mah > 0U &&
        consumption_deci_wh_per_km > 0) {
        const uint64_t numerator = (uint64_t)pack_voltage_mv * capacity_remaining_mah;
        const uint64_t denominator = UINT64_C(100000) *
                                     (uint32_t)consumption_deci_wh_per_km;
        *out_range_km = rounded_range_km(numerator, denominator);
        return true;
    }

    if (!soc_valid) {
        return false;
    }
    const uint16_t clamped_soc = soc_percent > 100U ? 100U : soc_percent;
    *out_range_km = rounded_range_km((uint64_t)preset_range_km * clamped_soc, 100U);
    return true;
}
