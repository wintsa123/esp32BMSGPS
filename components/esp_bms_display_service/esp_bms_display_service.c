#include "esp_bms_display_service.h"

#include <limits.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "bms_display";

#define DISPLAY_SERVICE_TASK_STACK_BYTES 8192U
#define DISPLAY_SERVICE_TASK_PRIORITY 6U
#define DISPLAY_SERVICE_COMMAND_QUEUE_LENGTH 12U
#define DISPLAY_SERVICE_ACTION_QUEUE_LENGTH 16U
#define DISPLAY_SERVICE_START_TIMEOUT_MS 5000U
#define DISPLAY_SERVICE_LOOP_MAX_DELAY_MS 5U
#define DISPLAY_SERVICE_DRAG_FRAME_SAMPLE_CAPACITY 256U

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define DISPLAY_SERVICE_TASK_CORE 1
#else
#define DISPLAY_SERVICE_TASK_CORE 0
#endif

typedef struct {
    esp_bms_dashboard_snapshot_t snapshot;
    int64_t queued_us;
} snapshot_item_t;

typedef struct {
    esp_bms_display_service_command_t command;
    uint32_t token;
    int64_t queued_us;
} command_item_t;

typedef struct {
    uint32_t token;
    esp_err_t result;
} command_result_t;

typedef struct {
    uint32_t frame_samples[DISPLAY_SERVICE_DRAG_FRAME_SAMPLE_CAPACITY];
    uint32_t frame_count;
    uint32_t sample_state;
    uint16_t frame_sample_count;
    uint32_t command_queue_count;
    uint64_t command_queue_total_us;
    uint32_t command_queue_max_us;
    uint32_t snapshot_queue_count;
    uint64_t snapshot_queue_total_us;
    uint32_t snapshot_queue_max_us;
    size_t minimum_psram_free;
    size_t minimum_dma_free;
    bool active;
} drag_perf_t;

typedef struct {
    esp_bms_lvgl_bridge_config_t config;
    esp_bms_dashboard_snapshot_t initial_snapshot;
    esp_bms_dashboard_snapshot_t last_snapshot;
    uint8_t initial_brightness_percent;
    QueueHandle_t snapshot_queue;
    QueueHandle_t command_queue;
    QueueHandle_t action_queue;
    QueueHandle_t result_queue;
    SemaphoreHandle_t command_mutex;
    SemaphoreHandle_t ready_sem;
    StaticQueue_t snapshot_queue_storage;
    StaticQueue_t command_queue_storage;
    StaticQueue_t action_queue_storage;
    StaticQueue_t result_queue_storage;
    StaticSemaphore_t command_mutex_storage;
    StaticSemaphore_t ready_sem_storage;
    uint8_t snapshot_queue_buffer[sizeof(snapshot_item_t)];
    uint8_t command_queue_buffer[DISPLAY_SERVICE_COMMAND_QUEUE_LENGTH * sizeof(command_item_t)];
    uint8_t action_queue_buffer[DISPLAY_SERVICE_ACTION_QUEUE_LENGTH * sizeof(esp_bms_lvgl_action_event_t)];
    uint8_t result_queue_buffer[sizeof(command_result_t)];
    StaticTask_t task_storage;
    StackType_t task_stack[DISPLAY_SERVICE_TASK_STACK_BYTES / sizeof(StackType_t)];
    TaskHandle_t task;
    esp_err_t init_result;
    uint32_t next_command_token;
    esp_bms_lvgl_data_source_t stable_data_source;
    drag_perf_t drag_perf;
    bool ready;
    bool running;
} display_service_t;

static display_service_t s_service;

static uint32_t elapsed_us_since(int64_t started_us)
{
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    if (elapsed_us <= 0) {
        return 0U;
    }
    return elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
}

