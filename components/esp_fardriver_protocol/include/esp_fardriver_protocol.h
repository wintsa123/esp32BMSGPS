#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_FARDRIVER_FRAME_LEN 16U
#define ESP_FARDRIVER_READ_REQUEST_LEN 5U
#define ESP_FARDRIVER_COMMAND_LEN 8U
/* 写入包：AA | (len-0x3C) | cmd | cmd | payload… | crc_hi | crc_lo，payload 最多 14 字节。 */
#define ESP_FARDRIVER_WRITE_PAYLOAD_MAX_LEN 14U
#define ESP_FARDRIVER_WRITE_PACKET_MAX_LEN (ESP_FARDRIVER_WRITE_PAYLOAD_MAX_LEN + 6U)
/* 求和方言（老固件）没有寄存器地址概念，frame[1] 本身就是 0..23 的块号。 */
#define ESP_FARDRIVER_SUM_BLOCK_COUNT 24U

/* 远驱控制器按帧校验方式分成两代，同一批 16 字节帧里 frame[1] 的最高位决定走哪一代：
 *   bit7 = 1 → CRC16 方言（新固件）：索引 0..55 查 FLASH_READ_ADDR 得到寄存器地址；
 *   bit7 = 0 → 16 位求和方言（老固件，例如 ND60240）：frame[1] 直接是 0..23 的块号。
 * 两代不仅校验方式不同，**同一个逻辑量的字节偏移与端序也完全不同**，所以解析必须分派，
 * 否则老固件会把电压/电流读成垃圾值（帧收得到、链路也在线，就是数值不对）。 */
typedef enum {
    ESP_FARDRIVER_DIALECT_NONE = 0,
    ESP_FARDRIVER_DIALECT_CRC16 = 1,
    ESP_FARDRIVER_DIALECT_SUM16 = 2,
} esp_fardriver_dialect_t;

typedef struct {
    bool rpm_valid;
    bool speed_valid;
    bool gear_valid;
    bool current_valid;
    bool power_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool controller_speed_params_valid;
    uint16_t rpm;
    uint16_t speed_deci_kmh;
    uint8_t gear;
    int32_t current_centi_a;
    int32_t power_w;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint8_t tire_rim_inch;
    uint8_t tire_aspect_percent;
    uint16_t tire_width_mm;
    uint16_t wheel_circumference_mm;
    uint16_t gear_ratio_centi;
    uint16_t fallback_wheel_circumference_mm;
    uint16_t fallback_gear_ratio_centi;
    uint16_t voltage_deci_v;
    uint8_t blocks[256][2];
    uint8_t block_valid[32];
    /* 诊断：对端实际在用的方言，以及两种方言各收到过多少帧。上机时看这两个
     * 计数就能判断型号走的是哪一代，不需要再猜。 */
    uint8_t dialect;
    uint32_t crc16_frame_count;
    uint32_t sum16_frame_count;
} esp_fardriver_state_t;

uint16_t esp_fardriver_crc(const uint8_t *data, size_t len);
size_t esp_fardriver_poll_address_count(void);
bool esp_fardriver_poll_address(size_t poll_index, uint8_t *address);
bool esp_fardriver_build_read_request(uint8_t address,
                                      uint8_t out[ESP_FARDRIVER_READ_REQUEST_LEN]);
bool esp_fardriver_build_open_command(uint8_t out[ESP_FARDRIVER_COMMAND_LEN]);
/* 通用 8 字节控制帧：AA | cmd | ~cmd | sub | v1 | v2 | sum | ~sum。
 * 不同固件版本用不同参数开启/复位数据流，控制器型号之间不通用。 */
bool esp_fardriver_build_control_command(uint8_t cmd, uint8_t sub, uint8_t v1, uint8_t v2,
                                         uint8_t out[ESP_FARDRIVER_COMMAND_LEN]);
bool esp_fardriver_build_keepalive_command(uint8_t out[ESP_FARDRIVER_COMMAND_LEN]);
/* 官方 App 的第二套写帧：AA | (len-0x3C) | cmd | cmd | payload… | crc_hi | crc_lo。
 * 校验与读包共用同一张 A/B 表（即 esp_fardriver_crc）。新固件（config[112] 不是
 * '1'..'6'）的连接初始化走这条路：先 write(171, {密码}) 与 write(171, {52,20})，
 * 再 write(160, {0x88, 0x03}) 或 {0x88, 0x02}。老固件则走 8 字节 old command。 */
bool esp_fardriver_build_write_packet(uint8_t cmd,
                                      const uint8_t *payload,
                                      size_t payload_len,
                                      uint8_t *out,
                                      size_t *out_len);
/* 官方 App 的完整开启序列（按顺序各发一次，用于老固件/新固件两种初始化）。
 * index 0..5 分别对应：
 *   0: write(171, {0x00, 0x00})   新固件密码（产品码为空时的哨兵值）
 *   1: write(171, {0x34, 0x14})   新固件第二段初始化
 *   2: write(160, {0x88, 0x03})   新固件开启数据流（官方 C1637ll）
 *   3: write(160, {0x88, 0x02})   新固件开启数据流（官方 C1557kl 变体）
 *   4: old command(19, 7, 1, 95)  两代通用的复位/启动
 *   5: old command(4, 111, 187, 85)  老固件（config[112] ∈ '1'..'6'）专用
 * 返回 false 表示 index 越界。 */
#define ESP_FARDRIVER_OPEN_SEQUENCE_COUNT 6U
bool esp_fardriver_build_open_sequence(uint8_t index, uint8_t *out, size_t *out_len);
bool esp_fardriver_tire_circumference_mm(uint8_t rim_inch,
                                         uint8_t aspect_percent,
                                         uint16_t width_mm,
                                         uint16_t *circumference_mm);
/* 解析一段通知数据：可传入等于或多于一帧的缓冲区（至少
 * ESP_FARDRIVER_FRAME_LEN 字节），函数按 16 字节窗口滑动定位校验正确的帧，
 * 并按 frame[1] 的最高位自动分派 CRC16 方言与求和方言两套字段布局。 */
bool esp_fardriver_parse_frame(esp_fardriver_state_t *state,
                               const uint8_t *frame,
                               size_t len);
bool esp_fardriver_link_online(const esp_fardriver_state_t *state);
bool esp_fardriver_has_instrument_telemetry(const esp_fardriver_state_t *state);
void esp_fardriver_refresh_derived(esp_fardriver_state_t *state);
/* 判定一帧属于哪一代方言（不校验内容，只看 frame[1] 最高位）。 */
esp_fardriver_dialect_t esp_fardriver_frame_dialect(const uint8_t *frame);
const char *esp_fardriver_dialect_name(esp_fardriver_dialect_t dialect);

#ifdef __cplusplus
}
#endif
