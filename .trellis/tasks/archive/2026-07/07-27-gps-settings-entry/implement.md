# 实施计划：GPS 一级设置入口

1. 在修改前读取前端和硬件构建规范；记录 GitNexus 对 `settings_show_controller_detail` 的 CRITICAL 影响，并在代码前确认调用者与模拟器流程。
2. 将设置根页和详情枚举拆分为 GPS、控制器两个一级入口；新增 GPS 详情与独立子视图状态，删除控制器页的 GPS-only 分支。
3. 让 GPS 详情复用单位和速度来源选择器，但以 GPS 状态处理返回和快照刷新；限制来源选择为 GPS+控制器构建。
4. 增加 GPS 专用快照比较与 simulator-only 钩子；扩展模拟器 CMake 的 GPS/控制器宏和 headless 矩阵断言。
5. 更新 `preview/render_system_settings.py`，生成首页与 GPS 详情的横、竖屏 PNG。
6. 运行格式化、GPS+控制器、仅 GPS、无 GPS 的 headless 模拟器检查、主机自测和 ESP-IDF 构建。
7. 依据 LAN RFC2217 技能刷写并监视硬件，验证 GPS 设置入口的实机可用性。
8. 运行 Trellis quality check、GitNexus `detect-changes`，更新适用规范并提交前确认工作区范围。

## Risk Gates

- 修改目标的 GitNexus 风险为 CRITICAL；保持对现有 5 个直接调用路径的导航兼容，并以模拟器矩阵覆盖 14 条关联流程中的 UI 路径。
- 不改 `esp_bms_idf_runtime`、GPS 解析或 Web；若 diff 扩散到这些边界，停止并重新评估。