static void drag_perf_sample_memory(void)
{
    drag_perf_t *perf = &s_service.drag_perf;
    if (!perf->active) {
        return;
    }
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (psram_free < perf->minimum_psram_free) {
        perf->minimum_psram_free = psram_free;
    }
    if (dma_free < perf->minimum_dma_free) {
        perf->minimum_dma_free = dma_free;
    }
}

static void drag_perf_start(void)
{
    drag_perf_t *perf = &s_service.drag_perf;
    memset(perf, 0, sizeof(*perf));
    perf->active = true;
    perf->sample_state = UINT32_C(0x9e3779b9);
    perf->minimum_psram_free = (size_t)-1;
    perf->minimum_dma_free = (size_t)-1;
    esp_bms_lvgl_bridge_reset_metrics();
    drag_perf_sample_memory();
}

static void drag_perf_record_frame(uint32_t elapsed_us)
{
    drag_perf_t *perf = &s_service.drag_perf;
    if (!perf->active) {
        return;
    }

    ++perf->frame_count;
    if (perf->frame_sample_count < DISPLAY_SERVICE_DRAG_FRAME_SAMPLE_CAPACITY) {
        perf->frame_samples[perf->frame_sample_count++] = elapsed_us;
        return;
    }

    perf->sample_state = perf->sample_state * UINT32_C(1664525) + UINT32_C(1013904223);
    const uint32_t sample_slot = perf->sample_state % perf->frame_count;
    if (sample_slot < DISPLAY_SERVICE_DRAG_FRAME_SAMPLE_CAPACITY) {
        perf->frame_samples[sample_slot] = elapsed_us;
    }
}

static void drag_perf_record_queue_delay(bool command, int64_t queued_us)
{
    drag_perf_t *perf = &s_service.drag_perf;
    if (!perf->active) {
        return;
    }
    const uint32_t elapsed_us = elapsed_us_since(queued_us);
    uint32_t *count = command ? &perf->command_queue_count : &perf->snapshot_queue_count;
    uint64_t *total_us = command ? &perf->command_queue_total_us : &perf->snapshot_queue_total_us;
    uint32_t *max_us = command ? &perf->command_queue_max_us : &perf->snapshot_queue_max_us;
    ++*count;
    *total_us += elapsed_us;
    if (elapsed_us > *max_us) {
        *max_us = elapsed_us;
    }
}

static uint32_t drag_perf_p95_us(drag_perf_t *perf)
{
    const uint16_t count = perf->frame_sample_count;
    if (count == 0U) {
        return 0U;
    }
    for (uint16_t index = 1U; index < count; ++index) {
        const uint32_t value = perf->frame_samples[index];
        uint16_t cursor = index;
        while (cursor > 0U && perf->frame_samples[cursor - 1U] > value) {
            perf->frame_samples[cursor] = perf->frame_samples[cursor - 1U];
            --cursor;
        }
        perf->frame_samples[cursor] = value;
    }
    const uint16_t p95_index =
        (uint16_t)((((uint32_t)count * 95U + 99U) / 100U) - 1U);
    return perf->frame_samples[p95_index];
}

