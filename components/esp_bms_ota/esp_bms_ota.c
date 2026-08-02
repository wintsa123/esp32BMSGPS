#include "esp_bms_ota.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_bms_display_service.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "esp_bms_ota";

#define OTA_BUFFER_SIZE 1024U
#define OTA_CODE_LEN 4U
#define OTA_RESTART_DELAY_MS 750U
#define OTA_ERROR_VISIBLE_DELAY_MS 6000U
#define OTA_TASK_STACK_BYTES 8192U
#define OTA_TASK_PRIORITY 5U

typedef struct {
    httpd_req_t *req;
    const esp_partition_t *partition;
    uint32_t content_len;
    char expected_code[OTA_CODE_LEN + 1U];
} ota_task_arg_t;

typedef struct {
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
    esp_bms_ota_progress_t progress;
    bool task_running;
} ota_state_t;

static ota_state_t s_ota = { 0 };

static SemaphoreHandle_t ota_mutex(void)
{
    if (!s_ota.mutex) {
        static portMUX_TYPE s_ota_mutex_spinlock = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&s_ota_mutex_spinlock);
        if (!s_ota.mutex) {
            s_ota.mutex = xSemaphoreCreateMutexStatic(&s_ota.mutex_storage);
        }
        portEXIT_CRITICAL(&s_ota_mutex_spinlock);
    }
    return s_ota.mutex;
}

static const char *ota_state_text(esp_bms_ota_state_t state)
{
    switch (state) {
    case ESP_BMS_OTA_STATE_UPLOADING:
        return "UPLOADING";
    case ESP_BMS_OTA_STATE_VERIFYING:
        return "VERIFYING";
    case ESP_BMS_OTA_STATE_REBOOTING:
        return "REBOOTING";
    case ESP_BMS_OTA_STATE_ERROR:
        return "UPDATE FAILED";
    default:
        return "";
    }
}

static void ota_notify_display(esp_bms_ota_state_t state, uint8_t percent, const char *message)
{
    esp_bms_display_service_command_t command = { 0 };
    if (state == ESP_BMS_OTA_STATE_IDLE) {
        command.kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_FINISH;
    } else {
        command.kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_UPDATE;
        command.data.ota_update.progress_percent = percent;
        command.data.ota_update.failed = state == ESP_BMS_OTA_STATE_ERROR;
        (void)snprintf(command.data.ota_update.status_text,
                       sizeof(command.data.ota_update.status_text),
                       "%s",
                       message && message[0] != '\0' ? message : ota_state_text(state));
    }
    (void)esp_bms_display_service_submit_command(&command, 200U);
}

static void ota_progress_update(esp_bms_ota_state_t state,
                                uint8_t percent,
                                uint32_t received_bytes,
                                uint32_t total_bytes,
                                const char *message)
{
    SemaphoreHandle_t mutex = ota_mutex();
    if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        s_ota.progress.state = state;
        s_ota.progress.percent = percent > 100U ? 100U : percent;
        s_ota.progress.received_bytes = received_bytes;
        s_ota.progress.total_bytes = total_bytes;
        if (state == ESP_BMS_OTA_STATE_ERROR && message) {
            (void)snprintf(s_ota.progress.message,
                           sizeof(s_ota.progress.message),
                           "%s",
                           message);
        } else if (state != ESP_BMS_OTA_STATE_ERROR) {
            s_ota.progress.message[0] = '\0';
        }
        xSemaphoreGive(mutex);
    }
    ota_notify_display(state, percent, message);
}

static void ota_task_arg_free(ota_task_arg_t *arg)
{
    SemaphoreHandle_t mutex = ota_mutex();
    if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        s_ota.task_running = false;
        xSemaphoreGive(mutex);
    }
    if (arg) {
        heap_caps_free(arg);
    }
}

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

