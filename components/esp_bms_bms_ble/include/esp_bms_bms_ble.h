#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_bms_idf_runtime esp_bms_idf_runtime_t;

esp_err_t esp_bms_bms_ble_init(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_bms_ble_start(esp_bms_idf_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
