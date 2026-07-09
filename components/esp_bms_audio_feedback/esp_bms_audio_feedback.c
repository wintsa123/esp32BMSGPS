#include "esp_bms_audio_feedback.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bms_audio_feedback";

#define AUDIO_PWM_GPIO GPIO_NUM_26
#define AUDIO_ENABLE_GPIO GPIO_NUM_4
#define AUDIO_PWM_MODE LEDC_LOW_SPEED_MODE
#define AUDIO_PWM_TIMER LEDC_TIMER_1
#define AUDIO_PWM_CHANNEL LEDC_CHANNEL_1
#define AUDIO_PWM_FREQ_HZ 1800U
#define AUDIO_PWM_MAX_DUTY 512U
#define AUDIO_PWM_MIN_AUDIBLE_DUTY 24U
#define AUDIO_VOLUME_BEEP_US 90000LL
#define AUDIO_FLAG_READY (UINT8_C(1) << 0)
#define AUDIO_FLAG_ACTIVE (UINT8_C(1) << 1)

static uint8_t s_audio_flags;
static int64_t s_audio_until_us;

static bool audio_flag_get(uint8_t flag)
{
    return (s_audio_flags & flag) != 0U;
}

static void audio_flag_set(uint8_t flag, bool enabled)
{
    if (enabled) {
        s_audio_flags |= flag;
    } else {
        s_audio_flags &= (uint8_t)~flag;
    }
}

static void audio_feedback_stop(void)
{
    if (!audio_flag_get(AUDIO_FLAG_READY) || !audio_flag_get(AUDIO_FLAG_ACTIVE)) {
        return;
    }

    (void)ledc_set_duty(AUDIO_PWM_MODE, AUDIO_PWM_CHANNEL, 0);
    (void)ledc_update_duty(AUDIO_PWM_MODE, AUDIO_PWM_CHANNEL);
    (void)gpio_set_level(AUDIO_ENABLE_GPIO, 0);
    audio_flag_set(AUDIO_FLAG_ACTIVE, false);
    s_audio_until_us = 0;
}

esp_err_t esp_bms_audio_feedback_init(void)
{
    const gpio_config_t enable_config = {
        .pin_bit_mask = 1ULL << AUDIO_ENABLE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&enable_config), TAG, "configure audio enable GPIO failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(AUDIO_ENABLE_GPIO, 0), TAG, "disable audio output failed");

    const ledc_timer_config_t timer_config = {
        .speed_mode = AUDIO_PWM_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = AUDIO_PWM_TIMER,
        .freq_hz = AUDIO_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure audio PWM timer failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = AUDIO_PWM_GPIO,
        .speed_mode = AUDIO_PWM_MODE,
        .channel = AUDIO_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = AUDIO_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure audio PWM channel failed");

    audio_flag_set(AUDIO_FLAG_READY, true);
    audio_flag_set(AUDIO_FLAG_ACTIVE, false);
    s_audio_until_us = 0;
    ESP_LOGI(TAG, "audio feedback ready: pwm_gpio=%d enable_gpio=%d freq=%u",
             AUDIO_PWM_GPIO, AUDIO_ENABLE_GPIO, AUDIO_PWM_FREQ_HZ);
    return ESP_OK;
}

void esp_bms_audio_feedback_play_volume(uint8_t volume_percent)
{
    if (!audio_flag_get(AUDIO_FLAG_READY)) {
        return;
    }

    if (volume_percent == 0U) {
        audio_feedback_stop();
        return;
    }

    if (volume_percent > 100U) {
        volume_percent = 100U;
    }

    const uint32_t duty_range = AUDIO_PWM_MAX_DUTY - AUDIO_PWM_MIN_AUDIBLE_DUTY;
    const uint32_t duty = AUDIO_PWM_MIN_AUDIBLE_DUTY +
                          ((uint32_t)volume_percent * duty_range) / 100U;
    if (ledc_set_duty(AUDIO_PWM_MODE, AUDIO_PWM_CHANNEL, duty) != ESP_OK ||
        ledc_update_duty(AUDIO_PWM_MODE, AUDIO_PWM_CHANNEL) != ESP_OK ||
        gpio_set_level(AUDIO_ENABLE_GPIO, 1) != ESP_OK) {
        audio_feedback_stop();
        return;
    }

    audio_flag_set(AUDIO_FLAG_ACTIVE, true);
    s_audio_until_us = esp_timer_get_time() + AUDIO_VOLUME_BEEP_US;
}

void esp_bms_audio_feedback_tick(void)
{
    if (audio_flag_get(AUDIO_FLAG_ACTIVE) && esp_timer_get_time() >= s_audio_until_us) {
        audio_feedback_stop();
    }
}
