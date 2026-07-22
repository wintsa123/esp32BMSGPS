#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
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

typedef enum {
    ESP_BMS_LVGL_DISPLAY_BUS_SPI = 0,
    ESP_BMS_LVGL_DISPLAY_BUS_I80,
} esp_bms_lvgl_display_bus_t;

typedef enum {
    ESP_BMS_LVGL_PANEL_ST7789 = 0,
    ESP_BMS_LVGL_PANEL_ST7796,
    ESP_BMS_LVGL_PANEL_ILI9488,
} esp_bms_lvgl_panel_driver_t;

typedef enum {
    ESP_BMS_LVGL_TOUCH_NONE = 0,
    ESP_BMS_LVGL_TOUCH_XPT2046,
    ESP_BMS_LVGL_TOUCH_FT5X06,
    ESP_BMS_LVGL_TOUCH_GT1151,
} esp_bms_lvgl_touch_driver_t;

typedef struct {
    esp_bms_lvgl_display_bus_t display_bus;
    esp_bms_lvgl_panel_driver_t panel_driver;
    esp_bms_lvgl_touch_driver_t touch_driver;
    gpio_num_t pin_miso;
    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_reset;
    gpio_num_t pin_backlight;
    gpio_num_t i80_data_pins[16];
    gpio_num_t pin_wr;
    uint8_t i80_bus_width;
    gpio_num_t pin_touch_miso;
    gpio_num_t pin_touch_mosi;
    gpio_num_t pin_touch_sclk;
    gpio_num_t pin_touch_cs;
    gpio_num_t pin_touch_irq;
    gpio_num_t pin_touch_reset;
    gpio_num_t pin_touch_sda;
    gpio_num_t pin_touch_scl;
    int backlight_on_level;
    uint32_t pixel_clock_hz;
    uint16_t physical_width;
    uint16_t physical_height;
    esp_bms_display_rotation_t rotation;
    lcd_rgb_element_order_t rgb_element_order;
    bool invert_color;
    uint8_t spi_mode;
    bool i80_swap_color_bytes;
    bool i80_pclk_active_neg;
    bool i80_pclk_idle_low;
    bool touch_use_irq;
    bool touch_swap_xy;
    bool touch_mirror_x;
    bool touch_mirror_y;
    uint16_t touch_i2c_address;
    uint32_t touch_i2c_clock_hz;
    uint8_t touch_i2c_control_phase_bytes;
    uint8_t touch_i2c_dc_bit_offset;
    uint8_t touch_i2c_cmd_bits;
    uint8_t touch_i2c_param_bits;
    bool touch_i2c_disable_control_phase;
    bool touch_i2c_internal_pullup;
    uint8_t touch_reset_level;
    uint8_t touch_irq_level;
    uint32_t power_on_delay_ms;
} esp_bms_lvgl_bridge_config_t;

esp_err_t esp_bms_lvgl_bridge_init(const esp_bms_lvgl_bridge_config_t *config);
esp_err_t esp_bms_lvgl_bridge_set_brightness(uint8_t percent);
esp_err_t esp_bms_lvgl_bridge_set_rotation(esp_bms_display_rotation_t rotation);
esp_err_t esp_bms_lvgl_bridge_load_touch_calibration(void);
esp_err_t esp_bms_lvgl_bridge_begin_touch_calibration(void);
esp_err_t esp_bms_lvgl_bridge_add_touch_calibration_sample(uint8_t target_index,
                                                           uint16_t observed_x,
                                                           uint16_t observed_y,
                                                           uint16_t target_x,
                                                           uint16_t target_y,
                                                           bool *finished);
void esp_bms_lvgl_bridge_cancel_touch_calibration(void);
esp_err_t esp_bms_lvgl_bridge_reset_touch_calibration(void);
esp_err_t esp_bms_lvgl_bridge_lock(int32_t timeout_ms);
void esp_bms_lvgl_bridge_unlock(void);
/* Caller holds the LVGL bridge lock. Pixels are RGB565 big-endian. */
esp_err_t esp_bms_lvgl_bridge_write_rgb565(uint16_t x,
                                           uint16_t y,
                                           uint16_t width,
                                           uint16_t height,
                                           const uint8_t *pixels,
                                           size_t pixel_bytes);
lv_display_t *esp_bms_lvgl_bridge_get_display(void);
lv_indev_t *esp_bms_lvgl_bridge_get_touch(void);

#ifdef __cplusplus
}
#endif
