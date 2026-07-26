# PSRAM LVGL 帧率优化

## Goal

在具备 PSRAM 的固件目标上默认启用 LVGL 的两块部分渲染缓冲，使绘制与面板 DMA 刷新可以重叠；不改变无 PSRAM 设备的默认内存预算或显示行为。

## Confirmed Facts

- 默认 ESP32-S3 板为 `esp32s3-n16r8-st7796u-gt1151`，有 8 MB Octal PSRAM，使用 320x480、横屏 I80 ST7796 面板。
- 当前 bridge 已将 LVGL draw buffer 的分配交给 `esp_lvgl_adapter`，并在 PSRAM 容量满足时通过 `profile.use_psram` 放入 PSRAM。
- 该适配器在 `require_double_buffer=true` 时分配两块 draw buffer；当前 S3 默认配置仍显式关闭该选项。
- 当前缓冲高度为 40 行，横屏下每块 RGB565 buffer 为 480 x 40 x 2 = 38,400 B；两块约占 76,800 B，远小于 S3 的 8 MB PSRAM。
- 旧 ESP32 默认无 PSRAM，且已有显式的 double-buffer 诊断 overlay；该 A/B 路径不能被自动优化破坏。
- 双缓冲只能重叠 CPU 绘制与 DMA 刷新，不能突破 I80/SPI 总线带宽或面板刷新率上限。

## Requirements

- R1: `ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER` 在启用 `SPIRAM` 的普通配置中默认开启，无 PSRAM 时默认关闭，仍允许诊断配置显式覆盖。
- R2: S3 默认 SDK 配置明确启用双缓冲；旧 ESP32 默认配置保持关闭。
- R3: PSRAM 目标只有在两块 buffer 都能容纳时才激活双缓冲；PSRAM 只能容纳一块时，继续使用一块 PSRAM buffer，不退化为两块内部 RAM buffer。
- R4: 无 PSRAM 目标若显式启用现有诊断 overlay，继续保留其内部 RAM 双缓冲 A/B 行为。
- R5: 启动日志必须区分“请求双缓冲”和“实际启用双缓冲”，并保留 PSRAM 空闲量与最大连续块观测。
- R6: 不迁移热执行代码、LVGL worker 栈或现有 ARGB8888 canvas 到 PSRAM；不改面板时钟、像素格式、I80 byte-swap 或 UI 访问锁模型。

## Acceptance Criteria

- [ ] AC1: S3 `sdkconfig.defaults` 生成的配置启用 `CONFIG_ESP_BMS_LVGL_BRIDGE_DOUBLE_BUFFER=y`，无 PSRAM 默认配置保持未启用。
- [ ] AC2: bridge 在带 PSRAM 且空间充足时向 adapter 传递 `use_psram=true` 和 `require_double_buffer=true`；空间不足时只传递单缓冲配置。
- [ ] AC3: 无 PSRAM 的显式 double-buffer 诊断配置仍向 adapter 请求双缓冲。
- [ ] AC4: 配置器自测、S3 profile 固件构建与相关静态检查通过。
- [ ] AC5: 变更通过 GitNexus 影响复核；真机可用时，启动日志显示实际 buffer/PSRAM 选择，且拖动诊断无显示异常。

## Out Of Scope

- 把可执行代码放到 PSRAM、修改 Flash/IRAM 放置策略。
- 默认使用全屏 framebuffer、增加 buffer 高度、修改 I80 时钟，或承诺固定 FPS 数值。
- 重写 LVGL UI、触摸逻辑或显示传输协议。

## Open Questions

- 无。用户已明确要求创建任务并直接实施；真机 FPS 提升将以可连接的 S3 硬件测量为准。
