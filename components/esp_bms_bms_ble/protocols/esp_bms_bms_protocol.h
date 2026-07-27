#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT 6U

typedef struct {
    uint32_t pack_voltage_mv;
    int16_t current_deci_amps;
    uint16_t soc_percent;
    uint16_t min_cell_voltage_mv;
    uint16_t average_cell_voltage_mv;
    uint16_t max_cell_voltage_mv;
    uint16_t delta_cell_voltage_mv;
    uint32_t total_capacity_mah;
    uint32_t capacity_remaining_mah;
    uint32_t total_cycle_mah;
    bool total_cycle_valid;
    int16_t temperatures_celsius[ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT];
    bool temperature_valid[ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT];
    uint64_t protection_mask;
    uint64_t warning_mask;
    bool partial;
} esp_bms_bms_telemetry_t;
