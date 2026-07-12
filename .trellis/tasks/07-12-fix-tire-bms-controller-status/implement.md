# Implementation

1. 读取前端/后端规范与相关思考指南，确认工作区基线。
2. 对 `settings_controller_roller`、`set_dashboard`、`apply_dashboard_snapshot`、`runtime_controller_set_subscription`、`runtime_controller_dsc_cb`、`runtime_controller_gap_event` 和 `esp_bms_idf_runtime_set_active_data_source` 跑 GitNexus upstream impact；如风险为 HIGH/CRITICAL，先报告 blast radius。
3. 修改 roller 公共样式和滚动导航行为，验证只影响轮胎 roller 页面。
4. 在 `set_dashboard` 添加安全的显示层电流符号转换，并让充电视觉使用转换后的符号。
5. 在 controller online 边沿结束连接 toast；调整订阅和 notify 解析条件，使被动遥测与稳定页面解耦，而 gather 保活仍受页面限制。
6. 执行目标文本/调用点检查、`git diff --check` 和完整 ESP-IDF build。
7. 执行 GitNexus `detect_changes --scope compare --base-ref refs/heads/main`，检查受影响流程；按硬件技能刷写并监控真实连接、控制器页数据和断线恢复。

## Risk Gates

- UI snapshot 刷新和 controller GAP 事件是高影响路径；修改前必须完成 impact 分析。
- 不更改任何已有 action 值、snapshot 位或 NVS 格式。
- 全量构建通过后才刷写；真机可用时验证连接提示、背景页面遥测缓存和控制器轮播显示。
