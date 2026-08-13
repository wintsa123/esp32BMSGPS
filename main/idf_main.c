#include "esp_bms_module_registry.h"
#include "esp_bms_display_service.h"
#include "esp_bms_idf_runtime.h"
#include "esp_bms_profile_hardware.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bms_idf_main";

#define MAIN_LOOP_TASK_PRIORITY 4U
#define MAIN_LOOP_PERIOD_MS 50U
#define BOOT_READY_HOLD_MS 80U
#define SETUP_AP_IDLE_TIMEOUT_MS (5U * 60U * 1000U)
#define SETUP_AP_START_GUARD_MS 500U
#define RESOURCE_DIAGNOSTICS_WINDOW_MS 1000U
#define RESOURCE_DIAGNOSTICS_MAX_TASKS 48U
#define RESOURCE_DIAGNOSTICS_TOP_TASKS 3U

typedef struct {
    const char *action_name;
    int64_t started_us;
    configRUN_TIME_COUNTER_TYPE total_runtime;
    UBaseType_t task_count;
    bool heap_monitoring;
    bool active;
    /* ponytail: tracks at most 48 tasks; use a heap-backed snapshot only if a profile exceeds this count. */
    TaskStatus_t before[RESOURCE_DIAGNOSTICS_MAX_TASKS];
    TaskStatus_t after[RESOURCE_DIAGNOSTICS_MAX_TASKS];
} resource_diagnostics_t;

typedef struct {
    const char *name;
    uint64_t runtime;
} resource_diagnostics_task_usage_t;

static resource_diagnostics_t s_resource_diagnostics;

typedef enum {
    SETUP_SERVICE_START_IDLE = 0,
    SETUP_SERVICE_START_AP,
    SETUP_SERVICE_START_HTTP,
} setup_service_start_stage_t;

static esp_bms_display_rotation_t bridge_rotation_from_runtime(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return ESP_BMS_DISPLAY_ROTATION_PORTRAIT;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
    default:
        return ESP_BMS_DISPLAY_ROTATION_LANDSCAPE;
    }
}

static esp_bms_idf_display_rotation_t runtime_rotation_from_bridge(esp_bms_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_DISPLAY_ROTATION_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
    case ESP_BMS_DISPLAY_ROTATION_LANDSCAPE:
    default:
        return ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    }
}

static bool action_should_save_display_settings(esp_bms_lvgl_action_t action)
{
    return action == ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS ||
           action == ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS ||
           action == ESP_BMS_LVGL_ACTION_SET_VOLUME ||
           action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE ||
           action == ESP_BMS_LVGL_ACTION_SET_SPEED_SOURCE ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_JK ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE ||
           action == ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE ||
           action == ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE ||
           action == ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND ||
           action == ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL ||
           action == ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO ||
           action == ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE ||
           action == ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO ||
           action == ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE ||
           action == ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS;
}

static void log_heap_state(const char *stage)
{
    const uint32_t internal_dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    const uint32_t internal_8bit_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal8_free = heap_caps_get_free_size(internal_8bit_caps);
    const size_t internal8_largest = heap_caps_get_largest_free_block(internal_8bit_caps);
    const size_t main_stack_words = uxTaskGetStackHighWaterMark(NULL);
    const unsigned internal8_fragment_pct = internal8_free == 0U
                                               ? 0U
                                               : (unsigned)((100U * (internal8_free - internal8_largest)) / internal8_free);
    ESP_LOGI(TAG,
             "heap[%s] default_free=%u default_min=%u internal8_free=%u internal8_largest=%u internal8_frag=%u%% main_stack_free=%uB dma_free=%u dma_largest=%u dma_min=%u psram_free=%u psram_largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)internal8_free,
             (unsigned)internal8_largest,
             internal8_fragment_pct,
             (unsigned)(main_stack_words * sizeof(StackType_t)),
             (unsigned)heap_caps_get_free_size(internal_dma_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_dma_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_dma_caps),
             (unsigned)heap_caps_get_free_size(psram_caps),
             (unsigned)heap_caps_get_largest_free_block(psram_caps));
}

