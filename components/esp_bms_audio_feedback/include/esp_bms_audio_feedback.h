#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t esp_bms_audio_feedback_init(void);
void esp_bms_audio_feedback_play_volume(uint8_t volume_percent);
void esp_bms_audio_feedback_tick(void);
