#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bms_controller_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FarDriver（远驱）控制器协议适配层：底层帧构造/解析仍由 esp_fardriver_protocol 提供，
 * 这里只把它的状态机接到统一的控制器协议接口上（开启指令轮换 + 保活/读包交替）。 */
void esp_bms_ctrl_fardriver_reset(void);
bool esp_bms_ctrl_fardriver_build_start(uint8_t attempt, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_fardriver_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_fardriver_feed(const uint8_t *chunk, size_t len);
void esp_bms_ctrl_fardriver_telemetry(esp_bms_ctrl_telemetry_t *out);

#ifdef __cplusplus
}
#endif
