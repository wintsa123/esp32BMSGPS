#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_BMS_OTA_STATE_IDLE = 0,
    ESP_BMS_OTA_STATE_UPLOADING,
    ESP_BMS_OTA_STATE_VERIFYING,
    ESP_BMS_OTA_STATE_REBOOTING,
    ESP_BMS_OTA_STATE_ERROR,
} esp_bms_ota_state_t;

#define ESP_BMS_OTA_MESSAGE_MAX 64U

typedef struct {
    esp_bms_ota_state_t state;
    uint8_t percent;
    uint32_t received_bytes;
    uint32_t total_bytes;
    char message[ESP_BMS_OTA_MESSAGE_MAX];
} esp_bms_ota_progress_t;

esp_err_t esp_bms_ota_handle_http_request(httpd_req_t *req);
esp_err_t esp_bms_ota_handle_progress_request(httpd_req_t *req);
esp_err_t esp_bms_ota_get_progress(esp_bms_ota_progress_t *progress);

#ifdef __cplusplus
}
#endif
