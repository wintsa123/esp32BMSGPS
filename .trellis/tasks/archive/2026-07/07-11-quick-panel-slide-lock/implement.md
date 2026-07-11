# 下拉栏一键锁屏与滑动解锁实施计划

## Implementation Checklist

1. 在 LVGL UI 私有状态中增加锁屏对象、timer、手势坐标和状态位；定义 3 秒超时、滑页阈值、解锁阈值与大触控尺寸常量。
2. 扩展 quick-panel 项目表，加入由 LVGL 基础图元绘制的锁屏图标 tile；在低风险 `quick_panel_item_event_cb()` 中将该项路由到 UI 内部锁屏入口，不排队 runtime action。
3. 创建透明全屏锁屏覆盖层和自绘滑条卡片，按横竖屏计算位置、轨道、填充与滑块大小。
4. 实现锁屏状态机：进入、轻点唤出、3 秒隐藏、滑动复位、阈值解锁、timer 清理。
5. 由覆盖层消费所有滑动，锁定期间冻结当前 Battery/Controller/GPS 页面并屏蔽所有底层交互。
6. 在 `create_screen()` 末尾创建覆盖层；在 `rebuild_screen_if_needed()` 中保存并恢复锁定位，确保 Web/runtime 页面结构变化不会绕过锁屏。
7. 如现有 host preview 可安全扩展，增加横竖屏锁定态和解锁滑条态预览，产物放在 `preview/`。
8. 运行格式/编译检查、任务范围 diff 检查、GitNexus `detect_changes --base-ref main`，再通过 LAN RFC2217 烧录并监控串口。

## Validation

- `git diff --check`
- 项目现有 ESP-IDF 构建命令（由硬件 skill 确认环境后执行）
- GitNexus `detect_changes --scope compare --base-ref main`
- 真机：下拉栏点击锁图标后立即隐藏。
- 真机：锁定时下拉、设置、页面内点击和左右滑页均无效。
- 真机：轻点显示大滑条；短滑/中途松手不解锁并复位。
- 真机：3 秒不通过后滑条隐藏；再次轻点可重新显示。
- 真机：滑到 85% 并释放后解锁，下拉与普通页面交互恢复。
- 真机：横竖屏/旋转重建后无越界、无 timer 崩溃、锁定状态不被绕过。

## Risky Files And Rollback Points

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`：已有未提交控制器页面和设置改动；所有补丁必须基于当前工作树，禁止覆盖或还原用户修改。
- `preview/`：只新增或更新锁屏预览，不触碰无关预览资源。
- 在触碰 `quick_panel_item_event_cb`、`create_screen`、`rebuild_screen_if_needed` 前复核已完成的 GitNexus impact；如实现需要改写 `set_quick_panel_open` 或 `move_to_page`，必须暂停并重新评估 CRITICAL/HIGH 风险。