static void drag_perf_finish(void)
{
    drag_perf_t *perf = &s_service.drag_perf;
    esp_bms_lvgl_bridge_metrics_t metrics = { 0 };
    esp_bms_lvgl_bridge_get_metrics(&metrics);
    const uint32_t p95_us = drag_perf_p95_us(perf);
    const uint32_t command_queue_average_us = perf->command_queue_count == 0U
                                                  ? 0U
                                                  : (uint32_t)(perf->command_queue_total_us /
                                                               perf->command_queue_count);
    const uint32_t snapshot_queue_average_us = perf->snapshot_queue_count == 0U
                                                   ? 0U
                                                   : (uint32_t)(perf->snapshot_queue_total_us /
                                                                perf->snapshot_queue_count);
    const size_t minimum_psram_free = perf->minimum_psram_free == (size_t)-1
                                          ? 0U
                                          : perf->minimum_psram_free;
    const size_t minimum_dma_free = perf->minimum_dma_free == (size_t)-1
                                        ? 0U
                                        : perf->minimum_dma_free;

    ESP_LOGI(TAG,
             "drag_perf frames=%u samples=%u p95_us=%u render_cnt=%u render_us=%llu "
             "render_max_us=%u flush_cnt=%u flush_us=%llu flush_max_us=%u "
             "dma_wait_cnt=%u dma_wait_us=%llu dma_wait_max_us=%u inv_areas=%u inv_px=%llu "
             "cmd_q_cnt=%u cmd_q_avg_us=%u cmd_q_max_us=%u snap_q_cnt=%u snap_q_avg_us=%u "
             "snap_q_max_us=%u psram_min=%u dma_min=%u",
             (unsigned)perf->frame_count,
             (unsigned)perf->frame_sample_count,
             (unsigned)p95_us,
             (unsigned)metrics.render_count,
             (unsigned long long)metrics.render_total_us,
             (unsigned)metrics.render_max_us,
             (unsigned)metrics.flush_count,
             (unsigned long long)metrics.flush_total_us,
             (unsigned)metrics.flush_max_us,
             (unsigned)metrics.flush_wait_count,
             (unsigned long long)metrics.flush_wait_total_us,
             (unsigned)metrics.flush_wait_max_us,
             (unsigned)metrics.invalidated_area_count,
             (unsigned long long)metrics.invalidated_pixel_count,
             (unsigned)perf->command_queue_count,
             (unsigned)command_queue_average_us,
             (unsigned)perf->command_queue_max_us,
             (unsigned)perf->snapshot_queue_count,
             (unsigned)snapshot_queue_average_us,
             (unsigned)perf->snapshot_queue_max_us,
             (unsigned)minimum_psram_free,
             (unsigned)minimum_dma_free);
    memset(perf, 0, sizeof(*perf));
}

static TickType_t timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == UINT32_MAX) {
        return portMAX_DELAY;
    }
    if (timeout_ms == 0U) {
        return 0;
    }
    const TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return ticks == 0U ? 1U : ticks;
}

