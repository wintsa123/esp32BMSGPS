# 下拉栏一键锁屏与滑动解锁设计

## Architecture

功能保持在 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 内部：

- 下拉栏新增一个仅由 UI 消费的锁屏快捷项，不新增运行时 action，不修改 NVS。
- UI 状态新增“已锁定、解锁提示可见、滑动追踪中”等位标志，以及锁屏覆盖层、滑条和一次性超时定时器对象。
- 透明全屏锁屏覆盖层位于仪表轮播页之上，统一拦截底层触控，因此设置入口、下拉手势和页面内控件无法被误触。
- 覆盖层拦截所有轮播手势；锁定期间不调用页面移动函数，当前轮播页保持冻结。
- 进入锁屏调用现有 `set_quick_panel_open(false)` 收起面板；不修改 CRITICAL 风险的面板状态函数。

## State And Interaction Contract

### Enter Lock

1. 用户点击下拉栏锁图标。
2. 立即退出快捷面板编辑/临时覆盖状态，并通过现有路径收起下拉栏。
3. 设置全局 UI 锁定位，隐藏解锁提示，只显示当前仪表轮播页。
4. 将透明锁屏覆盖层移到最前并隐藏 `quick_pull_zone`。

### Locked Carousel Display

- 覆盖层按下时记录起点。
- 所有横向和纵向滑动均被消费，不移动底层轮播页，也不显示解锁提示。
- 位移保持在轻点容差内时，松手显示解锁提示。

### Unlock Prompt

- 提示为全屏透明拦截层中的底部/中下部滑条卡片；横竖屏分别计算宽度和位置，避免越界。
- 视觉轨道高度不小于 52px，滑块不小于 44px，并额外扩展点击区域，保证易触控。
- 解锁只允许从左侧起始区开始拖动；释放时达到轨道 85% 阈值才通过。
- 未达到阈值则滑块和填充立即复位，仍保持锁定。
- 每次提示从隐藏变为显示时启动一个 3000ms 单次 LVGL timer；超时隐藏提示、复位滑条并保持锁定。
- 达到阈值后删除 timer、隐藏覆盖层、清除锁定位并恢复 `quick_pull_zone`。

## Widget And Layout Choice

- 使用项目现有的基础 `lv_obj_t` / `panel()` / `label()` 组合构建滑条，而不是新增组件依赖。
- 滑条父容器使用固定触控高度和按屏宽计算的宽度；它是浮在轮播页上的 overlay，不参与底层布局。
- 新锁屏快捷项沿用现有 quick-panel tile。当前 LVGL 内置 FontAwesome 子集不含锁字形，因此用基础 LVGL 矩形和边框组合绘制锁身与锁梁，不依赖新增字体或图片资源。
- 新增 UI preview 时只写入仓库根目录 `preview/`；本次优先复用现有预览脚本增加锁屏态截图。

## Rebuild And Compatibility

- `rebuild_screen_if_needed()` 当前会清空 `s_ui`。重建前保存锁定位，删除锁屏 timer；重建后恢复仪表页，再重新应用锁屏覆盖状态。
- 锁定期间本机无法触发旋转或控制器设置，但 Web/runtime 仍可能导致页面结构重建，因此锁定状态必须跨重建保留。
- 不改变 action enum、运行时 snapshot、Web UI、亮度/音量手势或设置页面交互。
- 锁屏只存在于 RAM，重启后默认解锁。

## Risk Control

- `set_quick_panel_open` 的 GitNexus 风险为 CRITICAL：只调用，不修改；锁定状态不再触发 `move_to_page`。
- `create_screen` 与 `rebuild_screen_if_needed` 风险为 LOW，但覆盖启动、旋转和页面结构变化，必须构建并上板回归。
- 全屏覆盖层的层级错误可能让下拉区穿透；进入锁屏和重建恢复后都显式 `lv_obj_move_foreground()`。
- timer 生命周期必须在解锁、重建和重复显示时统一清理，避免回调访问旧对象。

## Rollback

回滚仅删除本任务在 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 和 `preview/` 中新增的锁屏状态、对象、回调和预览场景；不回滚文件中已有的控制器页面等用户改动。
