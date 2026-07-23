# 执行计划：ESP32 启动期 WDT 修复

1. [x] 使用 GitNexus impact 分析 `bluetoothon`、`wlanJZ`、`hotspoton` 和使用它们的 UI 初始化路径；影响风险 LOW。
2. [x] 检查所有本地 LVGL 字体并消除三处自引用 fallback。
3. [x] 运行字体回退静态检查、LVGL 模拟器 headless smoke 和主机自测。
4. [x] 构建 `esp32-wroom-32e-legacy` profile；最终 sdkconfig 保持 5 秒 task WDT 与 CPU0/CPU1 idle 检查启用。
5. [x] 通过 RFC2217 刷写并监控；首屏约 2.7 秒完成，`heap[boot_ready]` 约 6.6 秒出现，140 秒窗口无 WDT、panic 或重启。
6. [x] 运行 Trellis 质量检查和 GitNexus `detect_changes`；报告中的 HIGH 来自工作区原有 UI 改动，不属于本任务字体变更。

## 风险与回退点

- 风险：图标字体被请求未包含的 codepoint 后将不再递归，而是按 LVGL 默认缺字形行为处理；这正是需要的终止行为。
- 若实机仍出现 WDT，回退到诊断步骤，不变更 watchdog 配置。