static void ota_task(void *context)
{
    ota_task_arg_t *arg = (ota_task_arg_t *)context;
    httpd_req_t *req = arg->req;

    uint8_t *buffer = heap_caps_malloc(OTA_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "buffer allocation failed");
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR, 0U, 0U, 0U, "OTA buffer allocation failed");
        (void)ota_send_text(req, "500 Internal Server Error", "OTA buffer allocation failed");
        goto ota_task_finish;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(arg->partition, arg->content_len, &ota_handle);
    if (ret != ESP_OK) {
        heap_caps_free(buffer);
        ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(ret));
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR, 0U, 0U, arg->content_len, "OTA begin failed");
        (void)ota_send_text(req, "500 Internal Server Error", "OTA begin failed");
        goto ota_task_finish;
    }

    ota_progress_update(ESP_BMS_OTA_STATE_UPLOADING, 0U, 0U, arg->content_len, NULL);

    size_t remaining = arg->content_len;
    size_t received_total = 0U;
    uint32_t crc = 0U;
    uint8_t last_percent = 0U;
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
        const uint8_t percent =
            (uint8_t)((received_total * 100U) / (arg->content_len > 0U ? arg->content_len : 1U));
        if (percent != last_percent) {
            last_percent = percent;
            ota_progress_update(ESP_BMS_OTA_STATE_UPLOADING,
                                percent,
                                (uint32_t)received_total,
                                arg->content_len,
                                NULL);
        }
    }
    heap_caps_free(buffer);

    if (ret != ESP_OK || remaining != 0U) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_abort(ota_handle));
        ESP_LOGE(TAG, "receive/write failed after %u bytes: %s",
                 (unsigned)received_total, esp_err_to_name(ret));
        const char *detail =
            ret == ESP_ERR_TIMEOUT ? "upload timed out" : "upload connection lost";
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR,
                            last_percent,
                            (uint32_t)received_total,
                            arg->content_len,
                            detail);
        (void)ota_send_text(req,
                            ret == ESP_ERR_TIMEOUT ? "408 Request Timeout"
                                                   : "500 Internal Server Error",
                            "OTA receive failed");
        goto ota_task_finish;
    }

    char actual_code[OTA_CODE_LEN + 1U] = { 0 };
    (void)snprintf(actual_code, sizeof(actual_code), "%04u", (unsigned)(crc % 10000U));
    if (strcmp(actual_code, arg->expected_code) != 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_abort(ota_handle));
        ESP_LOGW(TAG, "firmware code mismatch: bytes=%u", (unsigned)received_total);
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR,
                            last_percent,
                            (uint32_t)received_total,
                            arg->content_len,
                            "firmware code mismatch");
        (void)ota_send_text(req, "403 Forbidden", "firmware code mismatch");
        goto ota_task_finish;
    }

    ota_progress_update(ESP_BMS_OTA_STATE_VERIFYING,
                        100U,
                        (uint32_t)received_total,
                        arg->content_len,
                        NULL);

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "image validation failed: %s", esp_err_to_name(ret));
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR,
                            100U,
                            (uint32_t)received_total,
                            arg->content_len,
                            "firmware image is invalid");
        (void)ota_send_text(req, "422 Unprocessable Content", "firmware image is invalid");
        goto ota_task_finish;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    ret = esp_ota_set_boot_partition(arg->partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "boot partition update failed: %s", esp_err_to_name(ret));
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR,
                            100U,
                            (uint32_t)received_total,
                            arg->content_len,
                            "OTA activation failed");
        (void)ota_send_text(req, "500 Internal Server Error", "OTA activation failed");
        goto ota_task_finish;
    }

    ESP_LOGI(TAG, "image accepted: bytes=%u partition=%s",
             (unsigned)received_total, arg->partition->label);
    ota_progress_update(ESP_BMS_OTA_STATE_REBOOTING,
                        100U,
                        (uint32_t)received_total,
                        arg->content_len,
                        NULL);
    ret = ota_send_json(req, "{\"status\":\"ready_to_reboot\"}");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "success response failed: %s", esp_err_to_name(ret));
        if (running_partition) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_set_boot_partition(running_partition));
        }
        ota_progress_update(ESP_BMS_OTA_STATE_ERROR,
                            100U,
                            (uint32_t)received_total,
                            arg->content_len,
                            "OTA response failed");
        goto ota_task_finish;
    }

    (void)httpd_req_async_handler_complete(req);
    ota_task_arg_free(arg);
    vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
    esp_restart();
    vTaskDelete(NULL);
    return;

