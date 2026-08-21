# 修复旧 ESP32 APK 蓝牙连接功能

## 目标

旧 ESP32 与 APK 建立 BLE 连接后，APK 能读取状态、能力和配置，并能保存允许的配置；配对失败或旧设备不支持当前 GATT 服务时要给出可诊断错误。

## 范围

- 修复 Android 原生 GATT 会话的 Bond、加密、服务发现和通知订阅时序。
- 保留固件 BLE API 的加密访问边界，不开放未授权配置写入。
- 校验旧 ESP32 profile 的 BLE API 服务、配对参数和广播行为。

## 验收标准

- [ ] 已 Bond 或首次配对的设备能稳定进入 APK `CONNECTED`，随后 `/api/status` 和 `/api/settings/manifest` 请求成功。
- [ ] 配对失败、加密 CCCD 写失败、服务缺失和请求超时分别显示明确错误并清理 GATT 状态。
- [ ] 固件构建通过，BLE API 的 `WRITE_ENC`/通知加密边界不被移除。
- [ ] Android 单测/编译和协议自检通过。

## 约束

- BLE 只承载设备状态、能力和配置；BMS/控制器外设扫描仍由固件独立处理。
