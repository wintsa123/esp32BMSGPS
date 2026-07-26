#include "esp_bms_ride_records.h"

#include <string.h>

static uint32_t current_magnitude(int16_t current_deci_amps)
{
    const int32_t expanded = current_deci_amps;
    return (uint32_t)(expanded < 0 ? -expanded : expanded);
}

void esp_bms_ride_records_reset(esp_bms_ride_records_t *records)
{
    if (!records) {
        return;
    }
    memset(records, 0, sizeof(*records));
    records->format_version = ESP_BMS_RIDE_RECORDS_FORMAT_VERSION;
}

bool esp_bms_ride_records_valid(const esp_bms_ride_records_t *records)
{
    return records && records->format_version == ESP_BMS_RIDE_RECORDS_FORMAT_VERSION &&
           records->count <= ESP_BMS_RIDE_RECORD_MAX_COUNT;
}

bool esp_bms_ride_records_apply(esp_bms_ride_records_t *records,
                                 bool *session_started,
                                 const esp_bms_ride_record_sample_t *sample)
{
    if (!records || !session_started || !sample || !sample->valid) {
        return false;
    }
    if (!esp_bms_ride_records_valid(records)) {
        esp_bms_ride_records_reset(records);
        *session_started = false;
    }

    if (!*session_started) {
        if (records->count == ESP_BMS_RIDE_RECORD_MAX_COUNT) {
            memmove(records->records,
                    records->records + 1U,
                    sizeof(records->records) - sizeof(records->records[0]));
            records->count--;
        }
        esp_bms_ride_record_t *record = &records->records[records->count++];
        record->max_current = sample->snapshot;
        record->max_delta = sample->snapshot;
        *session_started = true;
        return true;
    }

    esp_bms_ride_record_t *record = &records->records[records->count - 1U];
    bool changed = false;
    if (current_magnitude(sample->snapshot.current_deci_amps) >
        current_magnitude(record->max_current.current_deci_amps)) {
        record->max_current = sample->snapshot;
        changed = true;
    }
    if (sample->snapshot.delta_cell_voltage_mv > record->max_delta.delta_cell_voltage_mv) {
        record->max_delta = sample->snapshot;
        changed = true;
    }
    return changed;
}
