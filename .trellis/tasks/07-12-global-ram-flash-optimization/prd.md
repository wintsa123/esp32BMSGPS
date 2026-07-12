# 全工程 RAM 与 Flash 优化

## Goal

在不破坏既有设备功能、联网配网、蓝牙连接、控制器通信和 LVGL 界面的前提下，依据可复现的构建与设备运行数据，降低 ESP32 固件的 RAM 与 Flash 占用，并为后续回归持续提供内存观测手段。

## Confirmed Facts

- 目标为 ESP32 Rev.3；未启用 PSRAM。分区表的最小应用分区为 `0x1e0000`（1,966,080 B）。
- 当前 `idf.py size` 基线：Flash Code 948,988 B、Flash Data 270,984 B、IRAM 101,827 / 131,072 B（77.69%）、DRAM 56,172 / 124,580 B（45.09%）；生成的应用镜像为 1,343,715 B，分区剩余 `0x97eb0`（32%）。
- 构建配置已选择 `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`，并已限定 `CONFIG_ESP32_REV_MIN=3`；这两项不是待优化缺口。
- 工程使用 NimBLE 而非 Bluedroid；BLE 的双角色、广播/观察、GATT 客户端/服务端均已启用，最大连接数为 3，主机任务栈为 4096 B。
- Wi-Fi 已设置为较低的静态 RX 缓冲（6），但仍启用了 WPA3、SAE、OWE、企业 Wi-Fi 和 SoftAP 支持；动态 RX/TX 缓冲均为 12。
- 产品只需要 Setup AP 热点，不需要 Wi-Fi STA；热点须保留。
- BLE 必须同时连接 BMS、控制器和手机：BMS/控制器需要中央（扫描/发起连接）能力，手机需要外设（广播/GATT 服务端）能力，最大连接数不得低于 3。
- Setup AP 代码固定使用 `WIFI_AUTH_WPA2_PSK` 且最大仅一台客户端，因此 WPA3/SAE/OWE、企业 Wi-Fi 与 STA 专用能力不属于当前产品路径。
- 工程当前分区含两个 1,966,080 B OTA 应用槽和 `otadata`；代码中未发现 OTA 下载、切换或更新实现，README 将升级描述为通过手机刷写，但产品要求保留 OTA 能力与该分区布局。
- NimBLE 配置启用了 Proximity、Alert Notification、Current Time、Health Thermometer、IP Support、Tx Power、Immediate Alert、Link Loss、Scan Parameters、Heart Rate、Battery 与 Device Information 等标准服务；运行时代码只显式初始化并使用 GAP 服务，未发现这些可选标准服务的初始化或特征读写。
- 产品确认：手机只需连接设备并使用现有自定义协议；不需要 NimBLE 的电池、设备信息、心率、时间或其他可选标准服务。蓝牙名称继续由 GAP 服务提供。
- 工程已在 `main/idf_main.c`、`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` 和 LVGL bridge 中输出多类 heap 的空闲量、最低空闲量或最大连续块；动态内存观测已有基础。

## Requirements

- R1：先保留并归档可复现的静态体积基线（总体、组件、文件级），以实际收益决定优化优先级。
- R2：审查全工程的任务栈、静态 RAM、动态分配、资源常量、Wi-Fi/NimBLE 与日志配置，实施证据充分且可回归验证的优化。
- R3：不能仅为缩小体积而删除已承诺的用户功能、通信兼容性或诊断能力；任何会减少协议能力、连接数、认证方式或日志可见性的调整须先经产品范围确认。
- R4：补齐可在设备上查看的 heap 与任务栈余量观测，能够识别碎片风险与栈余量不足。
- R5：每项变更必须通过构建与相关自动化/设备验证，并记录与基线的 RAM、IRAM、Flash 差异。
- R6：保留 SoftAP、NimBLE 中央与外设角色、扫描、广播、GATT 客户端与服务端，以及至少三路并发 BLE 连接。
- R7：可移除 Wi-Fi STA、企业 Wi-Fi、WPA3/SAE/OWE 等无产品路径的构建能力，但热点仍须采用 WPA2-PSK 且支持一台客户端。
- R8：保留双 OTA 应用槽和 `otadata`，不通过调整分区表获取空间。
- R9：关闭未使用的 NimBLE 标准服务；保留 GAP、既有自定义协议、BLE 中央/外设角色、重连逻辑和至少三路连接。

## Acceptance Criteria

- [ ] AC1：保存优化前后的 `idf.py size`、`size-components` 和 `size-files` 结果，并明确主要占用来源和各项收益。
- [ ] AC2：优化后的固件可成功构建，应用镜像不超过当前基线，且不增大 IRAM/DRAM 静态占用；若个别指标因经验证的功能取舍而上升，须在结果中说明并获批准。
- [ ] AC3：设备运行时可输出内部 8-bit heap 的空闲量、历史最低值和最大连续块；关键应用任务能够报告栈高水位或等效观测值。
- [ ] AC4：Setup AP、BMS/控制器双中央连接、手机外设连接和主界面关键路径通过现有测试或真机验证。
- [ ] AC5：不启用不安全或不可维护的 IRAM 溢出策略，不以关闭错误检查替代实际内存优化。

## Out of Scope

- 未经明确决定，不引入 PSRAM 硬件依赖、不修改分区大小来掩盖镜像膨胀，也不移除用户可见功能。
- 不把 RTC 内存当作一般 RAM 扩容方案；仅在既有深睡眠恢复需求得到确认时评估。

## Open Question

- 无。
