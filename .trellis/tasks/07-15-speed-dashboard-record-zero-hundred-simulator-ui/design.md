# Design

## Boundaries

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 保持为唯一 UI 实现。新功能放在 `ESP_BMS_LVGL_SIMULATOR` 条件编译块中，模拟器直接复用生产页面与手势，固件构建不包含原型代码。
- `simulator/main.c` 只提供 SDL2 提示音和可重复的 headless 演示驱动，不复制 LVGL 页面、布局或统计逻辑。
- `simulator/CMakeLists.txt` 仅为桌面目标定义 `ESP_BMS_LVGL_SIMULATOR=1`；不改动 ESP-IDF 组件依赖、runtime、snapshot 合同或公开 UI API。
- 不新增子任务：行程记录和 0-100 测试共享同一入口遮罩、手势守卫、计时器和横竖屏布局，分开实现会重复状态管理。

## State Model

使用一个模拟器专用状态结构保存对象指针、记录统计和 0-100 流程，不引入通用状态机框架。

- 视图状态：`NONE -> MENU -> TRIP_RESULT` 或 `NONE -> ZERO_WAIT -> ZERO_COUNTDOWN -> ZERO_RUN`。
- 行程记录是独立布尔状态，记录中可在 `NONE` 和 `MENU` 之间切换，仪表页持续显示 `REC + 用时`。
- 一个暂停/恢复的 LVGL 50 ms 计时器驱动记录统计、倒计时、0-100 计时和流光相位；空闲和结果页时暂停。
- 旋转会重建 LVGL 根对象；模拟原型在重建前结束当前运动测试，新方向从原仪表页重新进入。本阶段不增加跨根对象恢复协议。

## Input And Navigation

- 仅当稳定页为 GPS 速度仪表或控制器页时，页面长按或明确的竖直上滑才打开运动菜单。
- 上滑识别使用已有 pointer 坐标模式：竖直位移超过阈值且绝对值明显大于水平位移时才消费，避免破坏横向分页。
- 菜单使用一个全屏可点击 guard 和一个中央面板。guard 阻止底层穿透，点击面板外关闭；按钮点击不冒泡到 guard。
- 行程结果页的左上角返回原仪表。0-100 页长按 guard 立即退出；上滑显示只有一个红色停止按钮的遮罩，点击后退出。

## Trip Recording Data Flow

`s_ui.last_snapshot.speed_deci_units + speed_unit + active_speed_source -> 50 ms time-weighted accumulator -> REC badge/result labels`

- 开始时记住当前显示单位和速度来源，每个计时器周期对当前显示速度做按时间加权的累计。
- 平均速度 = `sum(speed_deci * delta_ms) / total_ms`；停车的 0 速度计入总用时。
- 超过已记录最大值时，同步保存主机 `localtime_r()` 的 `HH:MM:SS`。
- 本原型假定记录过程中不切换速度单位；真机数据层接入时再统一归一化为 km/h。

## 0-100 Data Flow

- 进入后先强制展示 GPS 等待页至少一个短延时，然后使用模拟 GPS 连接进入 `3 -> 2 -> 1`。
- 倒计时结束后，模拟 GPS 速度从 0 平滑上升并继续超过 100 km/h；页面始终强制显示 `km/h`，不跟随全局 mph 设置。
- 50 km/h 和 100 km/h 用布尔门栓确保提示音只播放一次。达到 100 km/h 时保存并定格计时，但速度和背景动画继续。

## Visual Design

- 入口面板采用两个固定尺寸大按钮：播放/红色停止使用 LVGL 符号或简单几何图元；骷髅头使用圆、矩形和齿形的 LVGL 子对象组合，不增加位图资源。
- 0-100 页为纯黑全屏。大速度数字使用现有 `controller_digits_72`，下方依次是小写 `km/h` 和单一计时。
- 流光使用一个自绘对象和固定方向表绘制有界数量的线段，线段从中心向边缘移动。不启用 ThorVG/矢量引擎，不创建大量独立 line widget。
- 横屏使用横向按钮行，竖屏使用两个稳定大触摸区；结果页使用顶部导航和 2x2 数据区，不嵌套卡片。

## Simulator Sound And Automation

- UI 通过模拟器专用函数请求简短音调。`simulator/main.c` 使用 SDL2 原生音频 API 生成短方波，不引入 SDL_mixer 或音频文件。
- headless 模式使用 SDL dummy audio driver，并通过模拟器专用的演示入口驱动行程结果和 0-100 完成态；真实手势仍在可视 SDL 窗口中用鼠标验证。

## Risk Control

- GitNexus 上游影响：`create_screen` 为 LOW（6 个受影响符号，直接调用者为 `esp_bms_lvgl_ui_init` 和 `rebuild_screen_if_needed`）；模拟器 `apply_command` / `process_ui_action` 为 LOW，仅由 `main` 调用。
- `apply_dashboard_snapshot` 为 CRITICAL（20 个受影响符号，涉及真机 `app_main`、页面滚动和快捷面板）。本设计不修改它，新计时器只读已有 `s_ui.last_snapshot`。
- 如实现中发现必须改动 HIGH/CRITICAL 符号，先停止编码并向用户报告新的影响范围。

## Rollback

- 移除 `ESP_BMS_LVGL_SIMULATOR` 条件块、模拟器编译定义和 SDL 音调/演示驱动即可完整回退，不需要迁移 snapshot、NVS 或 runtime 状态。
