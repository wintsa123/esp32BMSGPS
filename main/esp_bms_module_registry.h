#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct esp_bms_idf_runtime esp_bms_idf_runtime_t;

esp_err_t esp_bms_module_registry_init(esp_bms_idf_runtime_t *runtime);
esp_err_t esp_bms_module_registry_start(esp_bms_idf_runtime_t *runtime);
bool esp_bms_module_registry_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms);
void esp_bms_module_registry_stop(esp_bms_idf_runtime_t *runtime);
bool esp_bms_module_registry_gps_enabled(void);
bool esp_bms_module_registry_gps_finish_startup_probe(esp_bms_idf_runtime_t *runtime);
bool esp_bms_module_registry_gps_is_available(const esp_bms_idf_runtime_t *runtime);
void esp_bms_module_registry_play_connection_audio(uint8_t events, uint8_t volume_percent);
void esp_bms_module_registry_play_volume_audio(uint8_t volume_percent);
