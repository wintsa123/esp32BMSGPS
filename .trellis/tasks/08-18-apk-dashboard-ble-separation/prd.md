# APK 仪表与 BMS/控制器蓝牙分离

## Goal

修复 APK 导航与蓝牙设备选择混用问题，让 BMS、控制器和控制器数据仪表成为可独立使用的入口；同时明确手机 BLE 扫描与单片机 BLE 扫描的边界，并为“手机选择 MAC 后保存到单片机”定义可验证的通信路径。

## Confirmed Facts

- APK 反编译源码位于 `.yy-apk-analysis.5U6GAR/sources/sources/com/yybms`。
- `MainFragment_bluetoothdisplay` 使用 `com.inuker.bluetooth.library` 的 `SearchRequest` / `SearchResponse` 调用手机 Android BLE 扫描；它维护最多 15 个名称、RSSI 和 MAC，并直接使用扫描结果更新 APK 页面。
- APK 的 `BluetoothLeService` 使用 Android `BluetoothGatt` 连接、发现服务、订阅通知和接收数据，说明 APK 当前能直接连接 BLE 外设。
- 固件 `esp_bms_bms_ble` 和 `esp_bms_controller_ble` 使用 NimBLE 扫描并分别维护 `bms_scan_candidates`、`controller_scan_candidates`；两类候选在固件快照中已有独立数组和独立绑定 MAC。
- 固件 LVGL 设置页当前仍共用“蓝牙连接”列表渲染函数，但通过 `settings_ble_source_t` 区分 BMS/Controller；现有任务已包含候选分页和控制器仪表相关工作，需避免重复覆盖其边界。
- 固件已有控制器遥测状态和控制器仪表页面实现基础，包含速度、挡位、功率、RPM、控制器温度和电机温度；本任务不修改 TFT，仅在 APK 重新设计控制器仪表。
- 仓库没有可直接编译的 Android Gradle 工程，只有多个 APK 成品和 `.yy-apk-analysis.5U6GAR` 反编译源码；APK 产出能力必须在实施前确认。
- `MainFragment_bluetoothdisplay` 已通过 BLE 写入固定格式命令把扫描到的 MAC 发给单片机，说明手机扫描结果可以经 BLE 下发；但现有命令语义和 BMS/Controller 类型隔离需要在实施时复核。

## Requirements

### R1 - APK 导航命名

- 将 APK 中当前“BMS”导航入口改名为“仪表”。
- 导航改名不得改变既有 BMS 设置和数据读取协议。

### R2 - BMS 与控制器蓝牙列表独立

- 扫描 BMS 和扫描控制器必须有独立入口、独立列表状态和独立已保存 MAC。
- 选择控制器不得覆盖 BMS MAC；选择 BMS 不得覆盖控制器 MAC。
- 列表应显示名称、MAC、连接/选择状态，并保持现有 BLE 权限、停止扫描和连接错误处理。

### R3 - APK 控制器数据仪表

- 在 APK 中增加并重新设计控制器数据显示入口/页面，至少显示协议能稳定提供的速度、挡位、电流或功率、RPM、控制器温度、电机温度；无效字段显示占位符，不伪造数据。
- 仪表页面与 BMS 保护板详情页视觉和数据状态独立；导航名称“仪表”指向仪表集合，不再把 BMS 扫描列表作为唯一内容。
- 仪表数据必须来自控制器 BLE 数据链路；不得把 BMS 数据误显示为控制器数据。

### R4 - 手机扫描 MAC 写入单片机

- 先确认当前 APK 与单片机之间使用的传输方式和现有命令合同。
- 若已有可靠的设备通信通道，则增加“选择手机扫描到的设备 -> 下发 MAC -> 单片机校验并持久化 -> 返回结果”的闭环。
- 单片机必须校验 MAC 格式、按 BMS/Controller 类型分别保存，并在失败时不覆盖旧值。
- 若当前没有可复用的写入协议，本任务只输出技术结论和最小协议设计，不假装已完成保存。

## Acceptance Criteria

- [ ] APK 导航显示“仪表”，原 BMS 功能仍可进入并工作。
- [ ] BMS 与控制器扫描页面不共享候选数据或绑定 MAC；切换页面、刷新、取消和重进页面不会串列表。
- [ ] 控制器仪表能显示真实控制器数据；断连或字段无效时安全显示占位符。
- [ ] 能明确记录当前 BLE 扫描主体：手机 APK 扫描、单片机固件扫描，或两者并存；连接主体与数据来源一致。
- [ ] 若实现 MAC 下发，手机选择后的 MAC 经设备端校验、按类型持久化，重启后仍可读回；通信失败不破坏旧绑定。
- [ ] 相关 APK/固件构建或静态检查通过，且 GitNexus 影响范围与实际改动一致。

## Out Of Scope

- 新增未确认的 BMS 或控制器协议解析。
- 将手机扫描结果无条件自动写入单片机而不经过用户确认、类型选择和格式校验。
- 重写已有 NimBLE 扫描仲裁、FarDriver 解析或 LVGL 控制器仪表布局。

## Open Question

