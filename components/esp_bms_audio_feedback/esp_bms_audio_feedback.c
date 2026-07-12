#include "esp_bms_audio_feedback.h"

#include <stdbool.h>
#include <string.h>

#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "bms_audio_feedback";

#define AUDIO_DAC_GPIO GPIO_NUM_26
#define AUDIO_ENABLE_GPIO GPIO_NUM_4
#define AUDIO_SAMPLE_RATE_HZ 16000U
#define AUDIO_DAC_BUFFER_SIZE 1024U
#define AUDIO_DAC_DESCRIPTOR_COUNT 4U
#define AUDIO_COMMAND_QUEUE_LENGTH 3U
#define AUDIO_BEEP_SAMPLE_COUNT 1440U
#define AUDIO_FADE_SAMPLE_COUNT 128U
#define AUDIO_VOICE_GAIN_PERCENT 125U
#define AUDIO_VOICE_PREROLL_SAMPLE_COUNT 4096U
#define AUDIO_TASK_STACK_SIZE 4096U
#define AUDIO_TASK_PRIORITY 8U
#define AUDIO_AMPLIFIER_SETTLE_MS 8U
#define AUDIO_SILENCE_SAMPLE_COUNT 160U
#define AUDIO_SILENCE_SETTLE_MS 12U

extern const uint8_t bms_connected_pcm_start[] asm("_binary_bms_connected_pcm_start");
extern const uint8_t bms_connected_pcm_end[] asm("_binary_bms_connected_pcm_end");
extern const uint8_t controller_connected_pcm_start[] asm("_binary_controller_connected_pcm_start");
extern const uint8_t controller_connected_pcm_end[] asm("_binary_controller_connected_pcm_end");

typedef enum {
    AUDIO_COMMAND_VOLUME_BEEP = 0,
    AUDIO_COMMAND_VOICE,
} audio_command_kind_t;

typedef struct {
    audio_command_kind_t kind;
    esp_bms_audio_voice_t voice;
    uint8_t volume_percent;
} audio_command_t;

static dac_continuous_handle_t s_dac;
static QueueHandle_t s_audio_command_queue;
static bool s_audio_ready;
static bool s_audio_output_enabled;
static uint8_t s_volume_beep[AUDIO_BEEP_SAMPLE_COUNT];
static uint8_t s_silence[AUDIO_DAC_BUFFER_SIZE];

static uint8_t audio_clamp_volume(uint8_t volume_percent)
{
    return volume_percent > 100U ? 100U : volume_percent;
}

static esp_err_t audio_output_enable(void)
{
    if (s_audio_output_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(AUDIO_ENABLE_GPIO, 0), TAG, "enable audio amplifier failed");
    ESP_RETURN_ON_ERROR(dac_continuous_enable(s_dac), TAG, "enable DAC channel failed");
    s_audio_output_enabled = true;
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMPLIFIER_SETTLE_MS));
    return ESP_OK;
}

static void audio_output_disable(void)
{
    if (!s_audio_output_enabled) {
        return;
    }
    memset(s_silence, 128, sizeof(s_silence));
    if (dac_continuous_write(s_dac, s_silence, AUDIO_SILENCE_SAMPLE_COUNT, NULL, -1) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(AUDIO_SILENCE_SETTLE_MS));
    }
    (void)dac_continuous_disable(s_dac);
    (void)gpio_set_level(AUDIO_ENABLE_GPIO, 1);
    s_audio_output_enabled = false;
}

static esp_err_t audio_write_pcm(const uint8_t *pcm,
                                 size_t pcm_len,
                                 uint8_t volume_percent,
                                 uint8_t gain_percent)
{
    uint8_t buffer[AUDIO_DAC_BUFFER_SIZE];
    const uint8_t volume = audio_clamp_volume(volume_percent);
    for (size_t offset = 0U; offset < pcm_len; offset += sizeof(buffer)) {
        const size_t chunk_len = (pcm_len - offset) > sizeof(buffer) ? sizeof(buffer) : pcm_len - offset;
        memset(buffer, 128, sizeof(buffer));
        for (size_t index = 0U; index < chunk_len; ++index) {
            const int16_t centered = (int16_t)pcm[offset + index] - 128;
            const size_t sample_index = offset + index;
            const size_t samples_remaining = pcm_len - sample_index - 1U;
            uint16_t gain = ((uint16_t)volume * gain_percent) / 100U;
            if (sample_index < AUDIO_FADE_SAMPLE_COUNT) {
                gain = (gain * sample_index) / AUDIO_FADE_SAMPLE_COUNT;
            }
            if (samples_remaining < AUDIO_FADE_SAMPLE_COUNT) {
                gain = (gain * samples_remaining) / AUDIO_FADE_SAMPLE_COUNT;
            }
            const int32_t output = 128 + ((int32_t)centered * gain) / 100;
            buffer[index] = (uint8_t)(output < 0 ? 0 : output > 255 ? 255 : output);
        }
        const esp_err_t ret = dac_continuous_write(s_dac, buffer, chunk_len, NULL, -1);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

static void audio_play_volume_beep(uint8_t volume_percent)
{
    if (volume_percent == 0U) {
        return;
    }
    const uint8_t amplitude = (uint8_t)(18U + ((uint16_t)audio_clamp_volume(volume_percent) * 72U) / 100U);
    for (size_t index = 0U; index < sizeof(s_volume_beep); ++index) {
        s_volume_beep[index] = ((index / 4U) & 1U) == 0U ?
                                   (uint8_t)(128U + amplitude) : (uint8_t)(128U - amplitude);
    }
    const esp_err_t ret = audio_write_pcm(s_volume_beep, sizeof(s_volume_beep), 100U, 100U);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "volume beep playback failed: %s", esp_err_to_name(ret));
    }
}

