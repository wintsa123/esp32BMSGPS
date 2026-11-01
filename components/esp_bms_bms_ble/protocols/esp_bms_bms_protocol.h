#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT 6U
#define ESP_BMS_BMS_PROTOCOL_CELL_MAX_COUNT 32U

/* Partial frames update only present fields; complete frames keep all-field semantics. */
#define ESP_BMS_BMS_FIELD_PACK_VOLTAGE (UINT32_C(1) << 0U)
#define ESP_BMS_BMS_FIELD_CURRENT (UINT32_C(1) << 1U)
#define ESP_BMS_BMS_FIELD_SOC (UINT32_C(1) << 2U)
#define ESP_BMS_BMS_FIELD_MIN_CELL (UINT32_C(1) << 3U)
#define ESP_BMS_BMS_FIELD_AVERAGE_CELL (UINT32_C(1) << 4U)
#define ESP_BMS_BMS_FIELD_MAX_CELL (UINT32_C(1) << 5U)
#define ESP_BMS_BMS_FIELD_DELTA_CELL (UINT32_C(1) << 6U)
#define ESP_BMS_BMS_FIELD_TOTAL_CAPACITY (UINT32_C(1) << 7U)
#define ESP_BMS_BMS_FIELD_CAPACITY_REMAINING (UINT32_C(1) << 8U)
#define ESP_BMS_BMS_FIELD_TEMPERATURES (UINT32_C(1) << 9U)

typedef struct {
    bool valid;
    uint8_t cell_count;
    uint16_t cell_voltage_mv[ESP_BMS_BMS_PROTOCOL_CELL_MAX_COUNT];
    uint16_t soh_percent;
    uint8_t charge_mos_state;
    uint8_t discharge_mos_state;
    uint8_t balance_state;
    uint32_t balance_mask;
    int32_t power_w;
    uint32_t charge_time_seconds;
    uint32_t last_charge_gap_seconds;
    uint16_t remaining_charge_minutes;
    uint16_t remaining_discharge_minutes;
    bool charge_time_valid;
    bool last_charge_gap_valid;
    bool remaining_charge_valid;
    bool remaining_discharge_valid;
} esp_bms_bms_ant_diagnostics_t;

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
    uint32_t running_time_seconds;
    bool total_cycle_valid;
    bool running_time_valid;
    int16_t temperatures_celsius[ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT];
    bool temperature_valid[ESP_BMS_BMS_PROTOCOL_TEMP_MAX_COUNT];
    uint64_t protection_mask;
    uint64_t warning_mask;
    bool balancing_supported;
    bool balancing_active;
    bool partial;
    uint32_t present_fields;
    esp_bms_bms_ant_diagnostics_t ant_diagnostics;
} esp_bms_bms_telemetry_t;
