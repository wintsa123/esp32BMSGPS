#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    ESP_BMS_AUDIO_VOICE_BMS_CONNECTED = 0,
    ESP_BMS_AUDIO_VOICE_CONTROLLER_CONNECTED,
} esp_bms_audio_voice_t;

esp_err_t esp_bms_audio_feedback_init(void);
void esp_bms_audio_feedback_play_volume(uint8_t volume_percent);
void esp_bms_audio_feedback_play_voice(esp_bms_audio_voice_t voice, uint8_t volume_percent);
void esp_bms_audio_feedback_tick(void);
