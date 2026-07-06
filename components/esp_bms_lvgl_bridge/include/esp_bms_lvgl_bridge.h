#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_BMS_DISPLAY_ROTATION_PORTRAIT = 0,
    ESP_BMS_DISPLAY_ROTATION_LANDSCAPE,
    ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT,
    ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE,
} esp_bms_display_rotation_t;

typedef struct {
    gpio_num_t pin_miso;
    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_reset;
    gpio_num_t pin_backlight;
    gpio_num_t pin_touch_miso;
    gpio_num_t pin_touch_mosi;
    gpio_num_t pin_touch_sclk;
    gpio_num_t pin_touch_cs;
    gpio_num_t pin_touch_irq;
    int backlight_on_level;
    uint32_t pixel_clock_hz;
    uint16_t physical_width;
    uint16_t physical_height;
    esp_bms_display_rotation_t rotation;
    bool touch_use_irq;
    bool touch_swap_xy;
    bool touch_mirror_x;
    bool touch_mirror_y;
    uint32_t power_on_delay_ms;
} esp_bms_lvgl_bridge_config_t;

#define ESP_BMS_LVGL_BRIDGE_DEFAULT_CONFIG() {          \
    .pin_miso = GPIO_NUM_12,                            \
    .pin_mosi = GPIO_NUM_13,                            \
    .pin_sclk = GPIO_NUM_14,                            \
    .pin_cs = GPIO_NUM_15,                              \
    .pin_dc = GPIO_NUM_2,                               \
    .pin_reset = GPIO_NUM_NC,                           \
    .pin_backlight = GPIO_NUM_21,                       \
    .pin_touch_miso = GPIO_NUM_39,                      \
    .pin_touch_mosi = GPIO_NUM_32,                      \
    .pin_touch_sclk = GPIO_NUM_25,                      \
    .pin_touch_cs = GPIO_NUM_33,                        \
    .pin_touch_irq = GPIO_NUM_36,                       \
    .backlight_on_level = 1,                            \
    .pixel_clock_hz = 40 * 1000 * 1000,                 \
    .physical_width = 240,                              \
    .physical_height = 320,                             \
    .rotation = ESP_BMS_DISPLAY_ROTATION_LANDSCAPE,     \
    .touch_use_irq = false,                             \
    .touch_swap_xy = true,                              \
    .touch_mirror_x = false,                            \
    .touch_mirror_y = false,                            \
    .power_on_delay_ms = 1000,                          \
}

esp_err_t esp_bms_lvgl_bridge_init(const esp_bms_lvgl_bridge_config_t *config);
esp_err_t esp_bms_lvgl_bridge_set_brightness(uint8_t percent);
esp_err_t esp_bms_lvgl_bridge_set_rotation(esp_bms_display_rotation_t rotation);
esp_err_t esp_bms_lvgl_bridge_lock(int32_t timeout_ms);
void esp_bms_lvgl_bridge_unlock(void);
lv_display_t *esp_bms_lvgl_bridge_get_display(void);
lv_indev_t *esp_bms_lvgl_bridge_get_touch(void);

#ifdef __cplusplus
}
#endif
