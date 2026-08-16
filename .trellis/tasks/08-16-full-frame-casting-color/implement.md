# 实施计划：投屏整帧显示与颜色修复

## 1. 协议与纯逻辑测试

- [x] 将 C/Kotlin 投屏协议升级到 v3，定义单 JPEG frame header、`262144 B` 上限和保留的 heartbeat/ACK。
- [x] 更新 `/api/cast/info` 与 Android capability parser，拒绝 v2/v3 混用。
- [x] 更新固件 host selftest 和 Android unit test，覆盖合法帧、短 header、超限、错误版本/rotation、ACK sequence 及大于 65535 B 的 WebSocket 长度编码。

## 2. Android 完整帧发送

- [x] 用可复用 source/target Bitmap、`copyPixelsFromBuffer` 和 Canvas 拉伸替换运行时逐像素 RGB565 转换。
- [x] 以 quality 80、20 fps 节流生成 JPEG；超过设备上限时降到 quality 60，仍超限则丢弃该帧并保留最新画面。
- [x] 将发送状态改为单个 JPEG frame + ACK；删除差分 block hot path，保证 `latest` 覆盖旧帧而不排队。
- [x] 补齐 WebSocket client 的 64-bit payload length 写入与对应测试。

## 3. 固件接收、解码与提交

- [x] Runtime 首次投屏连接时懒分配并复用 PSRAM receive buffer，完整读取并校验单 WebSocket JPEG message。
- [x] Display service 用同步 `PRESENT_JPEG` 命令替换逐块 `WRITE_RGB565` 热路径。
- [x] LVGL bridge 进入投屏时分配 16-byte aligned RGB565 frame buffer，使用现有 `esp_new_jpeg` 解码为 RGB565_BE；dummy draw 跳过普通 LVGL flush 的预交换，再由 I80 callback 交换后提交。
- [x] 只有 JPEG header、尺寸和 decode 成功后才应用 rotation，并按 40 行 DMA 条带连续提交完整帧；全部完成后 ACK。
- [x] 退出/失败路径释放 bridge decoded buffer，保持现有 LVGL 和业务服务恢复幂等。
- [x] 增加低频聚合日志，记录 bytes、decode/present/total ms、fps 和明确失败原因。

## 4. 验证

- [x] 运行 `./scripts/run-host-selftests.sh`。
- [x] 运行 `RUN_TESTS=1 ./scripts/build-android-cast.sh`。
- [x] 构建 `cast-s3-build` 完整固件并运行 `git diff --check`。
- [x] 通过 RFC2217 烧录完整固件并捕获启动日志；确认无 panic、watchdog 或内存失败。
- [x] 当前 `ESP32-S3 + ST7796U 16-bit I80` 实机由用户确认整帧投屏速度和颜色正常。
- [ ] 通过 RFC2217 烧录，捕获启动及 10 秒持续投屏日志；确认平均有效帧率至少 18 fps、无增长队列、panic、watchdog 或内存失败。
- [ ] 真机依次显示八色纯色、彩条、渐变和高细节画面，确认无红蓝互换、字节序错色、花屏或可见分块刷新。
- [ ] 将旧 ESP32 ST7789 SPI 接入 RFC2217 后，验证投屏颜色；对 `I80_SWAP_COLOR_BYTES=1` profile 另行接板验证，不能复用 S3 ST7796U 的结果。
- [ ] 验证横竖屏切换、主动停止、断线/超限帧恢复和常规 UI 重建。
- [x] 安装最终 debug APK 到已发现的无线 ADB 设备；若手机切换到 Setup AP 导致 ADB 断开，先在 LAN 阶段完成安装。
- [x] 提交前运行 `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`，对比预期协议、Android capture、runtime、display service 和 bridge 流程。

## 风险与回滚点

- Android JPEG 实际大小超过 `262144 B`：先降质量；仍超限只丢当前帧，不扩大设备内存上限。
- ESP32-S3 decoder 输出缓冲未 16 字节对齐会出现边缘错位：只用 aligned PSRAM allocation，并以高细节图验证。
- 无 TE 只能实现约 6.2 ms 的快速整帧扫描，不能保证电子级无撕裂；若肉眼仍可见，停止继续调软件并单独规划 TE/面板双缓冲。
- 新协议不兼容旧 APK/固件；回滚必须同时回滚 Android 和固件协议，不保留双协议分支。
