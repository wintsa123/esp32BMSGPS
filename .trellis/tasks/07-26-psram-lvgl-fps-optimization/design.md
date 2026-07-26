# 技术设计

## 边界

优化只涉及 LVGL bridge 的部分渲染 buffer 选择和对应 Kconfig 默认值。可执行代码继续从 Flash/IRAM 执行；PSRAM 只承载适配器已有的 draw buffer 数据。

## 数据流

`esp_bms_lvgl_bridge_init` 读取 PSRAM heap：

1. 检查至少一块 buffer 是否可放入 PSRAM，决定 `profile.use_psram`。
2. 对启用 PSRAM 的目标，再检查两块 buffer 是否都可放入；只有满足时才令 `profile.require_double_buffer=true`。
3. 对未启用 PSRAM 的目标，保留显式 Kconfig double-buffer 请求，供既有拖动诊断使用。

适配器随后从 `MALLOC_CAP_SPIRAM` 分配一或两块 draw buffer。两块 buffer 时，LVGL 可在渲染下一块时让面板 DMA 发送上一块。

## 配置兼容性

- Kconfig 在 `SPIRAM` 启用时默认开启双缓冲，但不隐藏该开关，因此诊断 overlay 仍可在无 PSRAM 目标覆盖。
- `sdkconfig.defaults.esp32s3` 显式设为 `y`，保证已保存的 S3 profile 得到确定行为。
- 无 PSRAM 默认配置保持 `n`，不会增加旧 ESP32 的内部 RAM 压力。

## 失败与回退

- PSRAM 不足两块时：使用一块 PSRAM buffer，日志记录请求/实际状态。
- PSRAM 不可用时：PSRAM 配置目标使用一块内部 buffer；无 PSRAM 的显式诊断仍可请求两块内部 buffer。
- 回退只需恢复 S3 默认值和 Kconfig 默认值；面板、UI 和数据格式均未迁移。