static UBaseType_t resource_diagnostics_capture_tasks(
    TaskStatus_t *snapshot,
    configRUN_TIME_COUNTER_TYPE *total_runtime)
{
    const UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0U || task_count > RESOURCE_DIAGNOSTICS_MAX_TASKS) {
        return 0U;
    }
    return uxTaskGetSystemState(snapshot, task_count, total_runtime);
}

static const TaskStatus_t *resource_diagnostics_find_task(const TaskStatus_t *snapshot,
                                                           UBaseType_t task_count,
                                                           TaskHandle_t handle)
{
    for (UBaseType_t index = 0U; index < task_count; ++index) {
        if (snapshot[index].xHandle == handle) {
            return &snapshot[index];
        }
    }
    return NULL;
}

static bool resource_diagnostics_is_idle_task(const TaskStatus_t *task)
{
    return task->pcTaskName && strncmp(task->pcTaskName, "IDLE", 4U) == 0;
}

static unsigned resource_diagnostics_percent(uint64_t value, uint64_t total)
{
    return total == 0U ? 0U : (unsigned)((100U * value) / total);
}

static void resource_diagnostics_log_memory(const resource_diagnostics_t *diagnostics,
                                            const char *phase)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const uint32_t elapsed_ms =
        (uint32_t)((esp_timer_get_time() - diagnostics->started_us) / 1000);
    ESP_LOGI(TAG,
             "resource action=%s phase=%s elapsed_ms=%u heap_default_free=%u heap_default_min=%u internal8_free=%u internal8_min=%u psram_free=%u psram_min=%u",
             diagnostics->action_name,
             phase,
             elapsed_ms,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_free_size(psram_caps),
             (unsigned)heap_caps_get_minimum_free_size(psram_caps));
}

static void resource_diagnostics_log_cpu(resource_diagnostics_t *diagnostics)
{
    configRUN_TIME_COUNTER_TYPE total_runtime = 0U;
    const UBaseType_t task_count =
        resource_diagnostics_capture_tasks(diagnostics->after, &total_runtime);
    if (diagnostics->task_count == 0U || task_count == 0U) {
        ESP_LOGW(TAG, "resource action=%s phase=end cpu=unavailable tasks=%u",
                 diagnostics->action_name, (unsigned)uxTaskGetNumberOfTasks());
        return;
    }

    const configRUN_TIME_COUNTER_TYPE runtime_elapsed = total_runtime - diagnostics->total_runtime;
    const uint64_t total_capacity = (uint64_t)runtime_elapsed * configNUMBER_OF_CORES;
    if (total_capacity == 0U) {
        ESP_LOGW(TAG, "resource action=%s phase=end cpu=unavailable elapsed=0",
                 diagnostics->action_name);
        return;
    }

    resource_diagnostics_task_usage_t top[RESOURCE_DIAGNOSTICS_TOP_TASKS] = { 0 };
    uint64_t idle_runtime = 0U;
    for (UBaseType_t index = 0U; index < task_count; ++index) {
        const TaskStatus_t *current = &diagnostics->after[index];
        const TaskStatus_t *previous = resource_diagnostics_find_task(
            diagnostics->before, diagnostics->task_count, current->xHandle);
        const configRUN_TIME_COUNTER_TYPE previous_runtime =
            previous ? previous->ulRunTimeCounter : 0U;
        const uint64_t runtime = (uint64_t)(current->ulRunTimeCounter - previous_runtime);
        if (resource_diagnostics_is_idle_task(current)) {
            idle_runtime += runtime;
            continue;
        }
        for (size_t position = 0U; position < RESOURCE_DIAGNOSTICS_TOP_TASKS; ++position) {
            if (runtime <= top[position].runtime) {
                continue;
            }
            for (size_t move = RESOURCE_DIAGNOSTICS_TOP_TASKS - 1U; move > position; --move) {
                top[move] = top[move - 1U];
            }
            top[position] = (resource_diagnostics_task_usage_t) {
                .name = current->pcTaskName,
                .runtime = runtime,
            };
            break;
        }
    }

    const uint64_t busy_runtime = idle_runtime >= total_capacity ? 0U : total_capacity - idle_runtime;
    ESP_LOGI(TAG,
             "resource action=%s phase=end cpu_busy_pct=%u task=%s:%u%% task=%s:%u%% task=%s:%u%%",
             diagnostics->action_name,
             resource_diagnostics_percent(busy_runtime, total_capacity),
             top[0].name ? top[0].name : "-",
             resource_diagnostics_percent(top[0].runtime, total_capacity),
             top[1].name ? top[1].name : "-",
             resource_diagnostics_percent(top[1].runtime, total_capacity),
             top[2].name ? top[2].name : "-",
             resource_diagnostics_percent(top[2].runtime, total_capacity));
}

