#include "esp_bms_ota.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/task.h"

static const char *TAG = "esp_bms_ota";

#define OTA_BUFFER_SIZE 1024U
#define OTA_CODE_LEN 4U
#define OTA_RESTART_DELAY_MS 750U

static esp_err_t ota_send_text(httpd_req_t *req, const char *status, const char *text)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, X-Firmware-Code"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, status), TAG, "set HTTP status failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ota_send_json(httpd_req_t *req, const char *json)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, X-Firmware-Code"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true"), TAG, "set CORS failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool ota_read_code(httpd_req_t *req, char code[OTA_CODE_LEN + 1U])
{
    if (httpd_req_get_hdr_value_len(req, "X-Firmware-Code") != OTA_CODE_LEN ||
        httpd_req_get_hdr_value_str(req, "X-Firmware-Code", code, OTA_CODE_LEN + 1U) != ESP_OK) {
        return false;
    }
    for (size_t index = 0; index < OTA_CODE_LEN; ++index) {
        if (!isdigit((unsigned char)code[index])) {
            return false;
        }
    }
    return code[OTA_CODE_LEN] == '\0';
}

esp_err_t esp_bms_ota_handle_http_request(httpd_req_t *req)
{
    char expected_code[OTA_CODE_LEN + 1U] = { 0 };
    if (!ota_read_code(req, expected_code)) {
        return ota_send_text(req, "400 Bad Request", "invalid firmware code");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        return ota_send_text(req, "500 Internal Server Error", "OTA partition missing");
    }
    if (req->content_len == 0U) {
        return ota_send_text(req, "400 Bad Request", "firmware image is empty");
    }
    if (req->content_len > update_partition->size) {
        return ota_send_text(req, "413 Payload Too Large", "firmware image is too large");
    }

    uint8_t *buffer = heap_caps_malloc(OTA_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        return ota_send_text(req, "500 Internal Server Error", "OTA buffer allocation failed");
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (ret != ESP_OK) {
        heap_caps_free(buffer);
        ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(ret));
        return ota_send_text(req, "500 Internal Server Error", "OTA begin failed");
    }

    size_t remaining = req->content_len;
    size_t received_total = 0U;
    uint32_t crc = 0U;
    while (remaining > 0U) {
        const size_t requested = remaining < OTA_BUFFER_SIZE ? remaining : OTA_BUFFER_SIZE;
        const int received = httpd_req_recv(req, (char *)buffer, requested);
        if (received <= 0) {
            ret = received == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            break;
        }
        ret = esp_ota_write(ota_handle, buffer, (size_t)received);
        if (ret != ESP_OK) {
            break;
        }
        crc = esp_crc32_le(crc, buffer, (uint32_t)received);
        received_total += (size_t)received;
        remaining -= (size_t)received;
    }
    heap_caps_free(buffer);

    if (ret != ESP_OK || remaining != 0U) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_abort(ota_handle));
        ESP_LOGE(TAG, "receive/write failed after %u bytes: %s", (unsigned)received_total, esp_err_to_name(ret));
        return ota_send_text(req,
                             ret == ESP_ERR_TIMEOUT ? "408 Request Timeout" : "500 Internal Server Error",
                             "OTA receive failed");
    }

    char actual_code[OTA_CODE_LEN + 1U] = { 0 };
    (void)snprintf(actual_code, sizeof(actual_code), "%04u", (unsigned)(crc % 10000U));
    if (strcmp(actual_code, expected_code) != 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_abort(ota_handle));
        ESP_LOGW(TAG, "firmware code mismatch: bytes=%u", (unsigned)received_total);
        return ota_send_text(req, "403 Forbidden", "firmware code mismatch");
    }

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "image validation failed: %s", esp_err_to_name(ret));
        return ota_send_text(req, "422 Unprocessable Content", "firmware image is invalid");
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "boot partition update failed: %s", esp_err_to_name(ret));
        return ota_send_text(req, "500 Internal Server Error", "OTA activation failed");
    }

    ESP_LOGI(TAG, "image accepted: bytes=%u partition=%s", (unsigned)received_total, update_partition->label);
    ret = ota_send_json(req, "{\"status\":\"ready_to_reboot\"}");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "success response failed: %s", esp_err_to_name(ret));
        if (running_partition) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_set_boot_partition(running_partition));
        }
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;
}
