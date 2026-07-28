#include "esp_bms_capacity_estimate.h"

#include <assert.h>

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

int main(void)
{
    esp_bms_capacity_estimate_t state = { 0 };

    assert(esp_bms_capacity_estimate_observe(&state, 100000U, 50U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    assert(esp_bms_capacity_estimate_observe(&state, 100000U, 20U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    assert(!state.ready);

    assert(esp_bms_capacity_estimate_observe(&state, 110000U, 40U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.ready && state.estimate_mah == 50000U);

    esp_bms_capacity_estimate_reset(&state);
    assert(esp_bms_capacity_estimate_observe(&state, 200000U, 80U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    assert(esp_bms_capacity_estimate_observe(&state, 209000U, 62U) ==
           ESP_BMS_CAPACITY_ESTIMATE_NO_CHANGE);
    assert(esp_bms_capacity_estimate_observe(&state, 210000U, 64U) ==
           ESP_BMS_CAPACITY_ESTIMATE_REANCHORED);
    assert(esp_bms_capacity_estimate_observe(&state, 220000U, 84U) ==
           ESP_BMS_CAPACITY_ESTIMATE_UPDATED);
    assert(state.estimate_mah == 50000U);

    assert(esp_bms_capacity_estimate_observe(&state, 1000U, 40U) ==
           ESP_BMS_CAPACITY_ESTIMATE_CLEARED);
    assert(!state.ready);
    test_current_integrator();
    return 0;
}