ota_task_finish:
    (void)httpd_req_async_handler_complete(req);
    vTaskDelay(pdMS_TO_TICKS(OTA_ERROR_VISIBLE_DELAY_MS));
    ota_progress_update(ESP_BMS_OTA_STATE_IDLE, 0U, 0U, 0U, "");
    ota_task_arg_free(arg);
    vTaskDelete(NULL);
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

    SemaphoreHandle_t mutex = ota_mutex();
    bool busy = false;
    if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        busy = s_ota.task_running || s_ota.progress.state != ESP_BMS_OTA_STATE_IDLE;
        if (!busy) {
            s_ota.task_running = true;
        }
        xSemaphoreGive(mutex);
    }
    if (busy) {
        ESP_LOGW(TAG, "rejected concurrent OTA request");
        return ota_send_text(req, "409 Conflict", "OTA already in progress");
    }

    httpd_req_t *async_req = NULL;
    const esp_err_t begin_ret = httpd_req_async_handler_begin(req, &async_req);
    if (begin_ret != ESP_OK) {
        ESP_LOGE(TAG, "async begin failed: %s", esp_err_to_name(begin_ret));
        ota_task_arg_free(NULL);
        return ota_send_text(req, "500 Internal Server Error", "OTA request setup failed");
    }

    ota_task_arg_t *arg = heap_caps_malloc(sizeof(*arg), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!arg) {
        ota_task_arg_free(NULL);
        const esp_err_t send_ret =
            ota_send_text(async_req, "500 Internal Server Error", "OTA buffer allocation failed");
        (void)httpd_req_async_handler_complete(async_req);
        return send_ret;
    }
    arg->req = async_req;
    arg->partition = update_partition;
    arg->content_len = req->content_len;
    (void)memcpy(arg->expected_code, expected_code, sizeof(arg->expected_code));

    if (xTaskCreate(ota_task, "ota_upload", OTA_TASK_STACK_BYTES, arg, OTA_TASK_PRIORITY, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
        const esp_err_t send_ret =
            ota_send_text(async_req, "500 Internal Server Error", "OTA task creation failed");
        (void)httpd_req_async_handler_complete(async_req);
        ota_task_arg_free(arg);
        return send_ret;
    }

    ESP_LOGI(TAG, "OTA accepted: bytes=%u partition=%s",
             (unsigned)req->content_len, update_partition->label);
    return ESP_OK;
}

esp_err_t esp_bms_ota_handle_progress_request(httpd_req_t *req)
{
    esp_bms_ota_progress_t progress = { 0 };
    (void)esp_bms_ota_get_progress(&progress);

    char json[192] = { 0 };
    (void)snprintf(json,
                   sizeof(json),
                   "{\"active\":%s,\"state\":\"%s\",\"percent\":%u,"
                   "\"received_bytes\":%u,\"total_bytes\":%u,\"message\":\"%s\"}",
                   progress.state == ESP_BMS_OTA_STATE_IDLE ? "false" : "true",
                   ota_state_text(progress.state),
                   (unsigned)progress.percent,
                   (unsigned)progress.received_bytes,
                   (unsigned)progress.total_bytes,
                   progress.message);
    return ota_send_json(req, json);
}

esp_err_t esp_bms_ota_get_progress(esp_bms_ota_progress_t *progress)
{
    if (!progress) {
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t mutex = ota_mutex();
    if (!mutex || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    *progress = s_ota.progress;
    xSemaphoreGive(mutex);
    return ESP_OK;
}
