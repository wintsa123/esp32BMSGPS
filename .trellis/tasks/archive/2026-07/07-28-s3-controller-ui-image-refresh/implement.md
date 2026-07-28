# Implementation

1. 在 `create_controller_dashboard()` 为精确的 `480x320` 和 `320x480` 写入原生几何，保留旧画布分支。
2. 扩展模拟器控制器页 smoke check 至两个原生画布。
3. 构建并运行两种分辨率的 LVGL 模拟器，检查控制器页截图与快照断言。
4. 运行针对性代码检查和 GitNexus 改动分析；确认只触及预期 UI 流程。
