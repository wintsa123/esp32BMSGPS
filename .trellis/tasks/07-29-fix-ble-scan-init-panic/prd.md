# 修复蓝牙扫描初始化失败死机

## Goal

在 ESP32-WROOM-32E（无 PSRAM）上开启 phone-media 后，从 BMS 设置页进入扫描必须启动 NimBLE 并保持界面可用；资源不足或任一初始化步骤失败时，也不得发生 Guru Meditation 或软件复位。

## Confirmed Facts

- 现场日志在 `start-bms-bind` 后进入 `runtime_init_ble_host()`；phone-media GATT 的 `rc=3` 已修复，但 ESP32-WROOM-32E 在 `NimBLE synced` 前收到 Directed Advertising Report 时仍会软件复位。
- IDF 6.0.2 的 `ble_gatts_count_resources()` 只为无效服务定义返回 `BLE_HS_EINVAL`，且 `BLE_HS_EINVAL` 的值为 3；堆从约 25 KB 降到约 3.8 KB 是蓝牙控制器启动后的观测值，不是本次注册失败码。
- phone-media 新增一个服务、一个 notify characteristic 和一个 write characteristic。NimBLE 要求每个 characteristic 都有 `access_cb`；notify 特征已复用受加密状态写入回调。
- ESP-IDF 6.0.2 默认启用 `CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC`。其 `ble_gap_vars` 在堆不足时可能保持 NULL；Directed Advertising 的接收路径仍解引用它，导致 `EXCVADDR=0x00000006`。
- 影响分析：`runtime_init_ble_host()` 为 CRITICAL，直接入口为 `esp_bms_idf_runtime_ensure_ble_host()`；BMS 扫描、控制器扫描、设备广播与启动恢复共 6 条流程、3 个模块受到影响。
- 前一项相似缺陷已在 `bms_stop()` 通过 `BMS_BLE_READY && BMS_BLE_SYNCED` 守卫修复；本次故障发生在更早的 Host 初始化失败路径，不能在 UI 返回按钮处补丁。

## Requirements

- R1：为 phone-media 的 notify characteristic 补齐 NimBLE 必需的 `access_cb`，使媒体 GATT 服务可在 BMS 扫描前完成注册。
- R2：复用现有的状态写入回调；notify 特征不暴露 read/write 属性，协议 UUID、传输方向与安全验证均不改变。
- R3：保持既有 BMS、控制器 BLE 和设备广播的成功路径及 phone-media 协议不变。
- R4：不覆盖当前工作区中 phone-media 及其他未提交功能改动。
- R5：仅在 legacy ESP32 默认配置中关闭 NimBLE 的动态静态上下文转换，避免未完成的堆分配进入 HCI 接收路径。

## Acceptance Criteria

- [ ] AC1：功能开启的 ESP32 构建能通过，且 GATT 服务注册不再出现 `ble_gatts_count_resources rc=3`。
- [ ] AC2：从设置页启动 BMS 扫描后，不出现 Guru Meditation、StoreProhibited 或软件复位；扫描启动或明确显示失败状态。
- [ ] AC3：BMS 绑定扫描、控制器扫描、设备广播和 phone-media 的既有编译期/运行时自检通过。
- [ ] AC4：真机触发 BMS 扫描和蓝牙可发现后，日志出现 `NimBLE synced`，且不再有 `Guru Meditation` 或 `rst:0xc`。

## Out Of Scope

- 不修改 BMS 协议解析、LVGL 返回按钮行为或 Android 媒体协议。
- 不调整 BMS 协议解析、LVGL 退出路径，不引入动态服务、第二个 Host 或新依赖。
