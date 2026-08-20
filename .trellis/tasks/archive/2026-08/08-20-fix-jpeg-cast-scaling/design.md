# 技术设计：JPEG 投屏比例与清晰度

## 边界

只修改 Android `CastService` 的采集/绘制几何和设备投屏 capability 中的 JPEG quality。数据流保持：

`MediaProjection -> ImageReader -> 等比例居中裁剪 -> JPEG quality 80 -> WebSocket JPEG v3 -> ESP32 JPEG decoder -> RGB565 LCD`。

## 设计

- 根据实际 ImageReader 尺寸和目标 `CastTarget` 比较宽高比，计算覆盖目标比例的源裁剪矩形：裁剪较宽或较高的一侧，裁剪中心保持在画面中心。
- `Canvas.drawBitmap` 的 source rect 使用该裁剪矩形，destination rect 继续使用完整目标尺寸；两者比例相同，因此不存在非等比拉伸。
- `captureSizeFor` 继续使用覆盖目标的最小等比例采集尺寸，避免捕获无益的原生整屏像素；不改变最终目标尺寸。
- 将 `CastProtocol.DEFAULT_JPEG_QUALITY`、fallback 以及 ESP32 `/api/cast/info` 声明对齐到 80。

## 兼容性

协议仍为 v3、codec 仍为 jpeg，旧 Android 端仍可接收 quality 80；最大帧边界、方向、ACK 和 latest-wins 语义不变。

## 回滚

如实机延迟或丢帧明显增加，单独把 quality 恢复到 60；比例裁剪独立保留。
