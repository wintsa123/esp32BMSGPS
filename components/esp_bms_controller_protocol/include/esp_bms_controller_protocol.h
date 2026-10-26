#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 控制器协议框架：每个型号一个纯协议模块（build_start / build_poll / feed / telemetry），
 * 传输层（esp_bms_controller_ble）只负责 GATT 连接、订阅和按节拍调用这些函数。
 * 帧结构与字段偏移来自 Motexa 2.7.0 的控制器适配器实现（见 docs/protocols 基线文档）。 */

#define ESP_BMS_CTRL_FRAME_MAX_LEN 128U
#define ESP_BMS_CTRL_UUID128_LEN 16U

typedef enum {
    /* 0 = 未指定：保留既有 FarDriver 自动探测路径 */
    ESP_BMS_CTRL_TYPE_AUTO = 0,
    ESP_BMS_CTRL_TYPE_FARDRIVER = 1,
    ESP_BMS_CTRL_TYPE_BLUESTONE = 2,
    ESP_BMS_CTRL_TYPE_YUANQU_FOC = 3,
    ESP_BMS_CTRL_TYPE_LINGBO = 4,
    ESP_BMS_CTRL_TYPE_DATAI = 5,
    ESP_BMS_CTRL_TYPE_SUNIU = 6,
    ESP_BMS_CTRL_TYPE_YANXIN = 7,
    ESP_BMS_CTRL_TYPE_APT_A8 = 8,
    ESP_BMS_CTRL_TYPE_APT_LEGACY = 9,
    ESP_BMS_CTRL_TYPE_DUNEN = 10,
    ESP_BMS_CTRL_TYPE_ZHIKE = 11,
    ESP_BMS_CTRL_TYPE_JH = 12,
    ESP_BMS_CTRL_TYPE_AUSI = 13,
    ESP_BMS_CTRL_TYPE_JIAZHOU = 14,
    ESP_BMS_CTRL_TYPE_XINDA = 15,
    ESP_BMS_CTRL_TYPE_HOBBYWING = 16,
    ESP_BMS_CTRL_TYPE_COUNT,
} esp_bms_ctrl_type_t;

/* 控制器遥测：各型号能提供的字段不同，用 *_valid 标记有效位。
 * 传输层在 feed() 返回 true 后调用 telemetry() 取一份快照。 */
typedef struct {
    bool rpm_valid;
    bool speed_valid;
    bool gear_valid;
    bool current_valid;
    bool power_valid;
    bool voltage_valid;
    bool controller_temp_valid;
    bool motor_temp_valid;
    bool throttle_valid;
    bool fault_valid;
    bool controller_speed_params_valid;
    uint16_t rpm;
    uint16_t speed_deci_kmh;
    uint8_t gear;
    int32_t current_centi_a;
    int32_t power_w;
    uint16_t voltage_deci_v;
    int16_t controller_temp_c;
    int16_t motor_temp_c;
    uint8_t throttle_percent;
    uint16_t fault_code;
    uint8_t tire_rim_inch;
    uint8_t tire_aspect_percent;
    uint16_t tire_width_mm;
    uint16_t wheel_circumference_mm;
    uint16_t gear_ratio_centi;
    /* 本地配置侧的回退速度参数（不是协议数据，由 runtime 写入，快照投影用） */
    uint16_t fallback_wheel_circumference_mm;
    uint16_t fallback_gear_ratio_centi;
} esp_bms_ctrl_telemetry_t;

typedef struct {
    uint8_t type;
    const char *id;       /* Motexa 协议 id，例如 "lingbo_ble" */
    const char *label_zh; /* 设置页中文标签 */
    const char *label_en; /* 设置页英文标签 */
    /* GATT 绑定：uuid128 为真时用三个 16 字节 UUID，否则用三个 16 位 UUID。
     * 全部为 0 表示该型号依赖全服务探测（Motexa 的 null UUID 型号）。 */
    bool uuid128;
    const uint8_t *service_uuid;
    const uint8_t *notify_uuid;
    const uint8_t *write_uuid;
    uint16_t service_uuid16;
    uint16_t notify_uuid16;
    uint16_t write_uuid16;
    uint16_t requested_mtu;
    /* 首帧前重发开启帧的周期；在线后的轮询周期 */
    uint16_t start_retry_ms;
    uint16_t poll_period_ms;
    /* 协议实现 */
    void (*reset)(void);
    bool (*build_start)(uint8_t attempt, uint8_t *out, size_t *out_len);
    bool (*build_poll)(uint16_t step, uint32_t elapsed_ms, uint8_t *out, size_t *out_len);
    bool (*feed)(const uint8_t *chunk, size_t len);
    void (*telemetry)(esp_bms_ctrl_telemetry_t *out);
} esp_bms_ctrl_protocol_t;

const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_get(uint8_t type);
const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_find_by_id(const char *id);
size_t esp_bms_ctrl_protocol_count(void);
const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_at(size_t index);
/* 从 BLE 广播名/服务 UUID 猜测型号；返回 NULL 表示无法判断。 */
const esp_bms_ctrl_protocol_t *esp_bms_ctrl_protocol_detect(const char *name,
                                                           const uint8_t *service_uuid128,
                                                           uint16_t service_uuid16);
/* 型号文本 <-> 枚举（NVS 与 Web 配置共用）。 */
const char *esp_bms_ctrl_type_config_text(uint8_t type);
bool esp_bms_ctrl_type_from_config_text(const char *text, uint8_t *out_type);

/* 便捷：调用引擎 reset 并清空遥测。 */
void esp_bms_ctrl_reset(const esp_bms_ctrl_protocol_t *protocol);

/* 按转速 + 轮周长/齿比重算速度，并按电压×电流重算功率。
 * 设置页改动轮径/齿比后由 runtime 调用，让界面立即生效。 */
void esp_bms_ctrl_refresh_derived(esp_bms_ctrl_telemetry_t *state);

#ifdef __cplusplus
}
#endif
