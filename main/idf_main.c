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
           action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE ||
           action == ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS;
}

static void log_heap_state(const char *stage)
{
    const uint32_t internal_dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    const uint32_t internal_8bit_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "heap[%s] default_free=%u default_min=%u internal8_free=%u dma_free=%u dma_largest=%u dma_min=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(internal_8bit_caps),
             (unsigned)heap_caps_get_free_size(internal_dma_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_dma_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_dma_caps));
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting ESP-IDF LVGL adapter display path");

    esp_bms_idf_runtime_t runtime;
    esp_bms_idf_runtime_init(&runtime);
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
    log_heap_state("display_settings");

    const esp_err_t setup_ap_ret = esp_bms_idf_runtime_start_setup_ap(&runtime);
    if (setup_ap_ret != ESP_OK) {
        ESP_LOGE(TAG, "setup AP start failed: %s", esp_err_to_name(setup_ap_ret));
    }
    ESP_ERROR_CHECK(esp_bms_lvgl_bridge_lock(-1));
    ESP_ERROR_CHECK(esp_bms_lvgl_ui_update(&runtime.snapshot));
    esp_bms_lvgl_bridge_unlock();
    log_heap_state("setup_ap_http");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
        const uint8_t previous_brightness = runtime.brightness_percent;
        const esp_bms_idf_display_rotation_t previous_rotation = runtime.display_rotation;
        const bool tick_changed = esp_bms_idf_runtime_tick(&runtime, 50);

        ESP_ERROR_CHECK(esp_bms_lvgl_bridge_lock(-1));
        esp_bms_lvgl_action_t action = ESP_BMS_LVGL_ACTION_NONE;
        ESP_ERROR_CHECK(esp_bms_lvgl_ui_take_action(&action));
        const bool should_start_setup_ap =
            action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING && !runtime.setup_ap_started;
        const bool action_changed = esp_bms_idf_runtime_apply_action(&runtime, action);
        if ((tick_changed || action_changed) && runtime.brightness_percent != previous_brightness) {
            ESP_ERROR_CHECK(esp_bms_lvgl_bridge_set_brightness(runtime.brightness_percent));
        }
        if ((tick_changed || action_changed) && runtime.display_rotation != previous_rotation) {
            ESP_ERROR_CHECK(esp_bms_lvgl_bridge_set_rotation(bridge_rotation_from_runtime(runtime.display_rotation)));
        }
        if (tick_changed || action_changed) {
            ESP_ERROR_CHECK(esp_bms_lvgl_ui_update(&runtime.snapshot));
        }
        const bool should_save_display_settings =
            action_changed && action_should_save_display_settings(action);
        esp_bms_lvgl_bridge_unlock();

        if (should_save_display_settings) {
            const esp_err_t save_ret = esp_bms_idf_runtime_save_display_settings(&runtime);
            if (save_ret != ESP_OK) {
                ESP_LOGW(TAG, "display settings save failed: %s", esp_err_to_name(save_ret));
            }
        }

        if (should_start_setup_ap) {
            const esp_err_t ret = esp_bms_idf_runtime_start_setup_ap(&runtime);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "setup AP start failed: %s", esp_err_to_name(ret));
            }
            ESP_ERROR_CHECK(esp_bms_lvgl_bridge_lock(-1));
            ESP_ERROR_CHECK(esp_bms_lvgl_ui_update(&runtime.snapshot));
            esp_bms_lvgl_bridge_unlock();
        }

        if (action != ESP_BMS_LVGL_ACTION_NONE) {
            ESP_LOGI(TAG, "ui action=%s changed=%s",
                     esp_bms_idf_runtime_action_name(action),
                     action_changed ? "yes" : "no");
        }
    }
}
