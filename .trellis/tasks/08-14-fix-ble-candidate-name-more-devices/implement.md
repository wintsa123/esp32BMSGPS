# Implementation Plan

1. 为 BMS/控制器构造混合 `midea`、其他名称、无名和重复 MAC 报告，检查扫描存储、名称缓存和快照投影，定位首次串写层。
2. 对确认需要修改的符号以及 BLE 候选渲染、simulator smoke 符号运行 GitNexus upstream impact；若为 HIGH/CRITICAL 先告知用户。
3. 只在首次串写的共享根因处修复，保证名称严格归属于同一 MAC。
4. 在 `settings_bms_ble_refresh_rows()` 中让每个候选始终输出一行，并附加 MAC 后两段。
5. 更新现有 simulator smoke：覆盖混合名称、同名、无名、首屏 `More devices` 实际点击和更多页候选映射。
6. 运行格式化、目标 Python/host self-tests、480x320 与 240x320 LVGL smoke。
7. 运行 GitNexus `detect-changes`，确认只影响预期 BLE/UI 符号和流程。
8. 构建 `esp32s3-n16r8-st7796u-gt1151` 完整固件。
9. 通过 `rfc2217://192.168.2.10:4000?ign_set_control`、115200 波特率烧录并监控冷启动。
10. 真机逐 MAC 对照原始广告日志与 BMS/控制器列表，验证仅真实设备显示 `midea`，并验证无名占位、`More devices` 和确认框映射。
11. 独立执行 Trellis quality check，只提交本任务相关 hunk，保留工作区既有修改。

## Validation Gates

- 任何测试、S3 构建或烧录失败均不提交。
- 真机默认未进入列表时不得启动 BLE 广告。
- `More devices` 点击不得产生 bind confirmation 日志。
- 任一非 `midea` 原始广告对应的候选显示为 `midea`，即视为验证失败。
