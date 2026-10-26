#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bms_controller_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* yanxin 控制器协议（移植自 Motexa 2.7.0 的 v6.h1）。
 *
 * 帧格式：0xAA | len(=payload+4) | seq | flags | payload[len-4] | crcLo | crcHi
 * - CRC 为 CRC16/MODBUS，覆盖 [1 .. payload+3]（即除帧头 0xAA 外的全部内容）。
 * - flags bit1(0x02) = 逻辑包后续还有分片，seq 逐片递增，首片 seq 必须为 0。
 * - 首帧为“密码握手”：payload = 01 | scheme | pw0..pw3 | custHi | custLo。
 *   scheme 在 0x1F/0x0F 两种密码方案间轮换，cust 为 16 位 customerId 寄存器地址
 *   （Motexa 依次探测 0..4）；控制器用 (01,00,0x?F,err) 表示拒绝/换地址，
 *   用 (01,scheme,...) 表示握手成功。密码本身就在这一帧里，单帧即可完成登录。 */

void esp_bms_ctrl_yanxin_reset(void);
bool esp_bms_ctrl_yanxin_build_start(uint8_t attempt, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_yanxin_build_poll(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len);
bool esp_bms_ctrl_yanxin_feed(const uint8_t *chunk, size_t len);
void esp_bms_ctrl_yanxin_telemetry(esp_bms_ctrl_telemetry_t *out);

/* 可选：设置 4 位 ASCII 可见密码（0x21..0x7E）。不设置时按无密码握手（Motexa 的
 * 只读路径同样支持无密码），此时握手帧密码位全 0。传 NULL 表示清除密码。
 * 该入口不属于协议契约，传输层不调用也能工作；参数非法时清除密码。 */
void esp_bms_ctrl_yanxin_set_password(const uint8_t *ascii4);

#ifdef __cplusplus
}
#endif