static esp_err_t service_process_touch_calibration(const esp_bms_lvgl_action_event_t *event)
{
    switch (event->action) {
    case ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION:
        {
            const esp_err_t ret = esp_bms_lvgl_bridge_begin_touch_calibration();
            if (ret != ESP_OK) {
                (void)esp_bms_lvgl_ui_touch_calibration_result(false);
            }
            return ret;
        }
    case ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE:
        {
            bool finished = false;
            const esp_err_t ret = esp_bms_lvgl_bridge_add_touch_calibration_sample(
                event->touch_target_index,
                event->touch_observed_x,
                event->touch_observed_y,
                event->touch_target_x,
                event->touch_target_y,
                &finished);
            if (ret != ESP_OK || finished) {
                (void)esp_bms_lvgl_ui_touch_calibration_result(ret == ESP_OK && finished);
            }
            return ret;
        }
    case ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION:
        esp_bms_lvgl_bridge_cancel_touch_calibration();
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t service_process_command(const esp_bms_display_service_command_t *command)
{
    switch (command->kind) {
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_UPDATE:
        return esp_bms_lvgl_ui_boot_update(command->data.boot_update.progress_percent,
                                           command->data.boot_update.status_text);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_FINISH:
        s_service.last_snapshot = command->data.boot_finish.snapshot;
        return esp_bms_lvgl_ui_boot_finish(&s_service.last_snapshot);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_BRIGHTNESS:
        return esp_bms_lvgl_bridge_set_brightness(command->data.brightness.percent);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_ROTATION:
        {
            esp_err_t ret = esp_bms_lvgl_bridge_set_rotation(command->data.rotation.rotation);
            if (ret == ESP_OK) {
                ret = esp_bms_lvgl_ui_update(&s_service.last_snapshot);
            }
            return ret;
        }
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_SHOW_DASHBOARD:
        return esp_bms_lvgl_ui_show_dashboard();
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_PAGE:
        return esp_bms_lvgl_ui_set_page(command->data.page.page, command->data.page.animated);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_TOUCH_CALIBRATION_RESULT:
        return esp_bms_lvgl_ui_touch_calibration_result(
            command->data.touch_calibration_result.success);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_RESET_TOUCH_CALIBRATION:
        return esp_bms_lvgl_bridge_reset_touch_calibration();
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_WRITE_RGB565:
        return esp_bms_lvgl_bridge_write_rgb565(command->data.rgb565.x,
                                                 command->data.rgb565.y,
                                                 command->data.rgb565.width,
                                                 command->data.rgb565.height,
                                                 command->data.rgb565.pixels,
                                                 command->data.rgb565.pixel_bytes);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_UPDATE:
        return esp_bms_lvgl_ui_ota_update(command->data.ota_update.progress_percent,
                                          command->data.ota_update.status_text,
                                          command->data.ota_update.failed);
    case ESP_BMS_DISPLAY_SERVICE_COMMAND_OTA_FINISH:
        return esp_bms_lvgl_ui_ota_finish();
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static void service_process_commands(void)
{
    command_item_t item = { 0 };
    while (xQueueReceive(s_service.command_queue, &item, 0) == pdTRUE) {
        drag_perf_record_queue_delay(true, item.queued_us);
        const esp_err_t result = service_process_command(&item.command);
        const command_result_t response = {
            .token = item.token,
            .result = result,
        };
        (void)xQueueOverwrite(s_service.result_queue, &response);
        if (result != ESP_OK) {
            ESP_LOGW(TAG,
                     "command=%u queue_us=%lld result=%s",
                     (unsigned)item.command.kind,
                     (long long)(esp_timer_get_time() - item.queued_us),
                     esp_err_to_name(result));
        }
    }
}

static void service_process_snapshot(void)
{
    snapshot_item_t item = { 0 };
    if (xQueueReceive(s_service.snapshot_queue, &item, 0) != pdTRUE) {
        return;
    }

    drag_perf_record_queue_delay(false, item.queued_us);
    s_service.last_snapshot = item.snapshot;
    const esp_err_t ret = esp_bms_lvgl_ui_update(&s_service.last_snapshot);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "snapshot queue_us=%lld result=%s",
                 (long long)(esp_timer_get_time() - item.queued_us), esp_err_to_name(ret));
    }
}

static void service_process_actions(void)
{
    esp_bms_lvgl_action_event_t event = { 0 };
    if (esp_bms_lvgl_ui_take_action_event(&event) != ESP_OK ||
        event.action == ESP_BMS_LVGL_ACTION_NONE) {
        return;
    }

    if (event.action == ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION ||
        event.action == ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE ||
        event.action == ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION) {
        const esp_err_t ret = service_process_touch_calibration(&event);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "touch calibration action=%u result=%s",
                     (unsigned)event.action, esp_err_to_name(ret));
        }
        return;
    }

    if (xQueueSend(s_service.action_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "action queue full; dropping action=%u", (unsigned)event.action);
    }
}

