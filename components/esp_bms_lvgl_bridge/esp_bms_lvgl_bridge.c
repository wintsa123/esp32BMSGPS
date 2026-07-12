#include "esp_bms_lvgl_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
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
#include "nvs.h"

static const char *TAG = "bms_lvgl_bridge";

#define BACKLIGHT_PWM_FREQ_HZ 5000U
#define BACKLIGHT_PWM_DUTY_BITS 10U
#define BACKLIGHT_PWM_DUTY_MAX ((1U << BACKLIGHT_PWM_DUTY_BITS) - 1U)
#define BACKLIGHT_PWM_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_PWM_TIMER LEDC_TIMER_0
#define BACKLIGHT_PWM_CHANNEL LEDC_CHANNEL_0
#define LVGL_SPI_DRAW_BUFFER_HEIGHT CONFIG_ESP_BMS_LVGL_BRIDGE_SPI_DRAW_BUFFER_HEIGHT
#define LVGL_TASK_MAX_DELAY_MS CONFIG_ESP_BMS_LVGL_BRIDGE_TASK_MAX_DELAY_MS
#define TOUCH_CALIBRATION_VERSION 1U
#define TOUCH_CALIBRATION_POINT_COUNT 4U
#define TOUCH_CALIBRATION_NVS_NAMESPACE "esp_bms"
#define TOUCH_CALIBRATION_NVS_KEY "touch_cal"
#define TOUCH_FILTER_HISTORY_COUNT 3U
#define TOUCH_FILTER_MIN_SPIKE_DISTANCE 24U
#define TOUCH_FILTER_MAX_SPEED_PX_PER_MS 4U
#define TOUCH_FILTER_LOG_INTERVAL_MS 1000U
#define TOUCH_FILTER_PRESS_STABILITY_DISTANCE 16U
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
static TickType_t s_touch_last_filter_log_tick;
static bool s_touch_read_callback_logged;
static bool s_touch_was_pressed;

typedef struct {
    uint16_t x[TOUCH_FILTER_HISTORY_COUNT];
    uint16_t y[TOUCH_FILTER_HISTORY_COUNT];
    uint16_t accepted_x;
    uint16_t accepted_y;
    uint16_t candidate_x;
    uint16_t candidate_y;
    TickType_t accepted_tick;
    uint8_t count;
    bool candidate_pending;
    bool pressed;
    bool reported;
} touch_filter_t;

typedef enum {
    TOUCH_FILTER_HOLD,
    TOUCH_FILTER_ACCEPT,
    TOUCH_FILTER_REJECTED_SPIKE,
} touch_filter_result_t;

static touch_filter_t s_touch_filter;

typedef struct {
    uint32_t version;
    int32_t x_min;
    int32_t x_max;
    int32_t y_min;
    int32_t y_max;
} touch_calibration_t;

typedef struct {
    touch_calibration_t previous;
    int32_t observed_x[TOUCH_CALIBRATION_POINT_COUNT];
    int32_t observed_y[TOUCH_CALIBRATION_POINT_COUNT];
    int32_t target_x[TOUCH_CALIBRATION_POINT_COUNT];
    int32_t target_y[TOUCH_CALIBRATION_POINT_COUNT];
    uint8_t seen_mask;
    bool active;
    bool previous_valid;
} touch_calibration_session_t;

static touch_calibration_t s_touch_calibration;
static touch_calibration_session_t s_touch_calibration_session;
static bool s_touch_calibration_valid;

static void touch_rotation_flags(esp_bms_display_rotation_t rotation,
                                 bool *swap_xy,
                                 bool *mirror_x,
                                 bool *mirror_y);

static int32_t clamp_coordinate(int32_t value, int32_t maximum)
{
    if (value < 0) {
        return 0;
    }
    return value > maximum ? maximum : value;
}

static void touch_display_to_canonical(uint16_t display_x,
                                       uint16_t display_y,
                                       int32_t *canonical_x,
                                       int32_t *canonical_y)
{
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    touch_rotation_flags(s_rotation, &swap_xy, &mirror_x, &mirror_y);

    int32_t x = display_x;
    int32_t y = display_y;
    if (swap_xy) {
        const int32_t temporary = x;
        x = y;
        y = temporary;
    }
    if (mirror_x) {
        x = (int32_t)s_physical_width - x;
    }
    if (mirror_y) {
        y = (int32_t)s_physical_height - y;
    }
    *canonical_x = clamp_coordinate(x, s_physical_width);
    *canonical_y = clamp_coordinate(y, s_physical_height);
}