static void resource_diagnostics_end(resource_diagnostics_t *diagnostics)
{
    if (!diagnostics->active) {
        return;
    }

    resource_diagnostics_log_memory(diagnostics, "end");
    resource_diagnostics_log_cpu(diagnostics);
    if (diagnostics->heap_monitoring &&
        heap_caps_monitor_local_minimum_free_size_stop() != ESP_OK) {
        ESP_LOGW(TAG, "resource action=%s heap local-min stop failed",
                 diagnostics->action_name);
    }
    diagnostics->active = false;
}

static void resource_diagnostics_begin(resource_diagnostics_t *diagnostics, const char *action_name)
{
    resource_diagnostics_end(diagnostics);
    diagnostics->action_name = action_name;
    diagnostics->started_us = esp_timer_get_time();
    diagnostics->task_count =
        resource_diagnostics_capture_tasks(diagnostics->before, &diagnostics->total_runtime);
    diagnostics->heap_monitoring =
        heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
    diagnostics->active = true;
    if (!diagnostics->heap_monitoring) {
        ESP_LOGW(TAG, "resource action=%s heap local-min unavailable", action_name);
    }
    resource_diagnostics_log_memory(diagnostics, "begin");
}

static void resource_diagnostics_poll(resource_diagnostics_t *diagnostics)
{
    if (diagnostics->active &&
        esp_timer_get_time() - diagnostics->started_us >=
            (int64_t)RESOURCE_DIAGNOSTICS_WINDOW_MS * 1000) {
        resource_diagnostics_end(diagnostics);
    }
}

