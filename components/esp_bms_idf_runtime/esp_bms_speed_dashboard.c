#include "esp_bms_speed_dashboard.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define GPS_MOTION_ENTER_KMH 4U
#define GPS_MOTION_EXIT_KMH 2U
#define KNOT_MILLI_TO_KMH_NUMERATOR 1852U
#define KNOT_MILLI_TO_KMH_DENOMINATOR 1000000U

static bool is_leap_year(uint16_t year)
{
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = { 31U, 28U, 31U, 30U, 31U, 30U,
                                    31U, 31U, 30U, 31U, 30U, 31U };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return days[month - 1U];
}

bool esp_bms_gps_utc_to_local_utc8(const esp_bms_gps_datetime_t *utc,
                                    esp_bms_gps_datetime_t *local)
{
    if (!utc || !local || utc->year == 0U || utc->month == 0U || utc->month > 12U ||
        utc->day == 0U || utc->day > days_in_month(utc->year, utc->month) ||
        utc->hour > 23U || utc->minute > 59U || utc->second > 60U) {
        return false;
    }

    *local = *utc;
    const uint8_t shifted_hour = (uint8_t)(utc->hour + 8U);
    local->hour = (uint8_t)(shifted_hour % 24U);
    if (shifted_hour < 24U) {
        return true;
    }

    local->day++;
    if (local->day <= days_in_month(local->year, local->month)) {
        return true;
    }
    local->day = 1U;
    local->month++;
    if (local->month <= 12U) {
        return true;
    }
    local->month = 1U;
    if (local->year == UINT16_MAX) {
        return false;
    }
    local->year++;
    return true;
}

bool esp_bms_gps_speed_knots_milli_parse(const char *field,
                                          size_t field_len,
                                          uint32_t *speed_knots_milli)
{
    if (!speed_knots_milli || (!field && field_len > 0U)) {
        return false;
    }
    if (field_len == 0U) {
        *speed_knots_milli = 0U;
        return true;
    }

    uint64_t whole = 0U;
    uint32_t fraction = 0U;
    uint32_t fraction_scale = 100U;
    bool seen_digit = false;
    bool seen_decimal = false;
    for (size_t index = 0U; index < field_len; ++index) {
        const char value = field[index];
        if (value >= '0' && value <= '9') {
            seen_digit = true;
            if (seen_decimal) {
                if (fraction_scale > 0U) {
                    fraction += (uint32_t)(value - '0') * fraction_scale;
                    fraction_scale /= 10U;
                }
            } else {
                whole = (whole * 10U) + (uint32_t)(value - '0');
                if (whole > UINT32_MAX) {
                    return false;
                }
            }
        } else if (value == '.' && !seen_decimal) {
            seen_decimal = true;
        } else {
            return false;
        }
    }

    const uint64_t milli = (whole * 1000U) + fraction;
    if (!seen_digit || milli > UINT32_MAX) {
        return false;
    }
    *speed_knots_milli = (uint32_t)milli;
    return true;
}

void esp_bms_gps_motion_filter_reset(esp_bms_gps_motion_filter_t *filter)
{
    if (filter) {
        filter->moving = false;
    }
}

uint32_t esp_bms_gps_motion_filter_apply(esp_bms_gps_motion_filter_t *filter,
                                         bool gps_fix_valid,
                                         uint32_t speed_knots_milli)
{
    if (!filter || !gps_fix_valid) {
        esp_bms_gps_motion_filter_reset(filter);
        return 0U;
    }

    const uint64_t speed_kmh_scaled =
        (uint64_t)speed_knots_milli * KNOT_MILLI_TO_KMH_NUMERATOR;
    if (!filter->moving) {
        if (speed_kmh_scaled <
            (uint64_t)GPS_MOTION_ENTER_KMH * KNOT_MILLI_TO_KMH_DENOMINATOR) {
            return 0U;
        }
        filter->moving = true;
    } else if (speed_kmh_scaled <=
               (uint64_t)GPS_MOTION_EXIT_KMH * KNOT_MILLI_TO_KMH_DENOMINATOR) {
        filter->moving = false;
        return 0U;
    }

    return speed_knots_milli;
}

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
