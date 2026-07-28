#include "esp_bms_capacity_estimate.h"

#include <limits.h>
#include <string.h>

static uint32_t sample_history_median(const esp_bms_capacity_estimate_t *state)
{
    uint32_t sorted[ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT];
    memcpy(sorted, state->sample_history_mah, state->sample_count * sizeof(sorted[0]));
    for (uint8_t index = 1U; index < state->sample_count; ++index) {
        const uint32_t value = sorted[index];
        uint8_t insertion = index;
        while (insertion > 0U && sorted[insertion - 1U] > value) {
            sorted[insertion] = sorted[insertion - 1U];
            insertion--;
        }
        sorted[insertion] = value;
    }
    const uint8_t middle = state->sample_count / 2U;
    return (state->sample_count & 1U) != 0U ? sorted[middle] :
        (uint32_t)(((uint64_t)sorted[middle - 1U] + sorted[middle]) / 2U);
}

static void sample_history_recalculate(esp_bms_capacity_estimate_t *state)
{
    uint64_t total_mah = 0U;
    for (uint8_t index = 0U; index < state->sample_count; ++index) {
        total_mah += state->sample_history_mah[index];
    }
    state->estimate_mah = state->sample_count == 0U ? 0U :
        (uint32_t)(total_mah / state->sample_count);
    state->ready = state->sample_count >= ESP_BMS_CAPACITY_ESTIMATE_READY_SAMPLE_COUNT;
}

static bool sample_history_rejects(const esp_bms_capacity_estimate_t *state,
                                   uint32_t candidate_mah)
{
    if (state->sample_count < ESP_BMS_CAPACITY_ESTIMATE_READY_SAMPLE_COUNT) {
        return false;
    }
    const uint32_t median_mah = sample_history_median(state);
    if (median_mah == 0U) {
        return true;
    }
    const uint32_t difference_mah = candidate_mah > median_mah
                                        ? candidate_mah - median_mah
                                        : median_mah - candidate_mah;
    return (uint64_t)difference_mah * 100U >
           (uint64_t)median_mah * ESP_BMS_CAPACITY_ESTIMATE_OUTLIER_PERCENT;
}

static void sample_history_add(esp_bms_capacity_estimate_t *state, uint32_t sample_mah)
{
    state->sample_history_mah[state->next_sample_index] = sample_mah;
    if (state->sample_count < ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT) {
        state->sample_count++;
    }
    state->next_sample_index = (uint8_t)((state->next_sample_index + 1U) %
                                         ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT);
    sample_history_recalculate(state);
}

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

void esp_bms_capacity_integrator_reset(esp_bms_capacity_integrator_t *state,
                                       uint32_t total_cycle_mah)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->total_cycle_mah = total_cycle_mah;
    state->initialized = true;
}

void esp_bms_capacity_integrator_reset_anchor(esp_bms_capacity_integrator_t *state)
{
    if (state) {
        state->last_sample_valid = false;
    }
}

esp_bms_capacity_integrator_result_t esp_bms_capacity_integrator_observe(
    esp_bms_capacity_integrator_t *state,
    int64_t now_us,
    int16_t current_deci_amps)
{
    if (!state) {
        return ESP_BMS_CAPACITY_INTEGRATOR_NO_CHANGE;
    }
    if (!state->initialized) {
        esp_bms_capacity_integrator_reset(state, 0U);
    }
    if (!state->last_sample_valid) {
        state->last_sample_us = now_us;
        state->last_sample_valid = true;
        return ESP_BMS_CAPACITY_INTEGRATOR_REANCHORED;
    }

    const int64_t delta_us = now_us - state->last_sample_us;
    state->last_sample_us = now_us;
    if (delta_us <= 0 || delta_us > ESP_BMS_CAPACITY_INTEGRATOR_MAX_INTERVAL_US) {
        return ESP_BMS_CAPACITY_INTEGRATOR_DISCONTINUITY;
    }

    const int32_t current = current_deci_amps;
    const uint32_t magnitude = (uint32_t)(current < 0 ? -current : current);
    if (magnitude < ESP_BMS_CAPACITY_INTEGRATOR_DEADBAND_DECI_AMPS) {
        return ESP_BMS_CAPACITY_INTEGRATOR_NO_CHANGE;
    }

    const uint64_t numerator = (uint64_t)magnitude * (uint64_t)delta_us +
                               state->fractional_deci_amp_us;
    const uint64_t delta_mah = numerator / ESP_BMS_CAPACITY_INTEGRATOR_MAH_DIVISOR;
    state->fractional_deci_amp_us = numerator % ESP_BMS_CAPACITY_INTEGRATOR_MAH_DIVISOR;
    if (delta_mah > (uint64_t)(UINT32_MAX - state->total_cycle_mah)) {
        state->total_cycle_mah = 0U;
        state->fractional_deci_amp_us = 0U;
        return ESP_BMS_CAPACITY_INTEGRATOR_DISCONTINUITY;
    }
    state->total_cycle_mah += (uint32_t)delta_mah;
    return delta_mah == 0U ? ESP_BMS_CAPACITY_INTEGRATOR_NO_CHANGE :
                             ESP_BMS_CAPACITY_INTEGRATOR_UPDATED;
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
        if (state->sample_count > 0U && total_cycle_mah < state->last_accepted_cycle_mah) {
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
    if (sample_history_rejects(state, (uint32_t)sample_mah)) {
        set_anchor(state, total_cycle_mah, soc);
        return ESP_BMS_CAPACITY_ESTIMATE_REANCHORED;
    }
    sample_history_add(state, (uint32_t)sample_mah);
    state->last_accepted_cycle_mah = total_cycle_mah;
    set_anchor(state, total_cycle_mah, soc);
    return ESP_BMS_CAPACITY_ESTIMATE_UPDATED;
}
