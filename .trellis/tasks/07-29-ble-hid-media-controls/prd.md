# 免 App BLE HID 媒体控制

## Goal

让 ESP32 和 ESP32-S3 设备作为标准 BLE HID Consumer Control 外设配对 Android 手机。用户无需安装或运行 companion app，即可从设备音乐页触发上一首、下一首、播放/暂停、媒体音量加和媒体音量减。

## Confirmed Facts

- 当前手机媒体功能是 `phone-media` 模块，使用私有 BLE GATT 服务并依赖 `android-cast` companion app；它不符合免 App 目标。
- 当前固件使用一个 NimBLE Host，同时作为手机 BLE 外设和 BMS/控制器 BLE 中央设备；HID 必须与现有 BMS、控制器 BLE 功能共存。
- BLE HID Consumer Control 能向 Android 发送标准媒体按键，但不含歌曲标题或播放元数据；歌名、中文字体、SD 字库和歌词均不属于本任务。
- `phone-media` 已创建音乐页及四个自定义 GATT 控制动作。HID 模块不得与其同时启用，以避免两个音乐页或两套不兼容的手机媒体连接。
- ESP32-S3 构建采用 NimBLE 且不支持经典蓝牙 AVRCP；方案必须是 BLE HID，不能依赖 AVRCP/A2DP。

## Requirements

- 增加可编译裁剪的 BLE HID 媒体控制模块，要求 MCU 具有 BLE 能力。
- 配置器中该模块与 `phone-media` 互斥；未选择时不注册 HID 服务、不改变既有蓝牙广播或音乐页。
- 选中后，设备以 BLE HID Consumer Control 与 Android 系统蓝牙配对，无需 companion app、通知访问权限或自定义 GATT 客户端。
- 音乐页提供上一首、下一首、播放/暂停、媒体音量减和媒体音量加五个触控入口；不得显示或保存歌曲标题。
- 每个触控动作发送对应的标准 HID Consumer Usage 按下和释放报告；不得修改设备本地 `volume_percent`、音频反馈音量或其 NVS 设置。
- 配对、重连、断开和未配对状态必须清楚呈现，并且 HID 连接不得破坏 BMS/控制器 BLE 扫描与连接。

## Acceptance Criteria

- [ ] 配置器可选择 HID 媒体控制模块；无 BLE MCU 或同时选择 `phone-media` 时明确拒绝配置。
- [ ] 模块未选中时，不注册 HID 服务、不广播 HID 外观，且不创建音乐页。
- [ ] Android 系统蓝牙完成一次配对后，无 companion app 运行时，设备可作为媒体控制输入设备重连。
- [ ] 五个触控操作分别触发 Android 的上一首、下一首、播放/暂停、媒体音量减、媒体音量加；设备本地 `volume_percent` 不发生变化。
- [ ] 音乐页不显示歌名、歌词或来源于 Android 的媒体状态。
- [ ] HID 与 BMS/控制器 BLE 组合构建通过；配对、断开及 BMS/控制器同时使用时不出现 panic、watchdog 或不可恢复的蓝牙异常。
- [ ] 添加最小协议/报告自检、配置器自检、启用和禁用模块的构建验证，以及根目录 `preview/` 下的 UI 预览。

## Out Of Scope

- Android companion app、私有电话媒体 GATT 协议、歌名、歌词、封面和播放状态。
- 经典蓝牙 AVRCP/A2DP、向手机传输音频、同步手机和设备音量。
