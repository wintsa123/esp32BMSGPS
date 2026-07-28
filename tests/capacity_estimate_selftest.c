#include "esp_bms_capacity_estimate.h"

#include <assert.h>

static esp_bms_capacity_estimate_result_t add_capacity_sample(
    esp_bms_capacity_estimate_t *state,
    uint32_t *total_cycle_mah,
    uint32_t sample_mah)
{
    esp_bms_capacity_estimate_reset_anchor(state);
    assert(esp_bms_capacity_estimate_observe(state, *total_cycle_mah, 50U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    *total_cycle_mah += sample_mah / 5U;
    return esp_bms_capacity_estimate_observe(state, *total_cycle_mah, 70U);
}

static void test_current_integrator(void)
{
    esp_bms_capacity_integrator_t state = { 0 };
    esp_bms_capacity_integrator_reset(&state, 50000U);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(1000000), 100) ==
           ESP_BMS_CAPACITY_INTEGRATOR_REANCHORED);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(3000000), 100) ==
           ESP_BMS_CAPACITY_INTEGRATOR_UPDATED);
    assert(state.total_cycle_mah == 50005U);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(5000000), -100) ==
           ESP_BMS_CAPACITY_INTEGRATOR_UPDATED);
    assert(state.total_cycle_mah == 50011U);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(7000000), 4) ==
           ESP_BMS_CAPACITY_INTEGRATOR_NO_CHANGE);
    assert(state.total_cycle_mah == 50011U);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(11000000), 100) ==
           ESP_BMS_CAPACITY_INTEGRATOR_DISCONTINUITY);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(13000000), 100) ==
           ESP_BMS_CAPACITY_INTEGRATOR_UPDATED);
    assert(state.total_cycle_mah == 50016U);

    esp_bms_capacity_integrator_reset(&state, 0U);
    assert(esp_bms_capacity_integrator_observe(&state, 0, INT16_MIN) ==
           ESP_BMS_CAPACITY_INTEGRATOR_REANCHORED);
    assert(esp_bms_capacity_integrator_observe(&state, INT64_C(2000000), INT16_MIN) ==
           ESP_BMS_CAPACITY_INTEGRATOR_UPDATED);
    assert(state.total_cycle_mah == 1820U);
}

static void test_capacity_history(void)
{
    esp_bms_capacity_estimate_t state = { 0 };
    uint32_t total_cycle_mah = 100000U;

    assert(add_capacity_sample(&state, &total_cycle_mah, 50000U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.sample_count == 1U && !state.ready && state.estimate_mah == 50000U);
    assert(add_capacity_sample(&state, &total_cycle_mah, 50000U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.sample_count == 2U && !state.ready);
    assert(add_capacity_sample(&state, &total_cycle_mah, 50000U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.sample_count == 3U && state.ready && state.estimate_mah == 50000U);

    assert(add_capacity_sample(&state, &total_cycle_mah, 80000U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    assert(state.sample_count == 3U && state.estimate_mah == 50000U);

    for (uint8_t index = 0U; index < 5U; ++index) {
        assert(add_capacity_sample(&state, &total_cycle_mah, 60000U) ==
               ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    }
    assert(state.sample_count == ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT);
    assert(state.estimate_mah == 56250U);

    assert(add_capacity_sample(&state, &total_cycle_mah, 55000U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.sample_count == ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT);
    assert(state.estimate_mah == 56875U);

    assert(esp_bms_capacity_estimate_observe(&state, 1000U, 40U) ==
           ESP_BMS_CAPACITY_ESTIMATE_CLEARED);
    assert(!state.ready && state.sample_count == 0U);
}

int main(void)
{
    test_capacity_history();
    test_current_integrator();
    return 0;
}
