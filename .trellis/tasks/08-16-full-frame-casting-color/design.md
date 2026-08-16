# 设计：投屏整帧显示与颜色修复

## 架构与数据流

`MediaProjection ImageReader -> Android Bitmap 原生复制/拉伸 -> JPEG quality 80 -> WebSocket v3 单帧消息 -> ESP32 PSRAM 接收缓冲 -> esp_new_jpeg RGB565_BE -> 完整帧提交 -> ACK`。

Android 捕获线程最多每 50 ms 生成一帧，使用 `Bitmap.copyPixelsFromBuffer()` 和 `Canvas.drawBitmap()` 处理行跨度与目标尺寸，避免继续用 Kotlin 逐像素转换。发送线程只读取最新 JPEG；设备 ACK 前不会发送下一帧，捕获期间产生的旧帧由最新帧覆盖。

## 协议 v3

`GET /api/cast/info` 保留物理尺寸和方向数组，新增或改为声明：

- `protocol_version: 3`
- `codec: "jpeg"`
- `jpeg_quality: 80`
- `target_fps: 20`
- `max_frame_bytes: 262144`

每个画面是一个完整 WebSocket binary message：

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | `JPEG_FRAME` type |
| 1 | 1 | protocol version `3` |
| 2 | 4 | big-endian sequence |
| 6 | 1 | display rotation `0..3` |
| 7 | remaining | baseline JPEG bytes |

心跳保持单字节 type，ACK 保持 `type + big-endian sequence`。Android WebSocket writer 补齐 RFC 6455 64-bit payload length，允许单帧消息超过 `65535 B`。固件在读取 payload 前按消息长度拒绝小于头部或大于 `262144 + 7 B` 的帧。

协议 v2 分块消息不再接受，避免旧 App 与新固件对帧提交语义产生歧义。App 通过 `/api/cast/info` 的版本检查给出不兼容错误。

## 内存与所有权

- Runtime 在首次投屏连接时从 PSRAM 懒分配一个 `262144 + 7 B` 接收缓冲，后续会话复用；启动不因未使用的投屏功能永久占用该空间，也避免逐帧 malloc/free。
- LVGL bridge 在进入投屏时从 PSRAM 分配一个 16 字节对齐的最大物理帧缓冲，`320 * 480 * 2 = 307200 B`；退出投屏时释放。
- 当前已验证的 S3 ST7796U 路径使用 `JPEG_PIXEL_FORMAT_RGB565_BE`。该缓冲只由显示任务拥有，HTTP handler 只持有同步 display command 返回前有效的压缩输入指针。
- 预计投屏新增 PSRAM 峰值约 `563 KiB`，低于真机日志显示的 `8.25 MB` 最大连续块。

Runtime 接收缓冲在首次成功分配后保留到设备生命周期结束，避免心跳超时与 HTTP handler 并发时出现释放竞态。这个固定上限是有意的简化；只有未来支持更高分辨率时才扩大或改成分块压缩输入。

## 完整帧提交与颜色

显示服务新增一个同步 `PRESENT_JPEG` 命令，携带 sequence、rotation 和 JPEG 指针/长度。Bridge 的提交顺序为：

1. 解析 JPEG header，并验证宽高等于目标 rotation 的逻辑分辨率。
2. 解码到 16 字节对齐的 RGB565_BE PSRAM 帧缓冲。
3. 解码成功后才切换临时显示方向。
4. 按现有 40 行 DMA 容量把完整帧连续提交到 panel；网络接收过程中不触碰 LCD。
5. 全部 DMA 完成后返回，Runtime 才发送 ACK。

颜色格式按最终显示提交链路区分，不按 MCU 型号分支。当前已实测的 S3 ST7796U partial-refresh 链路中，普通 LVGL flush 会先按 `OTHER` 接口规则把 native RGB565 预交换，再由 `i80_draw_bitmap_swap()` 交换到 16-bit I80 DMA staging buffer；cast 的 dummy draw 不经过该 flush 预交换，因此 decoder 必须直接输出 `RGB565_BE`，再由同一 callback 交换一次后提交。Android 只负责生成标准 JPEG，不手工定义 RGB565 线格式。

旧 ESP32 的 ST7789 SPI 路径以及启用 `I80_SWAP_COLOR_BYTES=1` 的 I80 profile 需要各自连板验证后，才能确认或调整 decoder 输出格式；不能把 S3 ST7796U 的验证结果外推成全 MCU 通用结论。

LCD 没有接入 TE 信号，因此不能做到电子意义上的垂直同步原子翻转。25 MHz 16-bit I80 全屏像素传输理论约 `6.2 ms`，连续 40 行条带提交应在一个肉眼不可见的短窗口内完成；若真机仍观察到撕裂，TE/双 GRAM 属于后续硬件级工作。

## 错误、背压与恢复

- 非 binary、协议版本错误、超限、非法 rotation、JPEG header/尺寸错误、解码失败或显示提交失败均终止本次 cast session，不 ACK 错误帧。
- 错误帧在 decode 成功前不改变 rotation 或 LCD；连接重建后第一帧天然是完整 JPEG，无差分基线恢复问题。
- ACK 是唯一背压：设备的接收、解码和显示总耗时决定发送速率；Android 捕获线程继续覆盖 `latest`，不会形成帧队列。
- 现有独占投屏的 LVGL、GPS、控制器暂停/恢复路径保持不变。

## 观测与回滚

固件按固定时间窗口输出聚合投屏指标：成功帧数、平均 JPEG bytes、平均 decode/present/total ms、有效 fps 和失败原因。Android 错误仍通过现有状态广播显示中文详情。

回滚时恢复协议 v2 分块处理、Android `FrameEncoder` 和 bridge block write；不涉及 NVS、Setup AP 凭据、用户方向或业务数据迁移。
