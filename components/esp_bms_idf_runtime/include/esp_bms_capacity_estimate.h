#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ESP_BMS_CAPACITY_ESTIMATE_MIN_SOC_SPAN 20U
#define ESP_BMS_CAPACITY_ESTIMATE_MIN_CYCLE_DELTA_MAH 1000U

typedef struct {
    uint32_t estimate_mah;
    uint32_t last_accepted_cycle_mah;
    uint32_t anchor_cycle_mah;
    uint8_t sample_count;
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

void esp_bms_capacity_estimate_reset(esp_bms_capacity_estimate_t *state);
void esp_bms_capacity_estimate_reset_anchor(esp_bms_capacity_estimate_t *state);
esp_bms_capacity_estimate_result_t esp_bms_capacity_estimate_observe(
    esp_bms_capacity_estimate_t *state,
    uint32_t total_cycle_mah,
    uint16_t soc_percent);