static void boot_animation_update(uint8_t progress_percent, const char *status_text)
{
    esp_bms_display_service_command_t command = {
        .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_UPDATE,
        .data.boot_update.progress_percent = progress_percent,
    };
    (void)snprintf(command.data.boot_update.status_text,
                   sizeof(command.data.boot_update.status_text),
                   "%s",
                   status_text && status_text[0] != '\0' ? status_text : "BOOT");
    const esp_err_t ret = esp_bms_display_service_submit_command(&command, 1000U);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "boot animation update failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    vTaskPrioritySet(NULL, MAIN_LOOP_TASK_PRIORITY);
    ESP_LOGI(TAG, "starting ESP-IDF LVGL adapter display path");
    ESP_LOGI(TAG, "main loop task priority=%u", (unsigned)uxTaskPriorityGet(NULL));

    static esp_bms_idf_runtime_t runtime;
    esp_bms_idf_runtime_init(&runtime);
    esp_bms_lvgl_bridge_config_t config = ESP_BMS_PROFILE_LVGL_CONFIG;
    esp_bms_idf_runtime_set_display_rotation_default(
        &runtime,
        runtime_rotation_from_bridge(config.rotation),
        ESP_BMS_PROFILE_DISPLAY_ROTATION_DEFAULT_VERSION);
    const esp_err_t modules_ret = esp_bms_module_registry_init(&runtime);
    if (modules_ret != ESP_OK) {
        ESP_LOGW(TAG, "optional module init failed: %s", esp_err_to_name(modules_ret));
    }
    log_heap_state("runtime_init");

    bool display_settings_loaded = false;
    const esp_err_t display_settings_ret =
        esp_bms_idf_runtime_load_display_settings(&runtime, &display_settings_loaded);
    if (display_settings_ret == ESP_OK && display_settings_loaded) {
        ESP_LOGI(TAG, "display settings loaded from NVS before first frame");
    } else if (display_settings_ret == ESP_ERR_NVS_NOT_FOUND ||
               display_settings_ret == ESP_ERR_INVALID_STATE) {
        if (display_settings_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "display settings in NVS are invalid; saving defaults");
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_idf_runtime_save_display_settings(&runtime));
    } else {
        ESP_LOGW(TAG, "display settings load failed: %s",
                 esp_err_to_name(display_settings_ret));
    }

    config.rotation = bridge_rotation_from_runtime(runtime.display_rotation);
    ESP_ERROR_CHECK(esp_bms_display_service_start(&config,
                                                   runtime.brightness_percent,
                                                   &runtime.snapshot));
    log_heap_state("first_ui");

    ESP_LOGI(TAG, "display path initialized");
    vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS));
    boot_animation_update(15U, "DISPLAY READY");
    boot_animation_update(25U, "SETTINGS LOADED");
    log_heap_state("display_settings");

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_module_registry_start(&runtime));
    boot_animation_update(35U, "BLE START");

    if (esp_bms_module_registry_gps_enabled()) {
        const int64_t gps_probe_start_us = esp_timer_get_time();
        int64_t gps_probe_last_tick_us = gps_probe_start_us;
        uint32_t gps_probe_elapsed_ms = 0U;
        while (gps_probe_elapsed_ms < 3000U) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS));
            const int64_t now_us = esp_timer_get_time();
            const uint32_t tick_elapsed_ms =
                (uint32_t)((now_us - gps_probe_last_tick_us) / 1000);
            gps_probe_last_tick_us = now_us;
            (void)esp_bms_idf_runtime_tick(
                &runtime, tick_elapsed_ms > 0U ? tick_elapsed_ms : MAIN_LOOP_PERIOD_MS);
            esp_bms_module_registry_tick(&runtime,
                                         tick_elapsed_ms > 0U ? tick_elapsed_ms : MAIN_LOOP_PERIOD_MS);
            const uint64_t wall_elapsed_ms =
                (uint64_t)(now_us - gps_probe_start_us) / UINT64_C(1000);
            gps_probe_elapsed_ms = wall_elapsed_ms >= 3000U ? 3000U : (uint32_t)wall_elapsed_ms;
            const char *gps_status = esp_bms_module_registry_gps_is_available(&runtime)
                                         ? "GPS READY"
                                         : "GPS CHECK";
            boot_animation_update((uint8_t)(35U + ((gps_probe_elapsed_ms * 50U) / 3000U)),
                                  gps_status);
        }
        (void)esp_bms_module_registry_gps_finish_startup_probe(&runtime);
        boot_animation_update(92U,
                              esp_bms_module_registry_gps_is_available(&runtime)
                                  ? "GPS READY"
                                  : "GPS OFFLINE");
    } else {
        boot_animation_update(92U, "MODULES READY");
    }
    boot_animation_update(100U, "SYSTEM READY");
    vTaskDelay(pdMS_TO_TICKS(BOOT_READY_HOLD_MS));

    const esp_bms_display_service_command_t boot_finish_command = {
        .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_BOOT_FINISH,
        .data.boot_finish.snapshot = &runtime.snapshot,
    };
    const esp_err_t boot_finish_ret =
        esp_bms_display_service_submit_command(&boot_finish_command, 1000U);
    if (boot_finish_ret != ESP_OK) {
        ESP_LOGE(TAG, "boot animation finish failed: %s",
                 esp_err_to_name(boot_finish_ret));
    }
    log_heap_state("boot_ready");

    bool delayed_display_settings_save_pending = false;
    uint32_t delayed_display_settings_save_ms = 0;
    uint32_t setup_ap_idle_elapsed_ms = 0;
    uint32_t setup_ap_start_guard_ms = 0;
#if ESP_BMS_FEATURE_CAST
    bool cast_ui_active = false;
