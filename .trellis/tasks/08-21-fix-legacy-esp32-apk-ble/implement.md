# 实施计划

1. 对 `runtime_init_ble_host`、`runtime_bluetooth_gap_event` 和 `DeviceBleSession` 做影响分析。
2. 修复 Android Bond/加密时序、CCCD 重试和错误清理。
3. 增加最小协议/分片回归测试；保留固件 API 加密标志。
4. 运行 Android 测试与构建、固件构建或静态合同检查、`git diff --check`。
5. 运行 GitNexus `detect-changes`，确认只影响 BLE/APK 预期范围。
