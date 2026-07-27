#include "esp_bms_capacity_estimate.h"

#include <limits.h>
#include <string.h>

static void set_anchor(esp_bms_capacity_estimate_t *state,
                       uint32_t total_cycle_mah,
                       uint8_t soc_percent)
{
    state->anchor_cycle_mah = total_cycle_mah;
    state->anchor_soc_percent = soc_percent;
    state->last_soc_percent = soc_percent;
    state->anchor_direction = 0;
    state->last_direction = 0;
    state->anchor_valid = true;
    state->last_observation_valid = true;
}

void esp_bms_capacity_estimate_reset(esp_bms_capacity_estimate_t *state)
{
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

void esp_bms_capacity_estimate_reset_anchor(esp_bms_capacity_estimate_t *state)
{
    if (state) {
        state->anchor_valid = false;
        state->anchor_direction = 0;
        state->last_observation_valid = false;
        state->last_direction = 0;
    }
}

esp_bms_capacity_estimate_result_t esp_bms_capacity_estimate_observe(
    esp_bms_capacity_estimate_t *state,
    uint32_t total_cycle_mah,
    uint16_t soc_percent)
{
    if (!state || soc_percent > 100U) {
        return ESP_BMS_CAPACITY_ESTIMATE_NO_CHANGE;
    }

    const uint8_t soc = (uint8_t)soc_percent;
    if (!state->anchor_valid) {
        if (state->ready && total_cycle_mah < state->last_accepted_cycle_mah) {
            esp_bms_capacity_estimate_reset(state);
            set_anchor(state, total_cycle_mah, soc);
            return ESP_BMS_CAPACITY_ESTIMATE_CLEARED;
        }
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_REANCHORED;
    }

    if (total_cycle_mah < state->anchor_cycle_mah) {
        esp_bms_capacity_estimate_reset(state);
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_CLEARED;
    }

    const int8_t recent_direction = state->last_observation_valid
                                        ? soc > state->last_soc_percent ? 1 :
                                          soc < state->last_soc_percent ? -1 : 0
                                        : 0;
    if (recent_direction != 0 && state->last_direction != 0 &&
        recent_direction != state->last_direction) {
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_REANCHORED;
    }
    if (recent_direction != 0) {
        state->last_direction = recent_direction;
    }
    state->last_soc_percent = soc;
    state->last_observation_valid = true;

    const int8_t direction = soc > state->anchor_soc_percent ? 1 :
                             soc < state->anchor_soc_percent ? -1 : 0;
    const uint32_t cycle_delta_mah = total_cycle_mah - state->anchor_cycle_mah;
    if (direction == 0) {
        return ESP_BMS_CAPACITY_ESTIMATE_NO_CHANGE;
    }
    if (cycle_delta_mah == 0U ||
        (state->anchor_direction != 0 && direction != state->anchor_direction)) {
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_REANCHORED;
    }
    if (state->anchor_direction == 0) {
        state->anchor_direction = direction;
    }

    const uint8_t soc_span = soc > state->anchor_soc_percent
                                 ? (uint8_t)(soc - state->anchor_soc_percent)
                                 : (uint8_t)(state->anchor_soc_percent - soc);
    if (soc_span < ESP_BMS_CAPACITY_ESTIMATE_MIN_SOC_SPAN ||
        cycle_delta_mah < ESP_BMS_CAPACITY_ESTIMATE_MIN_CYCLE_DELTA_MAH) {
        return ESP_BMS_CAPACITY_ESTIMATE_NO_CHANGE;
    }

    const uint64_t sample_mah = (uint64_t)cycle_delta_mah * 100U / soc_span;
    if (sample_mah == 0U || sample_mah > UINT32_MAX) {
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_REANCHORED;
    }
    if (!state->ready) {
        state->estimate_mah = (uint32_t)sample_mah;
        state->sample_count = 1U;
        state->ready = true;
    } else {
        const uint8_t weight = state->sample_count < 4U ? state->sample_count : 4U;
        state->estimate_mah = (uint32_t)(((uint64_t)state->estimate_mah * weight + sample_mah) /
                                         (weight + 1U));
        if (state->sample_count < 4U) {
            state->sample_count++;
        }
    }
    state->last_accepted_cycle_mah = total_cycle_mah;
    set_anchor(state, total_cycle_mah, soc);
    return ESP_BMS_CAPACITY_ESTIMATE_UPDATED;
}
