# Implementation

1. 在 `simulator/CMakeLists.txt` 为桌面目标定义 `ESP_BMS_LVGL_SIMULATOR=1`，确认 ESP-IDF 组件构建不定义该宏。
2. 在 `esp_bms_lvgl_ui.c` 的条件编译块中增加最小运动 UI 状态，复用现有对象、颜色、标签、pointer 坐标和固定布局帮助函数。
3. 在 GPS/控制器稳定页加入长按与向上手势识别，实现全屏 guard、中央双按钮面板、点击外部关闭和触摸防穿透。
4. 实现行程记录：当前仪表速度的时间加权平均、最大值和主机实际时钟；仪表上的 `REC + 用时`；红色停止按钮；带左上角返回的 2x2 结果页。
5. 实现 0-100 流程：GPS 等待、3/2/1 倒计时、全屏 GPS 速度/`km/h`/计时、模拟加速、50/100 门栓、100 计时定格、长按退出和上滑停止面板。
6. 用单个自绘对象实现中心向四周的流光线段；用有界固定方向表和整数插值保持性能可预测。
7. 在 `simulator/main.c` 使用 SDL2 生成倒计时、50 km/h 和 100 km/h 短音调，并增加 headless 演示序列/截图状态；不引入新依赖。
8. 审计新增中文字符是否已包含在 `settings_zh_10/13/16`；只在确有缺字时重生成现有字体，不新增字体族。
9. 运行 `./scripts/run-lvgl-simulator.sh --headless`、`./scripts/run-lvgl-simulator.sh --headless --portrait`，确认普通冒烟与两条运动演示流程结束且无崩溃。
10. 在横屏和竖屏可视模拟器中检查手势、触摸区、遮罩、记录结果、倒计时、流光、100 计时定格和长按/上滑退出，将 PNG 截图保存到 `preview/`并目视确认。
11. 运行 `git diff --check`、ESP-IDF 构建和 GitNexus `detect_changes --scope compare --base-ref main`，确认固件预处理排除原型且变更范围仅包含预期 UI/模拟器符号。

## Validation Gates

- 任一 headless 方向未输出 `headless smoke passed` 则不进入视觉验收。
- 任一中央字号、单位、计时或停止按钮在 320x240/240x320 中裁切或重叠则回到布局步骤。
- 手势与横向分页冲突、guard 触摸穿透、50/100 提示音重复或 100 后计时继续增长均视为阻断问题。
- 如 GitNexus 检测到预期之外的真机 runtime/snapshot/音频流程，不提交实现，先缩小变更。

## Rollback Points

- 完成入口遮罩后先单独跑横竖 headless；如手势冲突，仅回退新的上滑/长按回调。
- 行程记录和 0-100 各自在同一条件块内保持独立入口；任一流程不稳定时可先回退该流程，保留共享遮罩。
- 删除模拟器编译宏即可证明固件构建不包含原型。
