# 免 App BLE HID 媒体控制实施计划

## Steps

1. 恢复 ESP-IDF 6.0.2 工具链，确认 NimBLE HID over GATT API 在 ESP32、ESP32-S3 构建中可用。
2. 新增 `ble-media-hid` catalog 模块并在配置器生成 `ESP_BMS_FEATURE_BLE_MEDIA_HID`；设置与 `phone-media` 的冲突，并添加配置器自检。
3. 在 runtime 注册标准 HID over GATT 服务，保持现有 Host 生命周期和安全回调所有权；广告 HID UUID/外观，维护连接、订阅与 suspend 状态。
4. 新增五个 Consumer Usage 的报告编码和串行按下/释放工作队列；添加最小 host 自检，验证 Usage 映射和释放报告。
5. 为 HID feature 添加无标题音乐控制页与五个动作；未连接时禁用动作，且不触碰本地音量状态。
6. 构建 feature 关闭/开启的 ESP32、ESP32-S3 profile，运行配置器与 host 自检，生成 LVGL 预览。
7. 使用 Android 系统蓝牙完成真机配对，验证自动重连、五个控制动作、BMS/控制器共存与断开恢复。

## Validation

- `./start.sh validate --modules ble-media-hid ...` 与 `phone-media,ble-media-hid` 的互斥失败检查。
- `./scripts/run-host-selftests.sh`，加上 HID 报告和配置器测试。
- 项目支持的 ESP-IDF 6.0.2 构建流程：模块关闭与启用的经典 ESP32、ESP32-S3 profile。
- LVGL 模拟器截图和像素检查；预览保存到 `preview/`。
- RFC2217 真机刷写后：Android 系统配对、上一首/下一首/播放暂停/音量减/音量加、重连、BMS/控制器并存。

## Rollback

移除 `ble-media-hid` 模块选择即可得到不包含 HID 服务、广告、音乐页和任务的原有固件。若 HID 注册影响 Host 初始化，停止在该步骤并恢复 feature 关闭构建，不修改现有 `phone-media` 或 BMS/控制器 BLE 行为。
