# 实施计划

1. 在通用默认配置启用两个 LVGL 软件绘制单元，并在 C3 覆盖为一个。
2. 在 LVGL bridge 初始化时按编译期核心数设置 adapter worker 核心。
3. 运行格式化、默认 ESP32 与 C3/S3 配置检查，以及模拟器自检。
4. 用现有 `esp-idf-drag-diag.sh` 在实机 A/B 对比全屏失效与双缓冲；仅在日志确认内部 RAM 余量后保留双缓冲。

## 风险与验证

- `esp_bms_lvgl_bridge_init` 上游影响低，只由 `app_main` 调用。
- 不修改高风险 `esp_bms_lvgl_ui_update`。
- 配置修改必须覆盖双核与单核目标，避免 C3 创建非法 Core 1 任务。
