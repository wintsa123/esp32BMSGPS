# 设计：Wi-Fi RGB565 投屏 A/B 测试

## Boundary

保持现有 v3 JPEG 会话为默认路径，增添一个 capability-gated 的完整帧 RGB565 实验路径：

`MediaProjection RGBA -> Android RGB565_BE -> WebSocket binary frame -> PSRAM receive buffer -> display service -> dummy draw strips -> ACK`。

两种 codec 都只保留最新待发送帧，均在设备提交完成后 ACK。RGB565 不使用旧 v2 的 frame begin/block/frame end 或差分基线。

## Capability And Protocol

`GET /api/cast/info` 保持 `codec: "jpeg"`、JPEG quality 和 `max_frame_bytes` 的既有含义，并增添 `rgb565_supported: true`。旧 App 忽略该字段，继续用 JPEG；新 App 仅在该字段为真时开放 RGB565。

协议版本保持 v3，新增 `RGB565_FRAME` type。其 header 与 JPEG frame 相同：type、v3、big-endian sequence、rotation，后接严格的 RGB565_BE payload。固件接收容量从 JPEG 上限扩展到 `7 + 307200 B`，但 JPEG 自身仍保留 `262144 B` 上限。raw frame 的最终大小在 bridge 根据 rotation 的实际逻辑分辨率验证，而非接受任意大 payload。

## Android

- `CastInfo` 解析 capability；投屏页在不改变默认 JPEG 的前提下提供两个互斥模式。
- `CastService` 将 mode 随 intent 传入，并复用当前 capture、缩放、latest-wins 与 ACK 发送循环。
- JPEG 延续原生 JPEG encode。RGB565 使用 `Bitmap.Config.RGB_565` 的目标 bitmap，写入可复用字节缓冲后按像素交换为 big-endian wire order；每个已发布 frame 是独立字节数组，避免 capture 线程覆盖 socket 正在发送的数据。
- `CastProtocol` 为 RGB565 frame 复用现有大 payload WebSocket framing，并在本地拒绝与所选 target 不匹配的 payload。

## Firmware

- `esp_bms_cast_protocol` 增加 RGB565 frame parser 和单独的 raw-size boundary；handler 根据 frame type 分发 JPEG 或 RGB565，同时保留心跳、会话所有权和 ACK。
- `esp_bms_display_service` 增加 `PRESENT_RGB565` 命令，仅在 cast active 时允许。
- `esp_bms_lvgl_bridge` 将 JPEG decode 后和 RGB565 input 都交给同一个完整帧 present helper：先校验 `width * height * 2`，再应用 rotation，最后以现有 40 行 dummy-draw synchronous strips 提交。这样两个 codec 的显示与颜色出口一致。
- RGB565 wire bytes 与当前 JPEG decoder 的 `RGB565_BE` 输出一致；不修改现有未提交的 JPEG 色序改动。
- 聚合日志新增 codec 标签；raw decode 时间为零，仍记录同一 total/present/fps 口径。

## Compatibility And Rollback

新固件对旧 JPEG v3 App 完全兼容。新 App 接到没有 `rgb565_supported` 的设备时仍只发送 JPEG。实验 mode 在 App 内选择，不写 NVS、不改变二维码、Setup AP 或用户显示设置。移除实验时撤销新增 capability、frame type、display command 和 mode selector 即可。

## Validation

host selftest 验证 v3 RGB565 header、边界和 ACK；Android 单测验证 capability、frame header、`307200 B` payload 与 64-bit WebSocket length。真机使用当前 S3/ST7796U，分别以 JPEG 和 RGB565 连续 10 秒复测动态画面及八色纯色；以设备日志比较，不以主观感受替代结果。
