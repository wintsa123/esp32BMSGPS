# Wi-Fi RGB565 投屏 A/B 测试

## Goal

在当前 `ESP32-S3 + ST7796U 16-bit I80` 的 Wi-Fi 投屏链路中，允许用户在开始投屏前选择 JPEG 或完整帧 RGB565，并在相同分辨率、网络和画面条件下比较两者的有效帧率、设备解码/提交耗时与稳定性。JPEG 继续是默认模式；本任务只提供可回退的实验，不预设 RGB565 一定更快。

## Confirmed Facts

- 当前协议 v3 只接受单 JPEG frame，JPEG 收包上限为 `262144 B`；WebSocket handler 先完整接收、同步显示、再 ACK（`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3893`）。
- 当前投屏已使用完整帧、40 行同步提交与 ACK 背压；旧 v2 分块 RGB565 曾暴露逐块刷新和缓冲复用问题，不能恢复该实现（`.trellis/tasks/07-12-android-low-latency-casting/design.md`）。
- `480x320` RGB565 一帧固定为 `307200 B`；20 fps 的裸像素流约为 `6.14 MB/s`，所以实验结果可能低于 JPEG。
- 当前工作区有待验证的 JPEG `RGB565_BE` 显示色序修复；RGB565 实验必须遵循同一字节序，不得覆盖该用户改动（`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1867`）。
- Android 发送端只保留最新画面并等待每帧 ACK；该背压语义必须保持（`android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt:214`）。

## Requirements

1. Android 投屏页在设备明确声明支持时提供 `JPEG` / `RGB565` 两种模式，默认 JPEG；不支持 RGB565 的旧固件不得显示为可用模式。
2. RGB565 使用当前单完整帧 WebSocket、同步显示和 ACK 流程；不得恢复分块差分、帧队列或异步复用接收缓冲。
3. RGB565 wire payload 固定为 `RGB565_BE`，帧头仍带 v3、sequence 和 rotation；设备只接受与当前逻辑分辨率严格匹配的完整 `width * height * 2` 字节。
4. 固件为 RGB565 接收帧提供受限的最大容量，拒绝短帧、超限帧、错误类型、错误版本、非法 rotation 或错误尺寸，且错误帧不得改变当前显示内容。
5. JPEG v3 路径、当前 App 和没有 RGB565 capability 的固件保持可用；本任务不把 RGB565 设为发布默认值。
6. 固件按 codec 分开输出平均帧字节数、decode、present、total 和有效 fps，供连续 10 秒 A/B 复测。
7. 实验必须覆盖横竖屏、八色纯色、持续动态画面、主动停止和断线恢复；不得引入 panic、watchdog、内存泄漏或投屏退出后 UI 无法恢复。

## Acceptance Criteria

- [ ] 设备 capability 明确声明 RGB565 支持；App 默认 JPEG，并可在开始投屏前切换到 RGB565。
- [ ] Android 生成的 `480x320` RGB565 frame payload 恰为 `307200 B`、包含有效 v3 header，并使用 WebSocket 64-bit payload-length 编码。
- [ ] 固件只在 payload 与当前 rotation 的完整帧大小相等时提交 RGB565；协议边界测试覆盖合法、短、超限、错误版本和非法 rotation。
- [ ] JPEG 继续完成 capability 查询、投屏、ACK、方向切换和停止/断线恢复；旧 App 对新固件仍可走 JPEG。
- [ ] RGB565 的红、绿、蓝、黑、白、青、品红、黄与 Android 源画面一致，无固定通道互换、花屏或可见的旧式分块扫描。
- [ ] 同一设备、同一 Setup AP、相同 `480x320` 动态画面下，分别连续投屏至少 10 秒并记录 JPEG 与 RGB565 的 codec 标识、平均 bytes、decode、present、total、fps 和稳定性结果。
- [ ] Android 单测、固件 host selftest、Android debug 构建和目标 S3 固件构建通过；真机复测无 panic、watchdog 或内存分配失败。

## Out Of Scope

- 更改默认 JPEG 模式、自动 codec 选择、自适应码率、无损压缩、帧差分或视频编解码。
- 蓝牙传输、手机网络共存/STA 配网、iOS、触控回传和新的第三方依赖。
- 将本次 S3 色序结果外推到旧 ESP32 SPI 或其它 I80 profile。

## Risk And Rollback

RGB565 去掉 JPEG 解码，但会增加传输量；它可能更慢，这是有效实验结论而不是故障。用户可直接选择 JPEG 回退；若需移除实验，只移除 RGB565 capability、帧类型和 UI 选择，不影响既有 JPEG v3 会话。
