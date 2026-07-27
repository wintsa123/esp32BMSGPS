# 实施计划：仪表设置归位与 GPS 搜星信息

1. 读取适用 Trellis 规范，对计划修改的解析、运行时和 LVGL 符号执行 GitNexus 上游影响分析。
2. 扩展纯 C GPS stream helper，解析校验和有效的 GSA/GSV，并在 `gps_stream_selftest` 覆盖正常、损坏、空字段、长 GSV 和最大 `C/N0`。
3. 扩展 dashboard snapshot 与 runtime 发布/超时路径，将卫星聚合数据从 GPS 任务传播到 UI。
4. 新增仪表一级详情和独立子视图状态，迁移样式、速度单位、速度来源；精简 GPS 与控制器详情并修正所有返回/刷新路径。
5. 在无 GPS 编译分支裁剪 S1000RR 卫星图标与 FIX 渲染签名，增加 simulator-only 可见性断言。
6. 更新 `preview/render_system_settings.py` 及受影响的 GPS、仪表横竖屏预览。
7. 运行格式检查、host selftests、GPS+控制器/仅 GPS/仅控制器/两者均无的横竖屏 headless 矩阵，以及 ESP-IDF 6.0.2 profile 构建。
8. 按项目 RFC2217 流程完成固件刷写尝试，执行 GitNexus `detect-changes`、Trellis quality check、规范同步与任务收尾。

## Risk Gates

- `esp_bms_dashboard_snapshot_t`、GPS publish 路径和 `settings_navigate_back` 属于共享路径；若 GitNexus 返回 HIGH/CRITICAL，先报告影响范围再编辑。
- 不修改 Web manifest、NVS 和动作枚举；diff 进入这些边界时停止并重新评估。
- 工作区已有并行 LVGL/手势改动；只叠加本任务最小 hunks，不覆盖或提交无关修改。
