# 实施计划：Wi-Fi RGB565 投屏 A/B 测试

## 1. Protocol And Capability

- [x] 定义 RGB565 frame type、最大完整帧容量和 parser；保留 JPEG v3 参数与边界。
- [x] 在 cast-info 增加 `rgb565_supported`，并让 Android capability parser 向后兼容缺失字段。
- [x] 扩展 C host selftest 与 Kotlin protocol/capability tests，覆盖 raw header、`307200 B` payload、64-bit WebSocket length 和拒绝路径。

## 2. Android Mode And Encoding

- [x] 在投屏开始前提供 JPEG/RGB565 互斥选择，默认 JPEG，未声明 capability 时禁用 RGB565。
- [x] 将选择传给 `CastService`；复用 existing capture/latest/ACK loop，按 codec 建帧。
- [x] 以 RGB565_BE 编码完整 target frame；不增加帧队列或第三方依赖。

## 3. Firmware Receive And Present

- [x] 增加 RGB565 WebSocket 分支与受限 PSRAM receive buffer 大小，保证无效帧不会提交。
- [x] 增加 display-service RGB565 command，并让 bridge 将 JPEG 和 raw 共享完整帧 rotation/strip present path。
- [x] 为 JPEG/RGB565 打出可区分的五秒聚合 metrics；保留现有 error/ACK/exit semantics。

## 4. Validation

- [x] `./scripts/run-host-selftests.sh`
- [x] `RUN_TESTS=1 ./scripts/build-android-cast.sh`
- [x] 构建目标 S3 profile，运行 `git diff --check` 和 GitNexus `detect-changes`。
- [ ] 通过项目 RFC2217 路径烧录目标板；分别测试 JPEG/RGB565 十秒动态画面、八色、横竖屏、停止与断线恢复，记录日志与结果。

## Risk Gates

- 先对每个被修改的符号执行 GitNexus upstream impact；若为 HIGH/CRITICAL，暂停并向用户报告。
- 不覆盖当前工作区内的 JPEG `RGB565_BE` 色序修复；raw path 必须复用它的输出契约。
- 若 RGB565 的有效帧率或稳定性更差，保留 JPEG 默认并在验收记录中报告，不为追求结果改动基线。