static void touch_canonical_to_display(int32_t canonical_x,
                                       int32_t canonical_y,
                                       uint16_t *display_x,
                                       uint16_t *display_y)
{
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    touch_rotation_flags(s_rotation, &swap_xy, &mirror_x, &mirror_y);

    int32_t x = clamp_coordinate(canonical_x, s_physical_width);
    int32_t y = clamp_coordinate(canonical_y, s_physical_height);
    if (mirror_x) {
        x = (int32_t)s_physical_width - x;
    }
    if (mirror_y) {
        y = (int32_t)s_physical_height - y;
    }
    if (swap_xy) {
        const int32_t temporary = x;
        x = y;
        y = temporary;
    }

    const int32_t display_width = (s_rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                                   s_rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                                      ? s_physical_height
                                      : s_physical_width;
    const int32_t display_height = (s_rotation == ESP_BMS_DISPLAY_ROTATION_LANDSCAPE ||
                                    s_rotation == ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE)
                                       ? s_physical_width
                                       : s_physical_height;
    *display_x = (uint16_t)clamp_coordinate(x, display_width - 1);
    *display_y = (uint16_t)clamp_coordinate(y, display_height - 1);
}

static bool touch_calibration_valid(const touch_calibration_t *calibration)
{
    if (!calibration || calibration->version != TOUCH_CALIBRATION_VERSION) {
        return false;
    }
    const int32_t x_span = calibration->x_max - calibration->x_min;
    const int32_t y_span = calibration->y_max - calibration->y_min;
    return x_span >= (int32_t)s_physical_width / 2 &&
           x_span <= (int32_t)s_physical_width * 2 &&
           y_span >= (int32_t)s_physical_height / 2 &&
           y_span <= (int32_t)s_physical_height * 2 &&
           calibration->x_min >= -(int32_t)s_physical_width &&
           calibration->x_max <= (int32_t)s_physical_width * 2 &&
           calibration->y_min >= -(int32_t)s_physical_height &&
           calibration->y_max <= (int32_t)s_physical_height * 2;
}

static int32_t touch_calibration_map(int32_t value, int32_t minimum, int32_t maximum, int32_t output_max)
{
    const int32_t span = maximum - minimum;
    if (span <= 0) {
        return clamp_coordinate(value, output_max);
    }
    const int64_t scaled = ((int64_t)value - minimum) * output_max;
    return clamp_coordinate((int32_t)(scaled / span), output_max);
}

static void touch_calibration_apply(esp_lcd_touch_point_data_t *points, uint8_t count)
{
    if (!s_touch_calibration_valid || !points) {
        return;
    }
    for (uint8_t index = 0; index < count; ++index) {
        int32_t canonical_x = 0;
        int32_t canonical_y = 0;
        touch_display_to_canonical(points[index].x, points[index].y, &canonical_x, &canonical_y);
        canonical_x = touch_calibration_map(canonical_x,
                                            s_touch_calibration.x_min,
                                            s_touch_calibration.x_max,
                                            s_physical_width - 1);
        canonical_y = touch_calibration_map(canonical_y,
                                            s_touch_calibration.y_min,
                                            s_touch_calibration.y_max,
                                            s_physical_height - 1);
        touch_canonical_to_display(canonical_x, canonical_y, &points[index].x, &points[index].y);
    }
}

static uint16_t touch_abs_difference(uint16_t left, uint16_t right)
{
    return left >= right ? left - right : right - left;
}

static uint16_t touch_median3(uint16_t first, uint16_t second, uint16_t third)
{
    if (first > second) {
        const uint16_t temporary = first;
        first = second;
        second = temporary;
    }
    if (second > third) {
        const uint16_t temporary = second;
        second = third;
        third = temporary;
    }
    return first > second ? first : second;
}

static void touch_filter_reset(void)
{
    memset(&s_touch_filter, 0, sizeof(s_touch_filter));
}

static uint16_t touch_filter_spike_limit(TickType_t now)
{
    const TickType_t elapsed_ticks = now - s_touch_filter.accepted_tick;
    const uint32_t elapsed_ms = (uint32_t)pdTICKS_TO_MS(elapsed_ticks);
    const uint32_t limit = TOUCH_FILTER_MIN_SPIKE_DISTANCE +
                           elapsed_ms * TOUCH_FILTER_MAX_SPEED_PX_PER_MS;
    return limit > UINT16_MAX ? UINT16_MAX : (uint16_t)limit;
}

static bool touch_filter_is_spike(uint16_t x, uint16_t y, uint16_t limit)
{
    const uint32_t distance = (uint32_t)touch_abs_difference(x, s_touch_filter.accepted_x) +
                              (uint32_t)touch_abs_difference(y, s_touch_filter.accepted_y);
    return distance > limit;
}

static void touch_filter_push(uint16_t x, uint16_t y)
{
    if (s_touch_filter.count < TOUCH_FILTER_HISTORY_COUNT) {
        s_touch_filter.x[s_touch_filter.count] = x;
        s_touch_filter.y[s_touch_filter.count] = y;
        s_touch_filter.count++;
        return;
    }

    s_touch_filter.x[0] = s_touch_filter.x[1];
    s_touch_filter.x[1] = s_touch_filter.x[2];
    s_touch_filter.x[2] = x;
    s_touch_filter.y[0] = s_touch_filter.y[1];
    s_touch_filter.y[1] = s_touch_filter.y[2];
    s_touch_filter.y[2] = y;
}

static touch_filter_result_t touch_filter_apply(esp_lcd_touch_point_data_t *point, TickType_t now)
{
    if (!s_touch_filter.pressed) {
        touch_filter_reset();
        s_touch_filter.pressed = true;
        s_touch_filter.candidate_x = point->x;
        s_touch_filter.candidate_y = point->y;
        return TOUCH_FILTER_HOLD;
    }

    if (!s_touch_filter.reported) {
        const uint32_t candidate_distance =
            (uint32_t)touch_abs_difference(point->x, s_touch_filter.candidate_x) +
            (uint32_t)touch_abs_difference(point->y, s_touch_filter.candidate_y);
        if (candidate_distance > TOUCH_FILTER_PRESS_STABILITY_DISTANCE) {
            s_touch_filter.candidate_x = point->x;
            s_touch_filter.candidate_y = point->y;
            return TOUCH_FILTER_HOLD;
        }

        s_touch_filter.reported = true;
        s_touch_filter.accepted_x = point->x;
        s_touch_filter.accepted_y = point->y;
        s_touch_filter.accepted_tick = now;
        touch_filter_push(s_touch_filter.candidate_x, s_touch_filter.candidate_y);
        touch_filter_push(point->x, point->y);
        return TOUCH_FILTER_ACCEPT;
    }

    const uint16_t limit = touch_filter_spike_limit(now);
    const bool spike = touch_filter_is_spike(point->x, point->y, limit);
    if (spike && (!s_touch_filter.candidate_pending ||
                  touch_abs_difference(point->x, s_touch_filter.candidate_x) +
                          touch_abs_difference(point->y, s_touch_filter.candidate_y) > limit)) {
        s_touch_filter.candidate_x = point->x;
        s_touch_filter.candidate_y = point->y;
        s_touch_filter.candidate_pending = true;
        point->x = s_touch_filter.accepted_x;
        point->y = s_touch_filter.accepted_y;
        return TOUCH_FILTER_REJECTED_SPIKE;
    }

    if (spike) {
        s_touch_filter.count = 0;
        touch_filter_push(s_touch_filter.candidate_x, s_touch_filter.candidate_y);
    }
    s_touch_filter.candidate_pending = false;
    touch_filter_push(point->x, point->y);
    if (s_touch_filter.count == TOUCH_FILTER_HISTORY_COUNT) {
        point->x = touch_median3(s_touch_filter.x[0], s_touch_filter.x[1], s_touch_filter.x[2]);
        point->y = touch_median3(s_touch_filter.y[0], s_touch_filter.y[1], s_touch_filter.y[2]);
    }
    s_touch_filter.accepted_x = point->x;
    s_touch_filter.accepted_y = point->y;
    s_touch_filter.accepted_tick = now;
    return TOUCH_FILTER_ACCEPT;
}

static bool touch_calibration_axis(const int32_t observed[TOUCH_CALIBRATION_POINT_COUNT],
                                   const int32_t target[TOUCH_CALIBRATION_POINT_COUNT],
                                   int32_t coordinate_max,
                                   int32_t *minimum,
                                   int32_t *maximum)
{
    int32_t observed_low = 0;
    int32_t observed_high = 0;
    int32_t target_low = 0;
    int32_t target_high = 0;
    uint8_t low_count = 0;
    uint8_t high_count = 0;

    for (uint8_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; ++index) {
        if (target[index] < coordinate_max / 2) {
            observed_low += observed[index];
            target_low += target[index];
            low_count++;
        } else {
            observed_high += observed[index];
            target_high += target[index];
            high_count++;
        }
    }
    if (low_count != 2U || high_count != 2U) {
        return false;
    }

    observed_low /= low_count;
    observed_high /= high_count;
    target_low /= low_count;
    target_high /= high_count;
    const int32_t observed_span = observed_high - observed_low;
    const int32_t target_span = target_high - target_low;
    if (observed_span < coordinate_max / 3 || target_span < coordinate_max / 3) {
        return false;
    }

    *minimum = observed_low - (int32_t)(((int64_t)target_low * observed_span) / target_span);
    *maximum = *minimum + (int32_t)(((int64_t)coordinate_max * observed_span) / target_span);
    return *maximum > *minimum;
}

static esp_err_t touch_calibration_save(const touch_calibration_t *calibration)
{
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(TOUCH_CALIBRATION_NVS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "open touch calibration NVS failed");
    esp_err_t ret = nvs_set_blob(handle,
                                 TOUCH_CALIBRATION_NVS_KEY,
                                 calibration,
                                 sizeof(*calibration));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static void touch_calibration_restore_previous(void)
{
    s_touch_calibration = s_touch_calibration_session.previous;
    s_touch_calibration_valid = s_touch_calibration_session.previous_valid;
    memset(&s_touch_calibration_session, 0, sizeof(s_touch_calibration_session));
}

static bool psram_can_hold_lvgl_buffers(size_t required_buffer_bytes,
                                        bool require_double_buffer,
                                        size_t *free_out,
                                        size_t *largest_out)
{
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t psram_free = heap_caps_get_free_size(psram_caps);
    const size_t psram_largest = heap_caps_get_largest_free_block(psram_caps);
    if (free_out) {
        *free_out = psram_free;
    }
    if (largest_out) {
        *largest_out = psram_largest;
    }

#if CONFIG_SPIRAM
    const size_t buffer_count = require_double_buffer ? 2U : 1U;
    return psram_largest >= required_buffer_bytes &&
           psram_free >= required_buffer_bytes * buffer_count;
#else
    (void)required_buffer_bytes;
    (void)require_double_buffer;
    return false;
#endif
}

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
        const bool was_pressed = s_touch_filter.pressed;
        touch_filter_reset();
        if (s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(1000)) {
            ESP_LOGW(TAG, "touch read failed: %s%s",
                     esp_err_to_name(ret),
                     was_pressed ? "; forcing release" : "");
            s_touch_last_log_tick = now;
        }
        return ret;
    }

    ret = esp_lcd_touch_get_data(tp, points, count, max_count);
    if (ret != ESP_OK) {
        const bool was_pressed = s_touch_filter.pressed;
        touch_filter_reset();
        if (s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(1000)) {
            ESP_LOGW(TAG, "touch data failed: %s%s",
                     esp_err_to_name(ret),
                     was_pressed ? "; forcing release" : "");
            s_touch_last_log_tick = now;
        }
        return ret;
    }

    if (count && *count > 0U) {
        touch_calibration_apply(points, *count);
        const touch_filter_result_t filter_result = touch_filter_apply(&points[0], now);
        if (filter_result == TOUCH_FILTER_HOLD) {
            *count = 0;
        } else if (filter_result == TOUCH_FILTER_REJECTED_SPIKE &&
            (s_touch_last_filter_log_tick == 0 ||
             now - s_touch_last_filter_log_tick >= pdMS_TO_TICKS(TOUCH_FILTER_LOG_INTERVAL_MS))) {
            ESP_LOGW(TAG, "touch spike rejected candidate=%u,%u held=%u,%u",
                     (unsigned)s_touch_filter.candidate_x,
                     (unsigned)s_touch_filter.candidate_y,
                     (unsigned)points[0].x,
                     (unsigned)points[0].y);
            s_touch_last_filter_log_tick = now;
        }
    } else {
        touch_filter_reset();
    }

    const bool pressed = count && *count > 0U;
    if (pressed) {
        if (!s_touch_was_pressed ||
            s_touch_last_log_tick == 0 ||
            now - s_touch_last_log_tick >= pdMS_TO_TICKS(300)) {
            ESP_LOGI(TAG, "touch sample accepted count=%u x=%u y=%u strength=%u",
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
    const size_t draw_buffer_bytes = (size_t)max_transfer_sz;
    size_t psram_free = 0;
    size_t psram_largest = 0;
    const bool use_psram_buffers = psram_can_hold_lvgl_buffers(draw_buffer_bytes,
                                                               LVGL_REQUIRE_DOUBLE_BUFFER,
                                                               &psram_free,
                                                               &psram_largest);
    s_physical_width = config->physical_width;
    s_physical_height = config->physical_height;

    ESP_LOGI(TAG, "init ST7789 SPI display hres=%u vres=%u pclk=%lu",
             hres, vres, (unsigned long)config->pixel_clock_hz);
    ESP_LOGI(TAG,
             "LVGL adapter tuning buffer_height=%u max_transfer=%d task_max_delay_ms=%u double_buffer=%s psram_free=%u psram_largest=%u psram_buffers=%s",
             (unsigned)LVGL_SPI_DRAW_BUFFER_HEIGHT,
             max_transfer_sz,
             (unsigned)LVGL_TASK_MAX_DELAY_MS,
             LVGL_REQUIRE_DOUBLE_BUFFER ? "yes" : "no",
             (unsigned)psram_free,
             (unsigned)psram_largest,
             use_psram_buffers ? "yes" : "no");

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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
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
#if CONFIG_ESP_LVGL_ADAPTER_LVGL_THREAD_STACK_IN_PSRAM
    adapter_config.stack_in_psram = use_psram_buffers;
#endif
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "init LVGL adapter failed");

    esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
            s_panel, s_panel_io, hres, vres, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.use_psram = use_psram_buffers;
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

esp_err_t esp_bms_lvgl_bridge_write_rgb565(uint16_t x,
                                           uint16_t y,
                                           uint16_t width,
                                           uint16_t height,
                                           const uint8_t *pixels,
                                           size_t pixel_bytes)
{
    ESP_RETURN_ON_FALSE(s_initialized && s_panel && s_display,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "bridge is not initialized");
    ESP_RETURN_ON_FALSE(pixels && width > 0U && height > 0U,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid RGB565 block");
    const uint16_t hres = lv_display_get_horizontal_resolution(s_display);
    const uint16_t vres = lv_display_get_vertical_resolution(s_display);
    const size_t expected = (size_t)width * height * sizeof(uint16_t);
    ESP_RETURN_ON_FALSE(pixel_bytes == expected && x < hres && y < vres &&
                            width <= hres - x && height <= vres - y,
                        ESP_ERR_INVALID_SIZE,
                        TAG,
                        "RGB565 block out of bounds");
    return esp_lcd_panel_draw_bitmap(s_panel, x, y, x + width, y + height, pixels);
}

lv_display_t *esp_bms_lvgl_bridge_get_display(void)
{
    return s_display;
}

lv_indev_t *esp_bms_lvgl_bridge_get_touch(void)
{
    return s_touch_indev;
}

esp_err_t esp_bms_lvgl_bridge_load_touch_calibration(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "bridge is not initialized");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(TOUCH_CALIBRATION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_touch_calibration_valid = false;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "open touch calibration NVS failed");

    touch_calibration_t calibration = { 0 };
    size_t size = sizeof(calibration);
    ret = nvs_get_blob(handle, TOUCH_CALIBRATION_NVS_KEY, &calibration, &size);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_touch_calibration_valid = false;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "load touch calibration failed");
    if (size != sizeof(calibration) || !touch_calibration_valid(&calibration)) {
        s_touch_calibration_valid = false;
        ESP_LOGW(TAG, "ignoring invalid touch calibration blob");
        return ESP_OK;
    }

    s_touch_calibration = calibration;
    s_touch_calibration_valid = true;
    ESP_LOGI(TAG, "touch calibration loaded x=%ld..%ld y=%ld..%ld",
             (long)calibration.x_min,
             (long)calibration.x_max,
             (long)calibration.y_min,
             (long)calibration.y_max);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_begin_touch_calibration(void)
{
    ESP_RETURN_ON_FALSE(s_initialized && s_touch, ESP_ERR_INVALID_STATE, TAG, "touch is not initialized");
    ESP_RETURN_ON_FALSE(!s_touch_calibration_session.active,
                        ESP_ERR_INVALID_STATE, TAG, "touch calibration already active");

    memset(&s_touch_calibration_session, 0, sizeof(s_touch_calibration_session));
    s_touch_calibration_session.previous = s_touch_calibration;
    s_touch_calibration_session.previous_valid = s_touch_calibration_valid;
    s_touch_calibration_session.active = true;
    s_touch_calibration_valid = false;
    ESP_LOGI(TAG, "touch calibration started");
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_bridge_add_touch_calibration_sample(uint8_t target_index,
                                                           uint16_t observed_x,
                                                           uint16_t observed_y,
                                                           uint16_t target_x,
                                                           uint16_t target_y,
                                                           bool *finished)
{
    ESP_RETURN_ON_FALSE(finished, ESP_ERR_INVALID_ARG, TAG, "finished output is required");
    *finished = false;
    ESP_RETURN_ON_FALSE(s_touch_calibration_session.active,
                        ESP_ERR_INVALID_STATE, TAG, "touch calibration is not active");
    ESP_RETURN_ON_FALSE(target_index < TOUCH_CALIBRATION_POINT_COUNT,
                        ESP_ERR_INVALID_ARG, TAG, "invalid touch calibration target");

    touch_display_to_canonical(observed_x,
                               observed_y,
                               &s_touch_calibration_session.observed_x[target_index],
                               &s_touch_calibration_session.observed_y[target_index]);
    touch_display_to_canonical(target_x,
                               target_y,
                               &s_touch_calibration_session.target_x[target_index],
                               &s_touch_calibration_session.target_y[target_index]);
    s_touch_calibration_session.seen_mask |= (uint8_t)(1U << target_index);
    ESP_LOGI(TAG, "touch calibration point=%u observed=%u,%u target=%u,%u",
             (unsigned)target_index,
             (unsigned)observed_x,
             (unsigned)observed_y,
             (unsigned)target_x,
             (unsigned)target_y);

    if (s_touch_calibration_session.seen_mask !=
        (uint8_t)((1U << TOUCH_CALIBRATION_POINT_COUNT) - 1U)) {
        return ESP_OK;
    }

    touch_calibration_t calibration = {
        .version = TOUCH_CALIBRATION_VERSION,
    };
    const bool valid = touch_calibration_axis(s_touch_calibration_session.observed_x,
                                              s_touch_calibration_session.target_x,
                                              s_physical_width,
                                              &calibration.x_min,
                                              &calibration.x_max) &&
                       touch_calibration_axis(s_touch_calibration_session.observed_y,
                                              s_touch_calibration_session.target_y,
                                              s_physical_height,
                                              &calibration.y_min,
                                              &calibration.y_max) &&
                       touch_calibration_valid(&calibration);
    if (!valid) {
        touch_calibration_restore_previous();
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = touch_calibration_save(&calibration);
    if (ret != ESP_OK) {
        touch_calibration_restore_previous();
        return ret;
    }

    s_touch_calibration = calibration;
    s_touch_calibration_valid = true;
    memset(&s_touch_calibration_session, 0, sizeof(s_touch_calibration_session));
    *finished = true;
    ESP_LOGI(TAG, "touch calibration saved x=%ld..%ld y=%ld..%ld",
             (long)calibration.x_min,
             (long)calibration.x_max,
             (long)calibration.y_min,
             (long)calibration.y_max);
    return ESP_OK;
}

void esp_bms_lvgl_bridge_cancel_touch_calibration(void)
{
    if (!s_touch_calibration_session.active) {
        return;
    }
    touch_calibration_restore_previous();
    ESP_LOGI(TAG, "touch calibration cancelled");
}

esp_err_t esp_bms_lvgl_bridge_reset_touch_calibration(void)
{
    if (s_touch_calibration_session.active) {
        memset(&s_touch_calibration_session, 0, sizeof(s_touch_calibration_session));
    }
    memset(&s_touch_calibration, 0, sizeof(s_touch_calibration));
    s_touch_calibration_valid = false;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(TOUCH_CALIBRATION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "open touch calibration NVS failed");
    ret = nvs_erase_key(handle, TOUCH_CALIBRATION_NVS_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}
