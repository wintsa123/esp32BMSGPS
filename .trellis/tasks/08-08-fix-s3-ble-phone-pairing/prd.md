# 修复 ESP32-S3 手机蓝牙免 PIN 连接

## Goal

修复 ESP32-S3 蓝牙发现后手机无法连接的问题，使其与旧 ESP32 一致，无需 PIN 即可直接连接。

## Background

- 用户报告 ESP32-S3 在手机蓝牙发现后无法建立连接。
- 旧 ESP32 曾出现过同类问题，且已有可用修复行为可供对照。
- 当前 BLE HID 分支在 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` 中配置为
  `BLE_SM_IO_CAP_DISP_ONLY` 和 `sm_mitm = 1`，并在配对事件中提供固定 PIN `123456`。
- 提交 `bf10e15c` 的旧 ESP32 修复将配对统一为 `BLE_SM_IO_CAP_NO_IO`、`sm_mitm = 0`
  的 Just Works 模式；提交 `bc6b65a4` 随后为 BLE HID 恢复了 PIN 路径，导致 S3 行为偏离。

## Requirements

- ESP32-S3 的可发现蓝牙服务必须允许手机在不输入 PIN 的情况下直接连接。
- ESP32-S3 的 BLE HID 安全与配对事件处理应恢复为旧 ESP32 已验证的 Just Works 行为，
  同时保留连接后的链路加密与现有 HID 功能。
- 保持旧 ESP32 构建与连接行为不变。

## Acceptance Criteria

- [ ] 编译配置中，ESP32-S3 不再要求 PIN 或数字确认才能建立目标手机连接。
- [ ] ESP32-S3 广播发现后，手机可直接完成 Just Works 配对和连接流程。
- [ ] 连接后 BLE HID 的加密特征、订阅及媒体按键路径保持可用。
- [ ] 旧 ESP32 的蓝牙连接流程未发生回归。
- [ ] 对受影响目标完成对应的构建或静态验证。

## Out of Scope

- 不修改蓝牙协议、设备名称或手机端应用逻辑。
- 不增加新的 PIN、绑定管理或配对 UI。

## Notes

- 修改范围预计限于 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` 的
  BLE HID 安全配置和 PIN 事件处理。
- 不移除安全配对或 GATT 加密要求；仅移除需要用户输入/确认的 MITM 与 PIN 流程。