#endif
    setup_service_start_stage_t setup_service_start_stage = SETUP_SERVICE_START_IDLE;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS));
        resource_diagnostics_poll(&s_resource_diagnostics);
        const uint8_t previous_brightness = runtime.brightness_percent;
        const esp_bms_idf_display_rotation_t previous_rotation = runtime.display_rotation;
        const bool tick_changed = esp_bms_idf_runtime_tick(&runtime, 50);
        const bool module_tick_changed =
            esp_bms_module_registry_tick(&runtime, MAIN_LOOP_PERIOD_MS);
        if (setup_ap_start_guard_ms > MAIN_LOOP_PERIOD_MS) {
            setup_ap_start_guard_ms -= MAIN_LOOP_PERIOD_MS;
        } else {
            setup_ap_start_guard_ms = 0;
        }
        const uint8_t connection_audio_events =
            esp_bms_idf_runtime_take_connection_audio_events(&runtime);
        esp_bms_module_registry_play_connection_audio(connection_audio_events,
                                                       runtime.volume_percent);

        esp_err_t ret = ESP_OK;
        const bool http_config_changed =
            esp_bms_idf_runtime_apply_pending_http_config(&runtime);
        esp_bms_lvgl_action_event_t action_event = { 0 };
        ret = esp_bms_display_service_take_action_event(&action_event);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "take UI action failed: %s", esp_err_to_name(ret));
            continue;
        }
        esp_bms_idf_runtime_set_active_data_source(&runtime,
                                                   esp_bms_display_service_stable_data_source());
        esp_bms_lvgl_action_t action = action_event.action;
        if (action != ESP_BMS_LVGL_ACTION_NONE) {
            resource_diagnostics_begin(&s_resource_diagnostics,
                                       esp_bms_idf_runtime_action_name(action));
        }
        const bool http_config_applied =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_APPLIED);
        const bool setup_ap_started =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_SETUP_AP_STARTED);
        const bool http_server_started =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SERVER_STARTED);
        const bool setup_ap_enabled =
            esp_bms_dashboard_snapshot_flag_get(&runtime.snapshot,
                                                ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED);
        if (action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING &&
            setup_ap_enabled && setup_ap_start_guard_ms > 0U) {
            action_event.action = ESP_BMS_LVGL_ACTION_NONE;
            action = ESP_BMS_LVGL_ACTION_NONE;
            ESP_LOGW(TAG, "ignored duplicate setup AP toggle during startup");
        }
        if ((setup_ap_started || http_server_started) && runtime.setup_ap_clients == 0U) {
            if (setup_ap_idle_elapsed_ms <= SETUP_AP_IDLE_TIMEOUT_MS - MAIN_LOOP_PERIOD_MS) {
                setup_ap_idle_elapsed_ms += MAIN_LOOP_PERIOD_MS;
            }
        } else {
            setup_ap_idle_elapsed_ms = 0;
        }
        const bool should_start_setup_services =
            action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING &&
            !setup_ap_enabled &&
            setup_service_start_stage == SETUP_SERVICE_START_IDLE &&
            (!setup_ap_started || !http_server_started);
        const bool should_stop_setup_services =
            (action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING &&
             setup_ap_enabled &&
             (setup_ap_started || http_server_started)) ||
            (setup_ap_idle_elapsed_ms >= SETUP_AP_IDLE_TIMEOUT_MS);
        const bool action_changed =
            esp_bms_idf_runtime_apply_action_event(&runtime, &action_event);
        if (action == ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS && action_changed) {
            const esp_bms_display_service_command_t reset_calibration_command = {
                .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_RESET_TOUCH_CALIBRATION,
            };
            const esp_err_t reset_ret =
                esp_bms_display_service_submit_command(&reset_calibration_command, 1000U);
            if (reset_ret != ESP_OK) {
                ESP_LOGW(TAG, "reset touch calibration failed: %s", esp_err_to_name(reset_ret));
            }
        }
        bool display_apply_failed = false;
        if ((tick_changed || module_tick_changed || action_changed || http_config_changed) &&
            runtime.brightness_percent != previous_brightness) {
            const esp_bms_display_service_command_t brightness_command = {
                .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_BRIGHTNESS,
                .data.brightness.percent = runtime.brightness_percent,
            };
            ret = esp_bms_display_service_submit_command(&brightness_command, 1000U);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "apply brightness action failed: %s", esp_err_to_name(ret));
                runtime.brightness_percent = previous_brightness;
                runtime.snapshot.brightness_percent = previous_brightness;
                display_apply_failed = true;
            }
        }
        if ((tick_changed || module_tick_changed || action_changed || http_config_changed) &&
            runtime.display_rotation != previous_rotation) {
            const esp_bms_display_service_command_t rotation_command = {
                .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_ROTATION,
                .data.rotation.rotation = bridge_rotation_from_runtime(runtime.display_rotation),
            };
            ret = esp_bms_display_service_submit_command(&rotation_command, 1000U);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "apply rotation action failed: %s", esp_err_to_name(ret));
                runtime.display_rotation = previous_rotation;
                display_apply_failed = true;
            }
        }
        if (tick_changed || module_tick_changed || action_changed || http_config_changed ||
            display_apply_failed) {
            ret = esp_bms_display_service_publish_snapshot(&runtime.snapshot);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "update UI after action failed: %s", esp_err_to_name(ret));
                display_apply_failed = true;
            }
        }
        if (http_config_applied && !display_apply_failed) {
            const esp_bms_display_service_command_t show_dashboard_command = {
                .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_SHOW_DASHBOARD,
            };
            ret = esp_bms_display_service_submit_command(&show_dashboard_command, 1000U);
            if (ret == ESP_OK) {
                esp_bms_idf_runtime_flag_set(&runtime,
                                             ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_APPLIED,
                                             false);
            } else {
                ESP_LOGE(TAG, "show dashboard after Web config failed: %s", esp_err_to_name(ret));
                display_apply_failed = true;
            }
        }