static void audio_play_voice(esp_bms_audio_voice_t voice, uint8_t volume_percent)
{
    memset(s_silence, 128, sizeof(s_silence));
    for (uint16_t offset = 0U; offset < AUDIO_VOICE_PREROLL_SAMPLE_COUNT;
         offset += AUDIO_DAC_BUFFER_SIZE) {
        if (dac_continuous_write(s_dac, s_silence, sizeof(s_silence), NULL, -1) != ESP_OK) {
            ESP_LOGW(TAG, "voice pre-roll failed");
            return;
        }
    }
    const uint8_t *pcm = voice == ESP_BMS_AUDIO_VOICE_CONTROLLER_CONNECTED
                             ? controller_connected_pcm_start
                             : bms_connected_pcm_start;
    const size_t pcm_len = voice == ESP_BMS_AUDIO_VOICE_CONTROLLER_CONNECTED
                               ? (size_t)(controller_connected_pcm_end - controller_connected_pcm_start)
                               : (size_t)(bms_connected_pcm_end - bms_connected_pcm_start);
    const esp_err_t ret = audio_write_pcm(pcm,
                                          pcm_len,
                                          volume_percent,
                                          AUDIO_VOICE_GAIN_PERCENT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "voice playback failed: %s", esp_err_to_name(ret));
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_command_t command;
    while (true) {
        if (xQueueReceive(s_audio_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const esp_err_t enable_ret = audio_output_enable();
        if (enable_ret != ESP_OK) {
            ESP_LOGW(TAG, "audio output enable failed: %s", esp_err_to_name(enable_ret));
            continue;
        }
        if (command.kind == AUDIO_COMMAND_VOLUME_BEEP) {
            audio_play_volume_beep(command.volume_percent);
        } else {
            audio_play_voice(command.voice, command.volume_percent);
        }
        audio_output_disable();
    }
}

static void audio_queue_command(audio_command_t command)
{
    if (!s_audio_ready || xQueueSend(s_audio_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "audio command dropped");
    }
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
    ESP_RETURN_ON_ERROR(gpio_set_level(AUDIO_ENABLE_GPIO, 1), TAG, "disable audio amplifier failed");

    const dac_continuous_config_t dac_config = {
        .chan_mask = DAC_CHANNEL_MASK_CH1,
        .desc_num = AUDIO_DAC_DESCRIPTOR_COUNT,
        .buf_size = AUDIO_DAC_BUFFER_SIZE,
        .freq_hz = AUDIO_SAMPLE_RATE_HZ,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    ESP_RETURN_ON_ERROR(dac_continuous_new_channels(&dac_config, &s_dac), TAG, "create DAC channel failed");

    s_audio_command_queue = xQueueCreate(AUDIO_COMMAND_QUEUE_LENGTH, sizeof(audio_command_t));
    if (!s_audio_command_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task,
                    "bms_audio",
                    AUDIO_TASK_STACK_SIZE,
                    NULL,
                    AUDIO_TASK_PRIORITY,
                    NULL) != pdPASS) {
        vQueueDelete(s_audio_command_queue);
        s_audio_command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_audio_ready = true;
    ESP_LOGI(TAG, "audio feedback ready: dac_gpio=%d enable_gpio=%d sample_rate=%u",
             AUDIO_DAC_GPIO, AUDIO_ENABLE_GPIO, AUDIO_SAMPLE_RATE_HZ);
    return ESP_OK;
}

void esp_bms_audio_feedback_play_volume(uint8_t volume_percent)
{
    if (volume_percent == 0U) {
        return;
    }
    audio_queue_command((audio_command_t){
        .kind = AUDIO_COMMAND_VOLUME_BEEP,
        .volume_percent = volume_percent,
    });
}

void esp_bms_audio_feedback_play_voice(esp_bms_audio_voice_t voice, uint8_t volume_percent)
{
    audio_queue_command((audio_command_t){
        .kind = AUDIO_COMMAND_VOICE,
        .voice = voice,
        .volume_percent = volume_percent,
    });
}

void esp_bms_audio_feedback_tick(void)
{
    /* Playback is owned by the dedicated DAC task. */
}
