#include "esp_bms_lvgl_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_bms_lvgl_contract.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_input.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bms_lvgl_bridge";

#define BACKLIGHT_PWM_FREQ_HZ 5000U
#define BACKLIGHT_PWM_DUTY_BITS 10U
#define BACKLIGHT_PWM_DUTY_MAX ((1U << BACKLIGHT_PWM_DUTY_BITS) - 1U)
#define BACKLIGHT_PWM_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_PWM_TIMER LEDC_TIMER_0
#define BACKLIGHT_PWM_CHANNEL LEDC_CHANNEL_0
#define LVGL_SPI_DRAW_BUFFER_HEIGHT CONFIG_ESP_BMS_LVGL_BRIDGE_SPI_DRAW_BUFFER_HEIGHT
#define LVGL_TASK_MAX_DELAY_MS CONFIG_ESP_BMS_LVGL_BRIDGE_TASK_MAX_DELAY_MS
#if CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER
#define LVGL_REQUIRE_DOUBLE_BUFFER true
#else
#define LVGL_REQUIRE_DOUBLE_BUFFER false
#endif

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_display;
static lv_indev_t *s_touch_indev;
static gpio_num_t s_backlight_pin = GPIO_NUM_NC;
static esp_bms_display_rotation_t s_rotation = ESP_BMS_DISPLAY_ROTATION_LANDSCAPE;
static uint16_t s_physical_width;
static uint16_t s_physical_height;
static bool s_touch_base_swap_xy;
static bool s_touch_base_mirror_x;
static bool s_touch_base_mirror_y;
static bool s_backlight_pwm_ready;
static bool s_initialized;
static TickType_t s_touch_last_log_tick;
static bool s_touch_read_callback_logged;
static bool s_touch_was_pressed;

static uint16_t logical_hres(const esp_bms_lvgl_bridge_config_t *config)
{
    switch (config->rotation) {
    case ESP_BMS_DISPLAY_ROTATION_LANDSCAPE:
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return config->physical_height;
    case ESP_BMS_DISPLAY_ROTATION_PORTRAIT:
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT:
    default:
        return config->physical_width;
    }
}

static uint16_t logical_vres(const esp_bms_lvgl_bridge_config_t *config)
{
    switch (config->rotation) {
    case ESP_BMS_DISPLAY_ROTATION_LANDSCAPE:
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return config->physical_width;
    case ESP_BMS_DISPLAY_ROTATION_PORTRAIT:
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT:
    default:
        return config->physical_height;
    }
}

static void rotation_flags(esp_bms_display_rotation_t rotation, bool *swap_xy, bool *mirror_x, bool *mirror_y)
{
    switch (rotation) {
    case ESP_BMS_DISPLAY_ROTATION_LANDSCAPE:
        *swap_xy = true;
        *mirror_x = true;
        *mirror_y = false;
        break;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        *swap_xy = false;
        *mirror_x = true;
        *mirror_y = true;
        break;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        *swap_xy = true;
        *mirror_x = false;
        *mirror_y = true;
        break;
    case ESP_BMS_DISPLAY_ROTATION_PORTRAIT:
    default:
        *swap_xy = false;
        *mirror_x = false;
        *mirror_y = false;
        break;
    }
}

static void touch_rotation_flags(esp_bms_display_rotation_t rotation, bool *swap_xy, bool *mirror_x, bool *mirror_y)
{
    bool swap = s_touch_base_swap_xy;
    bool x_mirror = s_touch_base_mirror_x;
    bool y_mirror = s_touch_base_mirror_y;

    switch (rotation) {
    case ESP_BMS_DISPLAY_ROTATION_PORTRAIT:
        swap = !s_touch_base_swap_xy;
        x_mirror = !s_touch_base_mirror_x;
        y_mirror = s_touch_base_mirror_y;
        break;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        swap = !s_touch_base_swap_xy;
        x_mirror = s_touch_base_mirror_x;
        y_mirror = !s_touch_base_mirror_y;
        break;
    case ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        swap = s_touch_base_swap_xy;
        x_mirror = !s_touch_base_mirror_x;
        y_mirror = !s_touch_base_mirror_y;
        break;
    case ESP_BMS_DISPLAY_ROTATION_LANDSCAPE:
    default:
        break;
    }

    *swap_xy = swap;
    *mirror_x = x_mirror;
    *mirror_y = y_mirror;
}