#if ESP_BMS_FEATURE_CAST
        if (!display_apply_failed && cast_ui_active != runtime.snapshot.cast_active) {
            const esp_bms_display_service_command_t show_dashboard_command = {
                .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_SHOW_DASHBOARD,
            };
            ret = esp_bms_display_service_submit_command(&show_dashboard_command, 1000U);
            if (ret == ESP_OK) {
                const esp_bms_lvgl_page_t target_page = runtime.snapshot.cast_active
                                                            ? ESP_BMS_LVGL_PAGE_CAST
                                                            : ESP_BMS_LVGL_PAGE_BATTERY;
                const esp_bms_display_service_command_t page_command = {
                    .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_SET_PAGE,
                    .data.page = {
                        .page = target_page,
                        .animated = false,
                    },
                };
                ret = esp_bms_display_service_submit_command(&page_command, 1000U);
            }
            if (ret == ESP_OK) {
                cast_ui_active = runtime.snapshot.cast_active;
                ESP_LOGI(TAG,
                         "cast UI %s; page=%s",
                         cast_ui_active ? "entered" : "restored",
                         cast_ui_active ? "cast" : "battery");
            } else {
                ESP_LOGE(TAG,
                         "apply cast UI transition failed: %s",
                         esp_err_to_name(ret));
                display_apply_failed = true;
            }
        }
