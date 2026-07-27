# 执行计划

1. 扩展 UI 状态，保存三张黑色占位页及标题对象。
2. 将 `page_transition_create()` 改为在 `s_ui.pages` 中创建同槽位、可吸附的
   黑页；移除屏幕级浮层标题手动位移。
3. 让过渡显示/隐藏只在真实页与占位页之间切换，并在所有收尾路径恢复真实页。
4. 更新模拟器冒烟：验证占位页保留横向滚动范围、标题存在、首尾边界正常，及
   结束后真实页恢复。
5. 运行 `./scripts/run-lvgl-simulator.sh --headless`、`git diff --check`，刷新
   GitNexus 后执行影响分析与 `detect-changes`；构建受影响 profile。
6. 更新轮播契约、提交本任务代码和文档，再归档任务。

## Risk Gates

- 编辑 `page_scroll_event_cb()`、`finish_page_scroll_state()` 和
  `create_screen()` 前，必须对每个既有符号运行 GitNexus upstream impact。
- 不改变现有手势距离阈值、页面映射或物理页的生成逻辑。
- 模拟器必须直接断言黑页可把容器滚到相邻槽位，避免只验证视觉状态而遗漏
  本次实机问题。
