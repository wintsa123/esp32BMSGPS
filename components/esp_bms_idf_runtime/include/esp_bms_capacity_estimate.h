#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ESP_BMS_CAPACITY_ESTIMATE_MIN_SOC_SPAN 20U
#define ESP_BMS_CAPACITY_ESTIMATE_MIN_CYCLE_DELTA_MAH 1000U
#define ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT 8U
#define ESP_BMS_CAPACITY_ESTIMATE_READY_SAMPLE_COUNT 3U
#define ESP_BMS_CAPACITY_ESTIMATE_OUTLIER_PERCENT 25U
#define ESP_BMS_CAPACITY_INTEGRATOR_DEADBAND_DECI_AMPS 5U
#define ESP_BMS_CAPACITY_INTEGRATOR_MAX_INTERVAL_US INT64_C(3000000)
#define ESP_BMS_CAPACITY_INTEGRATOR_MAH_DIVISOR UINT64_C(36000000)

typedef struct {
    uint32_t estimate_mah;
    uint32_t last_accepted_cycle_mah;
    uint32_t anchor_cycle_mah;
    uint32_t sample_history_mah[ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT];
    uint8_t sample_count;
    uint8_t next_sample_index;
    uint8_t anchor_soc_percent;
    uint8_t last_soc_percent;
    int8_t anchor_direction;
    int8_t last_direction;
    bool ready;
    bool anchor_valid;
    bool last_observation_valid;
} esp_bms_capacity_estimate_t;

typedef enum {
    ESP_BMS_CAPACITY_ESTIMATE_NO_CHANGE = 0,
    ESP_BMS_CAPACITY_ESTIMATE_REANCHORED,
    ESP_BMS_CAPACITY_ESTIMATE_UPDATED,
    ESP_BMS_CAPACITY_ESTIMATE_CLEARED,
} esp_bms_capacity_estimate_result_t;

typedef struct {
    uint64_t fractional_deci_amp_us;
    int64_t last_sample_us;
    uint32_t total_cycle_mah;
    bool initialized;
    bool last_sample_valid;
} esp_bms_capacity_integrator_t;

typedef enum {
    ESP_BMS_CAPACITY_INTEGRATOR_NO_CHANGE = 0,
    ESP_BMS_CAPACITY_INTEGRATOR_REANCHORED,
    ESP_BMS_CAPACITY_INTEGRATOR_UPDATED,
    ESP_BMS_CAPACITY_INTEGRATOR_DISCONTINUITY,
} esp_bms_capacity_integrator_result_t;

void esp_bms_capacity_estimate_reset(esp_bms_capacity_estimate_t *state);
void esp_bms_capacity_estimate_reset_anchor(esp_bms_capacity_estimate_t *state);
esp_bms_capacity_estimate_result_t esp_bms_capacity_estimate_observe(
    esp_bms_capacity_estimate_t *state,
    uint32_t total_cycle_mah,
    uint16_t soc_percent);
void esp_bms_capacity_integrator_reset(esp_bms_capacity_integrator_t *state,
                                       uint32_t total_cycle_mah);
void esp_bms_capacity_integrator_reset_anchor(esp_bms_capacity_integrator_t *state);
esp_bms_capacity_integrator_result_t esp_bms_capacity_integrator_observe(
    esp_bms_capacity_integrator_t *state,
    int64_t now_us,
    int16_t current_deci_amps);
