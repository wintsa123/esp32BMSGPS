# 技术设计：ESP32 启动期 WDT 修复

## 范围与边界

本任务只处理 legacy profile 在 LVGL 启动期造成 CPU0 IDLE0 饥饿的代码路径。任务看门狗
保持启用，不能通过修改超时或移除 idle 订阅降低报警灵敏度。

## 根因与修复

`bluetoothon` 和 `wlanJZ` 是只含图标字形的 LVGL 自定义字体，但其 `lv_font_t.fallback`
曾指向自身。LVGL 对不存在的字形沿 fallback 链继续查找；自环没有终止节点，因而可在
LVGL 定时器上下文持续占用 CPU0。移除两处 fallback 赋值，让默认的空 fallback 终止查找。

字体仍只负责其现有图标 codepoint；其他文本字体不改变，UI 的图标居中与样式逻辑不改变。

## 验证与回退

静态扫描所有本地字体定义，禁止 `fallback = &<同名字体>`。构建 legacy profile 后，以目标
板串口观察冷启动首屏、启动动画和 2 分钟空闲期。若 WDT 复现，保留 WDT 并在最小的定时/
启动路径添加可撤销诊断，而非调整 watchdog 配置。

回退只需恢复两个字体定义中的 fallback 行，但仅在实机证据否定该根因时进行。
