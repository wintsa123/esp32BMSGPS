#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bms_controller_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* lingbo 控制器协议（移植自 Motexa 2.7.0）。
 * 帧格式与字段偏移见 docs/protocols/motexa-baseline.md 的 lingbo 小节。 */
void esp_bms_ctrl_lingbo_reset(void);
bool esp_bms_ctrl_lingbo_build_start(uint8_t attempt, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_lingbo_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_lingbo_feed(const uint8_t *chunk, size_t len);
void esp_bms_ctrl_lingbo_telemetry(esp_bms_ctrl_telemetry_t *out);

#ifdef __cplusplus
}
#endif
