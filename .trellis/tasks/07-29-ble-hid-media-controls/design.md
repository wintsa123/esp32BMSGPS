# 免 App BLE HID 媒体控制设计

## Architecture

新增编译期模块 `ble-media-hid` 和开关 `ESP_BMS_FEATURE_BLE_MEDIA_HID`。它依赖 BLE，与 `phone-media` 互斥；后者保留其既有 Android companion app 和私有 GATT 协议，不在本任务改动。

模块启用时，现有 `esp_bms_idf_runtime` 仍是唯一的 NimBLE Host 所有者：负责 NVS、`nimble_port_init()`、安全参数、Host 任务、广告、手机连接与 BMS/控制器 BLE 的扫描仲裁。HID 层直接在该 Host 上注册一个标准 HID over GATT 服务，包含 HID Information、Report Map、Control Point、Report Protocol Mode 和一个 Consumer Control Input Report。

初始化顺序：runtime 初始化 GAP、注册 HID GATT 服务、配置自身 reset/sync/security 回调，最后启动现有 Host 任务。HID feature 启用时，广告携带 HID 外观和 `0x1812` 服务 UUID，继续使用同一设备名和安全配置。

## HID Contract

只声明一个 Consumer Control 输入报告。每次动作发送标准 Usage 的按下报告，约 30 ms 后发送全零释放报告：

| 触控动作 | Consumer Usage |
| --- | --- |
| 上一首 | Scan Previous Track (0x00B6) |
| 下一首 | Scan Next Track (0x00B5) |
| 播放/暂停 | Play/Pause (0x00CD) |
| 音量减 | Volume Decrement (0x00EA) |
| 音量加 | Volume Increment (0x00E9) |

触控回调只入队 HID Usage；一个小型运行时工作任务依次发送按下/释放报告，避免阻塞 LVGL 线程，也避免相邻点击把按下和释放交错。未配对、未连接或处于 HID suspend 时拒绝入队并使 UI 呈现不可用状态。

## UI And Configuration

HID 模块启用时创建一个无标题的音乐控制页，包含五个固定尺寸的触控按钮：上一首、播放/暂停、下一首、音量减、音量加。模块未启用时不创建该页。页面状态只表示 HID 配对/连接可用性，不显示歌名、播放状态或歌词。

配置器增加 `ble-media-hid` 模块目录记录：需要 `BLE` capability、与 `phone-media` 冲突。生成的 CMake feature flag 同时传给 runtime 和 LVGL UI。自定义 profile 不需要新的 GPIO。

BLE HID 触发 passkey 配对时，设备设置里的蓝牙详情页在现有“可被发现”行显示 `PIN 123456`，配对完成或失败后恢复普通发现状态文案。不要增加新的首页弹窗或独立配对页。

## Compatibility And Rollback

- 目标为经典 ESP32 与 ESP32-S3；两者都走 NimBLE BLE HID，绝不依赖 AVRCP/A2DP。
- Android 通过系统蓝牙把设备配对为输入设备；首次配对仍由用户在系统蓝牙设置中完成，之后使用 bond 自动重连。
- BMS/控制器仍作为同一 Host 的中央连接；手机 HID 连接占用本机外设连接时，既有扫描仲裁继续生效。
- 若模块被配置器取消，HID 服务、广告 UUID、音乐页和运行时工作任务均不编译进固件，恢复当前行为。

用户希望 Classic Bluetooth 可用时优先使用 Classic、不可用时降级到 BLE HID。该目标不能直接塞进当前 NimBLE HID 交付：ESP32-S3 不支持 Classic Bluetooth，且 Classic 会引入 Bluedroid/AVRCP 与现有 NimBLE Host 共存风险。当前任务保留已验证 BLE HID 作为通用 fallback；Classic ESP32-only 媒体控制应作为独立 profile/后续任务设计和验证。

## Risks

- ESP-IDF 的 `esp_hid` NimBLE 封装监听全局 GAP 连接；在本项目的中央/外设共存模式下，它可能把 BMS 或控制器连接误判成 HID 连接。因此这里直接注册标准 HOGP 服务，连接状态只取 runtime 已拥有的手机外设连接。
- 本机 ESP-IDF 路径当前失效；实现前必须恢复项目要求的 ESP-IDF 6.0.2 工具链，并以实际组件头文件/API 验证本设计。
- Android 厂商实现对 BLE HID 的配对呈现存在差异，真机验收必须覆盖 Android 系统蓝牙配对、五个 Usage 和 BMS/控制器共存。
