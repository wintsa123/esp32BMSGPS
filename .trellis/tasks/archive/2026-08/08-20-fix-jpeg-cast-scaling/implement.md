# 实施计划

1. 对 `CastService` 中负责绘制和尺寸计算的符号执行 GitNexus upstream impact，确认调用范围。
2. 增加可单测的居中裁剪矩形计算，接入 `consumeImage` 的 `drawBitmap`，保持目标尺寸和现有帧生命周期。
3. 将 Android 与 ESP32 JPEG quality 默认/声明值统一为 80，检查 fallback 和现有测试。
4. 运行 `git diff --check`、Android 单测/构建、主机自测；执行 GitNexus `detect-changes`。
5. 实机验证横竖屏、比例不同画面、文字清晰度、延迟、停止和断线恢复；若 quality 80 延迟不可接受，仅回滚 quality。

## 主要文件

- `android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt`
- `android-cast/app/src/main/java/com/fuckingbms/cast/CastProtocol.kt`
- `android-cast/app/src/test/java/com/fuckingbms/cast/CastServiceTest.kt`（如已有测试结构可复用）
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`

## 风险门槛

- 不修改 RGB565 实验路径。
- 不增加队列或改变 ACK 背压。
- 不在未完成 impact 分析前编辑函数/方法。