static void touch_coordinate_range(bool swap_xy, uint16_t hres, uint16_t vres, uint16_t *x_max, uint16_t *y_max)
{
    *x_max = swap_xy ? vres : hres;
    *y_max = swap_xy ? hres : vres;
}

static esp_err_t configure_backlight(gpio_num_t pin, int on_level)
{
    s_backlight_pin = pin;
    s_backlight_pwm_ready = false;
    if (pin == GPIO_NUM_NC) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = BACKLIGHT_PWM_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_PWM_TIMER,
        .freq_hz = BACKLIGHT_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure backlight PWM timer failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = pin,
        .speed_mode = BACKLIGHT_PWM_MODE,
        .channel = BACKLIGHT_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = on_level ? 0 : 1,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure backlight PWM channel failed");
    s_backlight_pwm_ready = true;
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_set_brightness(uint8_t percent)
{
    if (s_backlight_pin == GPIO_NUM_NC) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(s_backlight_pwm_ready, ESP_ERR_INVALID_STATE, TAG, "backlight PWM is not configured");

    if (percent > 100U) {
        percent = 100U;
    }
    const uint32_t duty = (BACKLIGHT_PWM_DUTY_MAX * (uint32_t)percent + 50U) / 100U;
    esp_err_t ret = ledc_set_duty(BACKLIGHT_PWM_MODE, BACKLIGHT_PWM_CHANNEL, duty);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(BACKLIGHT_PWM_MODE, BACKLIGHT_PWM_CHANNEL);
}

static esp_err_t set_backlight(gpio_num_t pin, int level)
{
    if (pin == GPIO_NUM_NC) {
        return ESP_OK;
    }
    return esp_bms_lvgl_bridge_set_brightness(level ? 100U : 0U);
}

static esp_err_t apply_touch_rotation(esp_bms_display_rotation_t rotation)
{
    if (!s_touch) {
        return ESP_OK;
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    touch_rotation_flags(rotation, &swap_xy, &mirror_x, &mirror_y);

    const uint16_t hres = (rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                           rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                              ? s_physical_height
                              : s_physical_width;
    const uint16_t vres = (rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                           rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                              ? s_physical_width
                              : s_physical_height;
    touch_coordinate_range(swap_xy, hres, vres, &s_touch->config.x_max, &s_touch->config.y_max);

    ESP_RETURN_ON_ERROR(esp_lcd_touch_set_swap_xy(s_touch, swap_xy), TAG, "set touch swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_set_mirror_x(s_touch, mirror_x), TAG, "set touch mirror_x failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_set_mirror_y(s_touch, mirror_y), TAG, "set touch mirror_y failed");
    ESP_LOGI(TAG, "touch rotation swap_xy=%s mirror_x=%s mirror_y=%s x_max=%u y_max=%u",
             swap_xy ? "yes" : "no",
             mirror_x ? "yes" : "no",
             mirror_y ? "yes" : "no",
             (unsigned)s_touch->config.x_max,
             (unsigned)s_touch->config.y_max);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_set_rotation(esp_bms_display_rotation_t rotation)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "bridge is not initialized");
    if (s_rotation == rotation) {
        return ESP_OK;
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    rotation_flags(rotation, &swap_xy, &mirror_x, &mirror_y);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, swap_xy), TAG, "set panel swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y), TAG, "set panel mirror failed");
    ESP_RETURN_ON_ERROR(apply_touch_rotation(rotation), TAG, "set touch rotation failed");

    s_rotation = rotation;
    if (s_display) {
        const uint16_t hres = (rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                               rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                                  ? s_physical_height
                                  : s_physical_width;
        const uint16_t vres = (rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                               rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                                  ? s_physical_width
                                  : s_physical_height;
        lv_display_set_resolution(s_display, hres, vres);
        lv_obj_invalidate(lv_screen_active());
    }
    return ESP_OK;
}

static esp_err_t touch_read_with_diagnostics(esp_lcd_touch_handle_t tp,
                                             esp_lcd_touch_point_data_t *points,
                                             uint8_t *count,
                                             uint8_t max_count,
                                             void *user_ctx)
{
    (void)user_ctx;
    if (count) {
        *count = 0;
    }

    const TickType_t now = xTaskGetTickCount();
    if (!s_touch_read_callback_logged) {
        ESP_LOGI(TAG, "touch read callback active");
        s_touch_read_callback_logged = true;
    }

    esp_err_t ret = esp_lcd_touch_read_data(tp);
    if (ret != ESP_OK) {
        if (s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(1000)) {
            ESP_LOGW(TAG, "touch read failed: %s", esp_err_to_name(ret));
            s_touch_last_log_tick = now;
        }
        return ret;
    }

    ret = esp_lcd_touch_get_data(tp, points, count, max_count);
    if (ret != ESP_OK) {
        if (s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(1000)) {
            ESP_LOGW(TAG, "touch data failed: %s", esp_err_to_name(ret));
            s_touch_last_log_tick = now;
        }
        return ret;
    }

    const bool pressed = count && *count > 0U;
    if (pressed) {
        if (!s_touch_was_pressed ||
            s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(300)) {
            ESP_LOGI(TAG, "touch sample count=%u x=%u y=%u strength=%u",
                     (unsigned)*count,
                     (unsigned)points[0].x,
                     (unsigned)points[0].y,
                     (unsigned)points[0].strength);
            s_touch_last_log_tick = now;
        }
        s_touch_was_pressed = true;
    } else if (s_touch_was_pressed) {
        ESP_LOGI(TAG, "touch released");
        s_touch_was_pressed = false;
        s_touch_last_log_tick = now;
    }

    return ESP_OK;
}

static esp_err_t init_touch(const esp_bms_lvgl_bridge_config_t *config, uint16_t hres, uint16_t vres)
{
    if (config->pin_touch_cs == GPIO_NUM_NC) {
        ESP_LOGI(TAG, "touch disabled");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "init XPT2046 touch on SPI3 clk=%d mosi=%d miso=%d cs=%d irq=%d polling=%s",
             config->pin_touch_sclk,
             config->pin_touch_mosi,
             config->pin_touch_miso,
             config->pin_touch_cs,
             config->pin_touch_irq,
             config->touch_use_irq ? "no" : "yes");

    const spi_bus_config_t touch_bus_config = {
        .sclk_io_num = config->pin_touch_sclk,
        .mosi_io_num = config->pin_touch_mosi,
        .miso_io_num = config->pin_touch_miso,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 256,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &touch_bus_config, SPI_DMA_CH_AUTO),
                        TAG, "initialize touch SPI bus failed");

    const esp_lcd_panel_io_spi_config_t touch_io_config =
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(config->pin_touch_cs);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST,
                                                &touch_io_config,
                                                &s_touch_io),
                        TAG, "create touch panel IO failed");

    s_touch_base_swap_xy = config->touch_swap_xy;
    s_touch_base_mirror_x = config->touch_mirror_x;
    s_touch_base_mirror_y = config->touch_mirror_y;

    bool touch_swap_xy = false;
    bool touch_mirror_x = false;
    bool touch_mirror_y = false;
    touch_rotation_flags(config->rotation, &touch_swap_xy, &touch_mirror_x, &touch_mirror_y);
    uint16_t touch_x_max = 0;
    uint16_t touch_y_max = 0;
    touch_coordinate_range(touch_swap_xy, hres, vres, &touch_x_max, &touch_y_max);
    const esp_lcd_touch_config_t touch_config = {
        .x_max = touch_x_max,
        .y_max = touch_y_max,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = config->touch_use_irq ? config->pin_touch_irq : GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = touch_swap_xy,
            .mirror_x = touch_mirror_x,
            .mirror_y = touch_mirror_y,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(s_touch_io, &touch_config, &s_touch),
                        TAG, "create XPT2046 touch failed");
    ESP_LOGI(TAG, "touch initial rotation swap_xy=%s mirror_x=%s mirror_y=%s x_max=%u y_max=%u",
             touch_swap_xy ? "yes" : "no",
             touch_mirror_x ? "yes" : "no",
             touch_mirror_y ? "yes" : "no",
             (unsigned)touch_x_max,
             (unsigned)touch_y_max);

    esp_lv_adapter_touch_config_t adapter_touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_display, s_touch);
    adapter_touch_config.callbacks.custom_touch_read = touch_read_with_diagnostics;
    s_touch_indev = esp_lv_adapter_register_touch(&adapter_touch_config);
    ESP_RETURN_ON_FALSE(s_touch_indev, ESP_FAIL, TAG, "register LVGL touch failed");

    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_init(const esp_bms_lvgl_bridge_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is required");
    ESP_RETURN_ON_FALSE(!s_initialized, ESP_ERR_INVALID_STATE, TAG, "bridge already initialized");
    ESP_RETURN_ON_FALSE(config->physical_width > 0 && config->physical_height > 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid display resolution");

    const uint16_t hres = logical_hres(config);
    const uint16_t vres = logical_vres(config);
    const int max_transfer_sz = hres * LVGL_SPI_DRAW_BUFFER_HEIGHT * sizeof(uint16_t);
    s_physical_width = config->physical_width;
    s_physical_height = config->physical_height;

    ESP_LOGI(TAG, "init ST7789 SPI display hres=%u vres=%u pclk=%lu",
             hres, vres, (unsigned long)config->pixel_clock_hz);
    ESP_LOGI(TAG, "LVGL adapter tuning buffer_height=%u max_transfer=%d task_max_delay_ms=%u double_buffer=%s",
             (unsigned)LVGL_SPI_DRAW_BUFFER_HEIGHT,
             max_transfer_sz,
             (unsigned)LVGL_TASK_MAX_DELAY_MS,
             LVGL_REQUIRE_DOUBLE_BUFFER ? "yes" : "no");

    ESP_RETURN_ON_ERROR(configure_backlight(config->pin_backlight, config->backlight_on_level),
                        TAG, "configure backlight failed");
    if (config->power_on_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(config->power_on_delay_ms));
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = config->pin_sclk,
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "initialize SPI bus failed");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->pin_dc,
        .cs_gpio_num = config->pin_cs,
        .pclk_hz = config->pixel_clock_hz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_panel_io),
                        TAG, "create panel IO failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->pin_reset,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel),
                        TAG, "create ST7789 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "set inversion failed");

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    rotation_flags(config->rotation, &swap_xy, &mirror_x, &mirror_y);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, swap_xy), TAG, "set panel swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y), TAG, "set panel mirror failed");
    s_rotation = config->rotation;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "turn panel on failed");

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_max_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "init LVGL adapter failed");

    esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
            s_panel, s_panel_io, hres, vres, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = LVGL_SPI_DRAW_BUFFER_HEIGHT;
    display_config.profile.require_double_buffer = LVGL_REQUIRE_DOUBLE_BUFFER;
    s_display = esp_lv_adapter_register_display(&display_config);
    ESP_RETURN_ON_FALSE(s_display, ESP_FAIL, TAG, "register adapter display failed");

    ESP_RETURN_ON_ERROR(init_touch(config, hres, vres), TAG, "init touch failed");

    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "start LVGL adapter failed");
    ESP_RETURN_ON_ERROR(set_backlight(config->pin_backlight, config->backlight_on_level),
                        TAG, "turn backlight on failed");

    s_initialized = true;
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_lock(int32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void esp_bms_lvgl_bridge_unlock(void)
{
    esp_lv_adapter_unlock();
}

lv_display_t *esp_bms_lvgl_bridge_get_display(void)
{
    return s_display;
}

lv_indev_t *esp_bms_lvgl_bridge_get_touch(void)
{
    return s_touch_indev;
}
