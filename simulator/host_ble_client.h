#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_bms_lvgl_ui.h"

typedef enum {
    ESP_BMS_HOST_BLE_SOURCE_BMS = 0,
    ESP_BMS_HOST_BLE_SOURCE_CONTROLLER = 1,
} esp_bms_host_ble_source_t;

typedef struct esp_bms_host_ble_client esp_bms_host_ble_client_t;

esp_bms_host_ble_client_t *esp_bms_host_ble_client_create(const char *endpoint);
void esp_bms_host_ble_client_destroy(esp_bms_host_ble_client_t *client);
bool esp_bms_host_ble_client_is_ready(const esp_bms_host_ble_client_t *client);
void esp_bms_host_ble_client_prepare_snapshot(esp_bms_host_ble_client_t *client,
                                              esp_bms_dashboard_snapshot_t *snapshot);
bool esp_bms_host_ble_client_start_scan(esp_bms_host_ble_client_t *client,
                                        esp_bms_host_ble_source_t source,
                                        esp_bms_dashboard_snapshot_t *snapshot);
bool esp_bms_host_ble_client_connect(esp_bms_host_ble_client_t *client,
                                     esp_bms_host_ble_source_t source,
                                     uint8_t bms_type,
                                     const char *mac,
                                     esp_bms_dashboard_snapshot_t *snapshot);
bool esp_bms_host_ble_client_disconnect(esp_bms_host_ble_client_t *client,
                                        esp_bms_host_ble_source_t source,
                                        esp_bms_dashboard_snapshot_t *snapshot);
bool esp_bms_host_ble_client_tick(esp_bms_host_ble_client_t *client,
                                  esp_bms_dashboard_snapshot_t *snapshot,
                                  uint32_t elapsed_ms);
