# 修复 ESP32-S3 蓝牙扫描列表与默认可发现性

## Goal

恢复 ESP32-S3 上 BMS 与控制器的真实 BLE 扫描候选列表，并确保设备启动后默认不对手机开放蓝牙可发现性；用户仍可从设备设置手动开启或关闭可发现性。

## Background

- 提交 `8e473d99` 为支持中文广播名，同时修改了生产 BMS 与 Controller 的 NimBLE 扫描回调。
- `bms_gap_event()` 与 `controller_scan_gap_event()` 当前只在广播名称包含中文且复制成功时存储候选；英文名、纯 ASCII 名和无名设备均被丢弃，因此常见 BMS 与 FarDriver 控制器列表为空。
- Windows simulator BLE bridge 只编入 `simulator` 目标，不进入 ESP-IDF 固件；回归来自同一提交对生产 BLE 回调的修改，不是 host bridge 直接覆盖 S3 snapshot。
- 同一提交把 `runtime_reset_state()` 中的 `BLUETOOTH_ADVERTISE_REQUESTED` 默认值从 `false` 改成 `ESP_BMS_FEATURE_BLE_MEDIA_HID != 0`，使启用 BLE HID 的 S3 启动后自动可发现。
- 现有 UI 已支持无名候选占位、候选分页和手动蓝牙可发现开关，不需要新增页面、配置项或抽象。

## Requirements

### R1 - 恢复真实扫描候选

- BMS 与 Controller 扫描回调必须按 MAC 存储所有实际发现的候选，不得以是否含中文或是否有名称作为入表条件。
- 有效广播名继续使用现有 UTF-8/ASCII 复制规则；没有可用名称时传入空名称，由现有 UI 显示稳定占位。
- 同一 MAC 的后续有名 scan response 继续补全先前无名候选，不改变 12 项上限、去重、分页、绑定和单一 NimBLE scanner 仲裁。
- Windows simulator host BLE bridge 保持 host-only，不修改其协议或复制一套固件扫描逻辑。

### R2 - 默认不开放可发现性

- `esp_bms_idf_runtime_init()` 完成后，无论 BLE media HID 是否编译启用，`BLUETOOTH_ADVERTISE_REQUESTED` 均默认为关闭。
- 启动不得因 BLE HID 功能存在而自动广播手机配对服务。
- 用户在设备设置中手动开启可发现性时，继续复用现有 action、广告启动和配对流程；手动关闭后停止广告。
- 不修改 Just Works、加密、bond、HID GATT 或已连接设备行为。

### R3 - 范围与兼容

- 保留工作区中已有的背光、UI、显示和 S3 配置改动，不回退或重写无关内容。
- 不新增依赖、BLE adapter、扫描框架、持久化字段或新 UI 文案。
- 修复同时适用于启用对应功能的 ESP32-S3 与 legacy ESP32 固件路径。

## Acceptance Criteria

- [ ] AC1：BMS 扫描收到英文/ASCII 名、中文名和无名广播时均按 MAC 出现在候选列表；名称存在时显示名称，无名时显示现有占位。
- [ ] AC2：Controller 扫描收到英文/ASCII 名、中文名和无名广播时均按 MAC 出现在候选列表，且可选择正确候选。
- [ ] AC3：同一 MAC 先无名、后有名时只保留一个候选并补全名称；候选上限、分页和 BMS/Controller 扫描接力不回归。
- [ ] AC4：ESP32-S3 冷启动后蓝牙可发现开关为关闭，手机扫描不到设备的本地 HID/配对广播。
- [ ] AC5：从设备设置手动开启后可发现和配对，手动关闭后停止广播；BMS/Controller 主动扫描仍可工作。
- [ ] AC6：现有 host self-tests、BLE bridge tests、LVGL simulator smoke、目标固件构建、`git diff --check` 和 GitNexus `detect_changes` 通过。
- [ ] AC7：在匹配 ESP32-S3 上烧录并观察启动日志、BMS/Controller 候选与手动可发现开关；无 panic、watchdog 或扫描列表再次清空。

## Out of Scope

- 修改 Windows BLE bridge 协议或桌面模拟器的设备扫描能力。
- 自动识别候选是否确实是 BMS/控制器；继续展示实际 BLE 扫描候选供用户选择。
- 新增配对页、PIN、语言文案、NVS 可发现性持久化或自动重连策略。

## Technical Notes

- `bms_gap_event` 与 `controller_scan_gap_event` 的静态 upstream impact 为 LOW 且无直接调用者，但二者是 NimBLE 注册回调，静态调用图低估了硬件影响。
- `runtime_reset_state` 的 upstream impact 为 HIGH：2 个直接调用者、4 个受影响符号、3 个流程和 2 个模块；直接覆盖 runtime 初始化、动作处理与 `app_main` 启动。
- 修改应恢复 `8e473d99` 之前的候选入表条件和广告默认值，同时保留该提交后已经完善的名称解析、双控制器 profile 与日志。
