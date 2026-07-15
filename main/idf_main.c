#include "esp_bms_audio_feedback.h"
#include "esp_bms_idf_runtime.h"
#include "esp_bms_lvgl_bridge.h"
#include "esp_bms_lvgl_ui.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

static const char *TAG = "bms_idf_main";

#define MAIN_LOOP_TASK_PRIORITY 4U
#define MAIN_LOOP_PERIOD_MS 50U
#define SETUP_AP_IDLE_TIMEOUT_MS (5U * 60U * 1000U)

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

static bool action_should_save_display_settings(esp_bms_lvgl_action_t action)
{
    return action == ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS ||
           action == ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS ||
           action == ESP_BMS_LVGL_ACTION_SET_VOLUME ||
           action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_JK ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD ||
           action == ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE ||
           action == ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE ||
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

void app_main(void)
{
    vTaskPrioritySet(NULL, MAIN_LOOP_TASK_PRIORITY);
    ESP_LOGI(TAG, "starting ESP-IDF LVGL adapter display path");
    ESP_LOGI(TAG, "main loop task priority=%u", (unsigned)uxTaskPriorityGet(NULL));

    static esp_bms_idf_runtime_t runtime;
    esp_bms_idf_runtime_init(&runtime);
    const esp_err_t audio_ret = esp_bms_audio_feedback_init();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "audio feedback init failed: %s", esp_err_to_name(audio_ret));
    }
    log_heap_state("runtime_init");

    const esp_bms_lvgl_bridge_config_t config = ESP_BMS_LVGL_BRIDGE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_bms_lvgl_bridge_init(&config));
    ESP_ERROR_CHECK(esp_bms_lvgl_bridge_set_brightness(runtime.brightness_percent));
    log_heap_state("lvgl_bridge");

    ESP_ERROR_CHECK(esp_bms_lvgl_bridge_lock(-1));
    ESP_ERROR_CHECK(esp_bms_lvgl_ui_init(esp_bms_lvgl_bridge_get_display()));
    ESP_ERROR_CHECK(esp_bms_lvgl_ui_update(&runtime.snapshot));
    esp_bms_lvgl_bridge_unlock();
    log_heap_state("first_ui");

    ESP_LOGI(TAG, "display path initialized");

    bool display_settings_loaded = false;
    const esp_err_t display_settings_ret =
        esp_bms_idf_runtime_load_display_settings(&runtime, &display_settings_loaded);
    if (display_settings_ret == ESP_OK && display_settings_loaded) {
        ESP_LOGI(TAG, "display settings loaded from NVS");
        ESP_ERROR_CHECK(esp_bms_lvgl_bridge_lock(-1));
        ESP_ERROR_CHECK(esp_bms_lvgl_bridge_set_brightness(runtime.brightness_percent));
        ESP_ERROR_CHECK(esp_bms_lvgl_bridge_set_rotation(bridge_rotation_from_runtime(runtime.display_rotation)));
        ESP_ERROR_CHECK(esp_bms_lvgl_ui_update(&runtime.snapshot));
        esp_bms_lvgl_bridge_unlock();
    } else if (display_settings_ret == ESP_ERR_NVS_NOT_FOUND ||
               display_settings_ret == ESP_ERR_INVALID_STATE) {
        if (display_settings_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "display settings in NVS are invalid; saving defaults");
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_idf_runtime_save_display_settings(&runtime));
    } else {
        ESP_LOGW(TAG, "display settings load failed: %s", esp_err_to_name(display_settings_ret));
    }
    const esp_err_t touch_calibration_load_ret =
        esp_bms_lvgl_bridge_load_touch_calibration();
    if (touch_calibration_load_ret != ESP_OK) {
        ESP_LOGW(TAG, "touch calibration load failed: %s",
                 esp_err_to_name(touch_calibration_load_ret));
    }
    log_heap_state("display_settings");

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_idf_runtime_start_controller_ble_if_enabled(&runtime));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_idf_runtime_start_bms_ble_if_bound(&runtime));

    bool delayed_display_settings_save_pending = false;
    uint32_t delayed_display_settings_save_ms = 0;
    uint32_t setup_ap_idle_elapsed_ms = 0;
    bool cast_ui_active = false;
    setup_service_start_stage_t setup_service_start_stage = SETUP_SERVICE_START_IDLE;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS));
        esp_bms_audio_feedback_tick();
        const uint8_t previous_brightness = runtime.brightness_percent;
        const esp_bms_idf_display_rotation_t previous_rotation = runtime.display_rotation;
        const bool tick_changed = esp_bms_idf_runtime_tick(&runtime, 50);
        const uint8_t connection_audio_events =
            esp_bms_idf_runtime_take_connection_audio_events(&runtime);
        if ((connection_audio_events & ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED) != 0U) {
            esp_bms_audio_feedback_play_voice(ESP_BMS_AUDIO_VOICE_BMS_CONNECTED,
                                              runtime.volume_percent);
        }
        if ((connection_audio_events & ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED) != 0U) {
            esp_bms_audio_feedback_play_voice(ESP_BMS_AUDIO_VOICE_CONTROLLER_CONNECTED,
                                              runtime.volume_percent);
        }

        esp_err_t ret = esp_bms_lvgl_bridge_lock(-1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LVGL lock failed while handling UI action: %s", esp_err_to_name(ret));
            continue;
        }
        const bool http_config_changed =
            esp_bms_idf_runtime_apply_pending_http_config(&runtime);
        esp_bms_lvgl_action_event_t action_event = { 0 };
        ret = esp_bms_lvgl_ui_take_action_event(&action_event);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "take UI action failed: %s", esp_err_to_name(ret));
            esp_bms_lvgl_bridge_unlock();
            continue;
        }
        esp_bms_idf_runtime_set_active_data_source(&runtime,
                                                   esp_bms_lvgl_ui_stable_data_source());
        const esp_bms_lvgl_action_t action = action_event.action;
        const bool http_config_applied =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_APPLIED);
        const bool setup_ap_started =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_SETUP_AP_STARTED);
        const bool http_server_started =
            esp_bms_idf_runtime_flag_get(&runtime, ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SERVER_STARTED);
        const bool setup_ap_enabled =
            esp_bms_dashboard_snapshot_flag_get(&runtime.snapshot,
                                                ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED);
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
        const bool touch_calibration_action =
            action == ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION ||
            action == ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE ||
            action == ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION;
        bool action_changed = false;
        if (action == ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION) {
            ret = esp_bms_lvgl_bridge_begin_touch_calibration();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "start touch calibration failed: %s", esp_err_to_name(ret));
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bms_lvgl_ui_touch_calibration_result(false));
            }
        } else if (action == ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE) {
            bool finished = false;
            ret = esp_bms_lvgl_bridge_add_touch_calibration_sample(
                action_event.touch_target_index,
                action_event.touch_observed_x,
                action_event.touch_observed_y,
                action_event.touch_target_x,
                action_event.touch_target_y,
                &finished);
            if (ret != ESP_OK || finished) {
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "touch calibration sample failed: %s", esp_err_to_name(ret));
                }
                ESP_ERROR_CHECK_WITHOUT_ABORT(
                    esp_bms_lvgl_ui_touch_calibration_result(ret == ESP_OK && finished));
            }
        } else if (action == ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION) {
            esp_bms_lvgl_bridge_cancel_touch_calibration();
        } else {
            action_changed = esp_bms_idf_runtime_apply_action_event(&runtime, &action_event);
        }
        if (!touch_calibration_action && action == ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS &&
            action_changed) {
            const esp_err_t reset_ret = esp_bms_lvgl_bridge_reset_touch_calibration();
            if (reset_ret != ESP_OK) {
                ESP_LOGW(TAG, "reset touch calibration failed: %s", esp_err_to_name(reset_ret));
            }
        }
        bool display_apply_failed = false;
        if ((tick_changed || action_changed || http_config_changed) &&
            runtime.brightness_percent != previous_brightness) {
            ret = esp_bms_lvgl_bridge_set_brightness(runtime.brightness_percent);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "apply brightness action failed: %s", esp_err_to_name(ret));
                runtime.brightness_percent = previous_brightness;
                runtime.snapshot.brightness_percent = previous_brightness;
                display_apply_failed = true;
            }
        }
        if ((tick_changed || action_changed || http_config_changed) &&
            runtime.display_rotation != previous_rotation) {
            ret = esp_bms_lvgl_bridge_set_rotation(bridge_rotation_from_runtime(runtime.display_rotation));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "apply rotation action failed: %s", esp_err_to_name(ret));
                runtime.display_rotation = previous_rotation;
                display_apply_failed = true;
            }
        }
        if (tick_changed || action_changed || http_config_changed || display_apply_failed) {
            ret = esp_bms_lvgl_ui_update(&runtime.snapshot);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "update UI after action failed: %s", esp_err_to_name(ret));
                display_apply_failed = true;
            }
        }
        if (http_config_applied && !display_apply_failed) {
            ret = esp_bms_lvgl_ui_show_dashboard();
            if (ret == ESP_OK) {
                esp_bms_idf_runtime_flag_set(&runtime,
                                             ESP_BMS_IDF_RUNTIME_FLAG_HTTP_CONFIG_APPLIED,
                                             false);
            } else {
                ESP_LOGE(TAG, "show dashboard after Web config failed: %s", esp_err_to_name(ret));
                display_apply_failed = true;
            }
        }
        if (!display_apply_failed && cast_ui_active != runtime.snapshot.cast_active) {
            ret = esp_bms_lvgl_ui_show_dashboard();
            if (ret == ESP_OK) {
                const esp_bms_lvgl_page_t target_page = runtime.snapshot.cast_active
                                                            ? ESP_BMS_LVGL_PAGE_CAST
                                                            : ESP_BMS_LVGL_PAGE_BATTERY;
                ret = esp_bms_lvgl_ui_set_page(target_page, false);
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
        esp_bms_lvgl_bridge_unlock();

        if (esp_bms_lvgl_action_event_flag_get(&action_event,
                                               ESP_BMS_LVGL_ACTION_EVENT_FLAG_VOLUME_FEEDBACK_VALID)) {
            esp_bms_audio_feedback_play_volume(action_event.volume_feedback_percent);
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
            ret = esp_bms_idf_runtime_stop_setup_services(&runtime);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "setup services stop failed: %s", esp_err_to_name(ret));
            }
            log_heap_state("setup_stop_after");
            ret = esp_bms_lvgl_bridge_lock(-1);
            if (ret == ESP_OK) {
                const esp_err_t update_ret = esp_bms_lvgl_ui_update(&runtime.snapshot);
                if (update_ret != ESP_OK) {
                    ESP_LOGE(TAG, "update UI after setup service stop failed: %s",
                             esp_err_to_name(update_ret));
                }
                esp_bms_lvgl_bridge_unlock();
            } else {
                ESP_LOGE(TAG, "LVGL lock after setup service stop failed: %s", esp_err_to_name(ret));
            }
        }

        if (setup_service_start_stage != SETUP_SERVICE_START_IDLE) {
            const setup_service_start_stage_t stage = setup_service_start_stage;
            setup_service_start_stage = SETUP_SERVICE_START_IDLE;
            log_heap_state(stage == SETUP_SERVICE_START_AP ? "setup_ap_before" : "setup_http_before");
            if (stage == SETUP_SERVICE_START_AP) {
                ret = esp_bms_idf_runtime_start_setup_ap(&runtime);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "setup AP start failed: %s", esp_err_to_name(ret));
                } else if (!esp_bms_idf_runtime_flag_get(&runtime,
                                                          ESP_BMS_IDF_RUNTIME_FLAG_HTTP_SERVER_STARTED)) {
                    setup_service_start_stage = SETUP_SERVICE_START_HTTP;
                }
                log_heap_state("setup_ap_after");
            } else {
                ret = esp_bms_idf_runtime_start_http_server(&runtime);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
                }
                log_heap_state("setup_http_after");
            }
            ret = esp_bms_lvgl_bridge_lock(-1);
            if (ret == ESP_OK) {
                const esp_err_t update_ret = esp_bms_lvgl_ui_update(&runtime.snapshot);
                if (update_ret != ESP_OK) {
                    ESP_LOGE(TAG, "update UI after setup service action failed: %s", esp_err_to_name(update_ret));
                }
                esp_bms_lvgl_bridge_unlock();
            } else {
                ESP_LOGE(TAG, "LVGL lock after setup service action failed: %s", esp_err_to_name(ret));
            }
        }

        if (action != ESP_BMS_LVGL_ACTION_NONE) {
            ESP_LOGI(TAG, "ui action=%s changed=%s",
                     esp_bms_idf_runtime_action_name(action),
                     action_changed ? "yes" : "no");
        }
    }
}