#endif
        const bool action_committed =
            esp_bms_lvgl_action_event_flag_get(&action_event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_COMMITTED);
        const bool controller_settings_save_requested =
            esp_bms_idf_runtime_flag_get(
                &runtime,
                ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SETTINGS_SAVE_REQUESTED);
        bool should_save_display_settings = false;
        if (!display_apply_failed && action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY && action_changed) {
            delayed_display_settings_save_pending = true;
            delayed_display_settings_save_ms = ESP_BMS_LVGL_ROTATE_SAVE_DELAY_MS;
        } else if (action_committed && !display_apply_failed &&
                   action_should_save_display_settings(action) &&
                   (action != ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND ||
                    esp_bms_lvgl_action_event_flag_get(
                        &action_event,
                        ESP_BMS_LVGL_ACTION_EVENT_FLAG_CONTROLLER_MAC_VALID))) {
            should_save_display_settings = !delayed_display_settings_save_pending;
        }
        if (delayed_display_settings_save_pending && action != ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY) {
            if (delayed_display_settings_save_ms <= 50U) {
                delayed_display_settings_save_pending = false;
                delayed_display_settings_save_ms = 0;
                should_save_display_settings = true;
            } else {
                delayed_display_settings_save_ms -= 50U;
            }
        }
        if (controller_settings_save_requested && !display_apply_failed) {
            should_save_display_settings = true;
        }
        if (esp_bms_lvgl_action_event_flag_get(&action_event,
                                               ESP_BMS_LVGL_ACTION_EVENT_FLAG_VOLUME_FEEDBACK_VALID)) {
            esp_bms_module_registry_play_volume_audio(action_event.volume_feedback_percent);
        }
        if (should_save_display_settings) {
            const esp_err_t save_ret = esp_bms_idf_runtime_save_display_settings(&runtime);
            if (save_ret != ESP_OK) {
                ESP_LOGW(TAG, "display settings save failed: %s", esp_err_to_name(save_ret));
            } else if (controller_settings_save_requested) {
                esp_bms_idf_runtime_flag_set(
                    &runtime,
                    ESP_BMS_IDF_RUNTIME_FLAG_CONTROLLER_SETTINGS_SAVE_REQUESTED,
                    false);
            }
        }

        if (should_start_setup_services) {
            setup_ap_start_guard_ms = SETUP_AP_START_GUARD_MS;
            setup_service_start_stage = setup_ap_started ? SETUP_SERVICE_START_HTTP : SETUP_SERVICE_START_AP;
            ESP_LOGI(TAG, "setup services queued: first_stage=%s",
                     setup_service_start_stage == SETUP_SERVICE_START_AP ? "ap" : "http");
        }

        if (should_stop_setup_services) {
            setup_service_start_stage = SETUP_SERVICE_START_IDLE;
            if (setup_ap_idle_elapsed_ms >= SETUP_AP_IDLE_TIMEOUT_MS) {
                ESP_LOGI(TAG, "setup AP idle for %u ms; stopping setup services",
                         (unsigned)setup_ap_idle_elapsed_ms);
            }
            setup_ap_idle_elapsed_ms = 0;
            log_heap_state("setup_stop_before");
            ret = esp_bms_module_registry_stop_setup_services(&runtime);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "setup services stop failed: %s", esp_err_to_name(ret));
            }
            log_heap_state("setup_stop_after");
            ret = esp_bms_display_service_publish_snapshot(&runtime.snapshot);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "update UI after setup service stop failed: %s",
                         esp_err_to_name(ret));
            }
        }

        if (setup_service_start_stage != SETUP_SERVICE_START_IDLE) {
            const setup_service_start_stage_t stage = setup_service_start_stage;
            setup_service_start_stage = SETUP_SERVICE_START_IDLE;
            log_heap_state(stage == SETUP_SERVICE_START_AP ? "setup_ap_before" : "setup_http_before");
            if (stage == SETUP_SERVICE_START_AP) {
                ret = esp_bms_module_registry_start_setup_ap(&runtime);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "setup AP start failed: %s", esp_err_to_name(ret));
                } else if (!esp_bms_idf_runtime_flag_get(&runtime,
                                                          ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SERVER_STARTED)) {
                    setup_service_start_stage = SETUP_SERVICE_START_HTTP;
                }
                log_heap_state("setup_ap_after");
            } else {
                ret = esp_bms_module_registry_start_http_server(&runtime);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
                }
                log_heap_state("setup_http_after");
            }
            ret = esp_bms_display_service_publish_snapshot(&runtime.snapshot);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "update UI after setup service action failed: %s", esp_err_to_name(ret));
            }
        }

        if (action != ESP_BMS_LVGL_ACTION_NONE) {
            ESP_LOGI(TAG, "ui action=%s changed=%s",
                     esp_bms_idf_runtime_action_name(action),
                     action_changed ? "yes" : "no");
        }
    }
}
