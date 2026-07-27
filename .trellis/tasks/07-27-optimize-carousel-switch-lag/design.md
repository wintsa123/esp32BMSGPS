# 静态仪表底图设计

## 范围

仅经典 ESP32、S1000RR、原生横屏 `320x240`。底图以 RGB565 小端序嵌入固件 Flash，不分配 PSRAM 或内部堆。

## 图层契约

底图包含黑色背景、未激活色带、红色危险区、外框、刻度、末端红区和分隔线。LVGL 仍绘制已激活的非危险色带、电池、卫星状态、温度前缀及所有实时文本。

`speed_art` 的背景图与其坐标均为 `320x240` 时才启用；其他尺寸清除背景图并沿用完整动态绘制。危险区无论速度如何均为红色，因此不需要动态重绘。

## 资源

`scripts/generate-speed-dashboard-static.py` 生成源 PNG 与 `components/esp_bms_lvgl_ui/speed_dashboard_static_landscape.rgb565`。CMake 仅在 `IDF_TARGET=esp32` 且 S1000RR 特性开启时通过 `EMBED_FILES` 链接该原始 RGB565 文件。
