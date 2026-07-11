#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_FARDRIVER_FRAME_LEN 16U

typedef enum {
    ESP_FARDRIVER_LAYOUT_COMPACT = 0,
    ESP_FARDRIVER_LAYOUT_EXTENDED = 1,
} esp_fardriver_layout_t;

typedef struct {
    bool rpm_valid;
    bool speed_valid;
    bool gear_valid;
    bool power_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool controller_speed_params_valid;
    uint16_t rpm;
    uint16_t speed_deci_kmh;
    uint8_t gear;
    int32_t power_w;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint16_t wheel_circumference_mm;
    uint16_t gear_ratio_centi;
    uint16_t fallback_wheel_circumference_mm;
    uint16_t fallback_gear_ratio_centi;
    uint16_t voltage_deci_v;
    uint8_t blocks[256][2];
    uint8_t block_valid[32];
} esp_fardriver_state_t;

uint16_t esp_fardriver_crc(const uint8_t *data, size_t len);
bool esp_fardriver_parse_frame(esp_fardriver_state_t *state,
                               const uint8_t *frame,
                               size_t len,
                               esp_fardriver_layout_t layout);
void esp_fardriver_refresh_derived(esp_fardriver_state_t *state);

#ifdef __cplusplus
}
#endif

