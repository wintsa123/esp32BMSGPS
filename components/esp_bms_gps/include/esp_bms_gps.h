#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_bms_lvgl_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_bms_idf_runtime esp_bms_idf_runtime_t;

esp_err_t esp_bms_gps_init(esp_bms_idf_runtime_t *runtime);
bool esp_bms_gps_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
void esp_bms_gps_stop(esp_bms_idf_runtime_t *runtime);
esp_bms_gps_module_state_t esp_bms_gps_module_state(const esp_bms_idf_runtime_t *runtime);
bool esp_bms_gps_finish_startup_probe(esp_bms_idf_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
