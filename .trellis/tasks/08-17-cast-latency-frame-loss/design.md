# 设计：低延迟 JPEG 投屏

## Boundary

保留现有 JPEG v3、最新帧覆盖和 ACK 背压。优化只覆盖 Android 采集/编码与 ESP32-S3 JPEG 解码生命周期：

`手机物理屏幕 -> 等比例缩小的 MediaProjection -> 现有 Canvas 满屏拉伸 -> JPEG quality 60 -> WebSocket -> 复用 JPEG decoder -> 40 行 I80 提交 -> ACK`

不增加帧队列、不切换 WebP/H.264/RGB565，也不改投屏页或设备页的现有未提交 UI 工作。

## Android Capture And Metrics

`CastService.configureCapture()` 不再以手机原生宽高创建 `ImageReader`。它仍先读取物理屏幕方向并选择 `CastTarget`，再计算保留手机原始宽高比、同时覆盖目标面板宽高的最小采集尺寸：

`scale = max(targetWidth / sourceWidth, targetHeight / sourceHeight)`

尺寸向上取整，以免因舍入而小于目标。现有 `Canvas.drawBitmap()` 继续将这个等比例采集图满屏拉伸到设备目标尺寸，故保持当前画面裁剪/拉伸语义，却避免每帧复制和缩放原生整屏。

每个不可变 `CapturedFrame` 记录采集完成时间和编码耗时。发送线程在 ACK 后按固定窗口记录：已发送数、被最新帧覆盖数、平均编码耗时、发送到 ACK 耗时、ACK 时帧龄和最大帧龄。统计只由发送线程累积，避免在 capture 与 socket 线程之间添加锁或队列。日志用于和固件现有 `[cast]` 指标配对分析，不新增 UI。

## Firmware Decode And Codec

设备仍返回 JPEG v3 与 `target_fps: 20`，但将 `jpeg_quality` 声明为 60。Android 已接受 60..100，故不需要协议升级或兼容分支。

`esp_bms_lvgl_bridge` 在投屏会话内缓存 `jpeg_dec_handle_t`。首次帧或逻辑宽高变化时以 `RGB565_BE` 打开 decoder；相同宽高的连续帧只重新解析 header 并处理。ESP_NEW_JPEG 的本地 stream 示例明确支持同尺寸连续 decode。投屏退出时关闭 decoder，然后按现有顺序关闭 dummy-draw 和释放 RGB565 帧缓冲。显示服务已经串行处理 `PRESENT_JPEG`，因此该句柄没有并发所有者。

方向变化会使 320x480 与 480x320 互换，属于 decoder 重建边界；错误帧会终止会话并进入既有恢复流程，不需要在错误会话内尝试复位 decoder。

## Trade-offs And Rollback

- quality 60 减少 JPEG 字节数和 Android 编码工作，代价是轻微压缩伪影；真机可读性不合格时只恢复 80。
- 采集尺寸缩小不改变最终目标分辨率或满屏拉伸规则，代价是不会保留最终 LCD 无法显示的源屏细节。
- decoder 复用减少每帧创建/销毁开销，代价是增加会话内一个受显示服务独占的句柄；尺寸切换和退出都有明确释放点。
- 40 行 I80 条带维持不变，因为当前 `cast-s3-build` DMA 缓冲恰为 40 行；扩大它会改变内存配置，超出本次最小优化。

回滚只需恢复 capability 中的 quality 80、原生采集尺寸和逐帧 decoder open/close；协议、NVS、Setup AP 和屏幕颜色路径均不变。