static void display_service_task(void *arg)
{
    (void)arg;
    esp_err_t ret = esp_bms_lvgl_bridge_init(&s_service.config);
    if (ret == ESP_OK) {
        ret = esp_bms_lvgl_bridge_set_brightness(s_service.initial_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = esp_bms_lvgl_bridge_start();
    }
    if (ret == ESP_OK) {
        if (esp_bms_lvgl_bridge_touch_calibration_supported()) {
            const esp_err_t calibration_ret = esp_bms_lvgl_bridge_load_touch_calibration();
            if (calibration_ret != ESP_OK) {
                ESP_LOGW(TAG, "touch calibration load failed: %s", esp_err_to_name(calibration_ret));
            }
        }
        ret = esp_bms_lvgl_bridge_lock(-1);
    }
    if (ret == ESP_OK) {
        ret = esp_bms_lvgl_ui_init(
            esp_bms_lvgl_bridge_get_display(),
            esp_bms_lvgl_bridge_touch_calibration_supported(),
            esp_bms_lvgl_bridge_native_gestures_supported());
        if (ret == ESP_OK) {
            s_service.last_snapshot = s_service.initial_snapshot;
            ret = esp_bms_lvgl_ui_boot_start(&s_service.last_snapshot);
        }
        esp_bms_lvgl_bridge_unlock();
    }

    s_service.init_result = ret;
    s_service.running = ret == ESP_OK;
    s_service.ready = true;
    (void)xSemaphoreGive(s_service.ready_sem);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display service startup failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    while (s_service.running) {
        uint32_t delay_ms = DISPLAY_SERVICE_LOOP_MAX_DELAY_MS;
        if (esp_bms_lvgl_bridge_lock(-1) == ESP_OK) {
            if (esp_bms_lvgl_ui_drag_active() && !s_service.drag_perf.active) {
                drag_perf_start();
            }
            service_process_commands();
            service_process_snapshot();
            service_process_actions();
            __atomic_store_n(&s_service.stable_data_source,
                             esp_bms_lvgl_ui_stable_data_source(),
                             __ATOMIC_RELAXED);
            const int64_t timer_started_us = esp_timer_get_time();
            delay_ms = lv_timer_handler();
            const uint32_t timer_elapsed_us = elapsed_us_since(timer_started_us);
            esp_bms_lvgl_native_gesture_t gesture = ESP_BMS_LVGL_NATIVE_GESTURE_NONE;
            if (esp_bms_lvgl_bridge_take_native_gesture(&gesture)) {
                const esp_err_t gesture_ret = esp_bms_lvgl_ui_handle_native_gesture(gesture);
                if (gesture_ret != ESP_OK) {
                    ESP_LOGW(TAG, "native gesture=%u failed: %s",
                             (unsigned)gesture,
                             esp_err_to_name(gesture_ret));
                }
            }
            const bool drag_active = esp_bms_lvgl_ui_drag_active();
            if (drag_active && !s_service.drag_perf.active) {
                drag_perf_start();
            }
            if (s_service.drag_perf.active) {
                drag_perf_record_frame(timer_elapsed_us);
                drag_perf_sample_memory();
                if (!drag_active) {
                    drag_perf_finish();
                }
            }
            esp_bms_lvgl_bridge_unlock();
        }
        if (delay_ms > DISPLAY_SERVICE_LOOP_MAX_DELAY_MS) {
            delay_ms = DISPLAY_SERVICE_LOOP_MAX_DELAY_MS;
        }
        if (delay_ms == 0U) {
            delay_ms = 1U;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    vTaskDelete(NULL);
}

esp_err_t esp_bms_display_service_start(const esp_bms_lvgl_bridge_config_t *config,
                                        uint8_t brightness_percent,
                                        const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(config && snapshot, ESP_ERR_INVALID_ARG, TAG,
                        "display config and snapshot are required");
    ESP_RETURN_ON_FALSE(!s_service.task && !s_service.ready, ESP_ERR_INVALID_STATE, TAG,
                        "display service already started");

    memset(&s_service, 0, sizeof(s_service));
    s_service.config = *config;
    s_service.initial_snapshot = *snapshot;
    s_service.initial_brightness_percent = brightness_percent;
    s_service.snapshot_queue = xQueueCreateStatic(1,
                                                   sizeof(snapshot_item_t),
                                                   s_service.snapshot_queue_buffer,
                                                   &s_service.snapshot_queue_storage);
    s_service.command_queue = xQueueCreateStatic(DISPLAY_SERVICE_COMMAND_QUEUE_LENGTH,
                                                  sizeof(command_item_t),
                                                  s_service.command_queue_buffer,
                                                  &s_service.command_queue_storage);
    s_service.action_queue = xQueueCreateStatic(DISPLAY_SERVICE_ACTION_QUEUE_LENGTH,
                                                 sizeof(esp_bms_lvgl_action_event_t),
                                                 s_service.action_queue_buffer,
                                                 &s_service.action_queue_storage);
    s_service.result_queue = xQueueCreateStatic(1,
                                                 sizeof(command_result_t),
                                                 s_service.result_queue_buffer,
                                                 &s_service.result_queue_storage);
    s_service.command_mutex = xSemaphoreCreateMutexStatic(&s_service.command_mutex_storage);
    s_service.ready_sem = xSemaphoreCreateBinaryStatic(&s_service.ready_sem_storage);
    ESP_RETURN_ON_FALSE(s_service.snapshot_queue && s_service.command_queue &&
                            s_service.action_queue && s_service.result_queue &&
                            s_service.command_mutex && s_service.ready_sem,
                        ESP_ERR_NO_MEM, TAG, "create display service queues failed");

    s_service.task = xTaskCreateStaticPinnedToCore(display_service_task,
                                                    "bms_display",
                                                    DISPLAY_SERVICE_TASK_STACK_BYTES / sizeof(StackType_t),
                                                    NULL,
                                                    DISPLAY_SERVICE_TASK_PRIORITY,
                                                    s_service.task_stack,
                                                    &s_service.task_storage,
                                                    DISPLAY_SERVICE_TASK_CORE);
    ESP_RETURN_ON_FALSE(s_service.task, ESP_ERR_NO_MEM, TAG, "create display service task failed");
    if (xSemaphoreTake(s_service.ready_sem, pdMS_TO_TICKS(DISPLAY_SERVICE_START_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return s_service.init_result;
}

esp_err_t esp_bms_display_service_publish_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(s_service.running, ESP_ERR_INVALID_STATE, TAG,
                        "display service is not running");
    const snapshot_item_t item = {
        .snapshot = *snapshot,
        .queued_us = esp_timer_get_time(),
    };
    return xQueueOverwrite(s_service.snapshot_queue, &item) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_bms_display_service_submit_command(
    const esp_bms_display_service_command_t *command,
    uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(command, ESP_ERR_INVALID_ARG, TAG, "command is required");
    ESP_RETURN_ON_FALSE(s_service.running, ESP_ERR_INVALID_STATE, TAG,
                        "display service is not running");
    const TickType_t timeout = timeout_to_ticks(timeout_ms);
    if (xSemaphoreTake(s_service.command_mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const command_item_t item = {
        .command = *command,
        .token = ++s_service.next_command_token,
        .queued_us = esp_timer_get_time(),
    };
    esp_err_t ret = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_service.command_queue, &item, timeout) == pdTRUE) {
        command_result_t response = { 0 };
        while (xQueueReceive(s_service.result_queue, &response, timeout) == pdTRUE) {
            if (response.token == item.token) {
                ret = response.result;
                break;
            }
        }
    }
    xSemaphoreGive(s_service.command_mutex);
    return ret;
}

esp_err_t esp_bms_display_service_take_action_event(esp_bms_lvgl_action_event_t *event)
{
    ESP_RETURN_ON_FALSE(event, ESP_ERR_INVALID_ARG, TAG, "action event output is required");
    ESP_RETURN_ON_FALSE(s_service.running, ESP_ERR_INVALID_STATE, TAG,
                        "display service is not running");
    memset(event, 0, sizeof(*event));
    (void)xQueueReceive(s_service.action_queue, event, 0);
    return ESP_OK;
}

esp_bms_lvgl_data_source_t esp_bms_display_service_stable_data_source(void)
{
    return __atomic_load_n(&s_service.stable_data_source, __ATOMIC_RELAXED);
}

bool esp_bms_display_service_speed_dashboard_style_available(esp_bms_speed_dashboard_style_t style)
{
    return esp_bms_lvgl_ui_speed_dashboard_style_available(style);
}

esp_bms_speed_dashboard_style_t esp_bms_display_service_default_speed_dashboard_style(void)
{
    return esp_bms_lvgl_ui_default_speed_dashboard_style();
}
