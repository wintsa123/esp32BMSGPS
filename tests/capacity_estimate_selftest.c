#include "esp_bms_capacity_estimate.h"

#include <assert.h>

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
    return 0;
}
