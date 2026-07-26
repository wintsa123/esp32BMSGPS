#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_bms_ride_records.h"

static esp_bms_ride_record_sample_t sample(uint32_t voltage_mv,
                                            int16_t current_deci_amps,
                                            uint16_t delta_mv,
                                            uint16_t soc_percent)
{
    esp_bms_ride_record_sample_t value = {
        .valid = true,
        .snapshot = {
            .pack_voltage_mv = voltage_mv,
            .current_deci_amps = current_deci_amps,
            .delta_cell_voltage_mv = delta_mv,
            .soc_percent = soc_percent,
            .temperature_valid_mask = 0x03U,
            .temperatures_celsius = { 24, 26, 0, 0, 0, 0 },
        },
    };
    return value;
}

static void test_first_sample_and_strict_peaks(void)
{
    esp_bms_ride_records_t records;
    esp_bms_ride_records_reset(&records);
    bool session_started = false;
    const esp_bms_ride_record_sample_t first = sample(52000U, -100, 12U, 80U);

    assert(esp_bms_ride_records_apply(&records, &session_started, &first));
    assert(session_started && records.count == 1U);
    assert(memcmp(&records.records[0].max_current, &first.snapshot, sizeof(first.snapshot)) == 0);
    assert(memcmp(&records.records[0].max_delta, &first.snapshot, sizeof(first.snapshot)) == 0);

    const esp_bms_ride_record_sample_t smaller = sample(52100U, 100, 12U, 79U);
    assert(!esp_bms_ride_records_apply(&records, &session_started, &smaller));
    assert(records.records[0].max_current.pack_voltage_mv == 52000U);
    assert(records.records[0].max_delta.pack_voltage_mv == 52000U);

    const esp_bms_ride_record_sample_t higher_current = sample(52300U, 101, 12U, 78U);
    assert(esp_bms_ride_records_apply(&records, &session_started, &higher_current));
    assert(records.records[0].max_current.pack_voltage_mv == 52300U);
    assert(records.records[0].max_delta.pack_voltage_mv == 52000U);

    const esp_bms_ride_record_sample_t higher_delta = sample(51900U, -10, 13U, 77U);
    assert(esp_bms_ride_records_apply(&records, &session_started, &higher_delta));
    assert(records.records[0].max_delta.pack_voltage_mv == 51900U);

    const esp_bms_ride_record_sample_t both = sample(51800U, -102, 14U, 76U);
    assert(esp_bms_ride_records_apply(&records, &session_started, &both));
    assert(records.records[0].max_current.pack_voltage_mv == 51800U);
    assert(records.records[0].max_delta.pack_voltage_mv == 51800U);
}

static void test_int16_min_and_invalid_sample(void)
{
    esp_bms_ride_records_t records;
    esp_bms_ride_records_reset(&records);
    bool session_started = false;
    const esp_bms_ride_record_sample_t first = sample(50000U, INT16_MAX, 1U, 50U);
    assert(esp_bms_ride_records_apply(&records, &session_started, &first));

    const esp_bms_ride_record_sample_t minimum = sample(51000U, INT16_MIN, 2U, 51U);
    assert(esp_bms_ride_records_apply(&records, &session_started, &minimum));
    assert(records.records[0].max_current.current_deci_amps == INT16_MIN);

    esp_bms_ride_record_sample_t invalid = sample(52000U, -200, 30U, 52U);
    invalid.valid = false;
    assert(!esp_bms_ride_records_apply(&records, &session_started, &invalid));
    assert(records.count == 1U);
    assert(records.records[0].max_delta.delta_cell_voltage_mv == 2U);
}

static void test_six_sessions_keep_latest_five(void)
{
    esp_bms_ride_records_t records;
    esp_bms_ride_records_reset(&records);

    for (uint32_t index = 1U; index <= 6U; ++index) {
        bool session_started = false;
        const esp_bms_ride_record_sample_t value = sample(index, (int16_t)index, (uint16_t)index, 50U);
        assert(esp_bms_ride_records_apply(&records, &session_started, &value));
        assert(session_started);
    }

    assert(records.count == ESP_BMS_RIDE_RECORD_MAX_COUNT);
    for (uint32_t index = 0U; index < ESP_BMS_RIDE_RECORD_MAX_COUNT; ++index) {
        assert(records.records[index].max_current.pack_voltage_mv == index + 2U);
    }
}

int main(void)
{
    test_first_sample_and_strict_peaks();
    test_int16_min_and_invalid_sample();
    test_six_sessions_keep_latest_five();
    puts("ride records self-test passed");
    return 0;
}
