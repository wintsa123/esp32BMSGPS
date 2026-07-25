# 技术设计

## 边界

配置层负责决定 LVGL 软件绘制线程数量；桥接层只负责 adapter worker 的 CPU 亲和性。UI 层继续由现有 mutex 保护，不感知 CPU 分配。

## 数据流

`app_main` 持锁更新 UI -> adapter worker 调用 LVGL handler -> 两个 LVGL 软件绘制线程并行渲染独立 draw task -> SPI DMA flush。

双绘制单元只缩短 CPU 绘制阶段，不能增加 SPI 总线带宽。双缓冲仍通过现有诊断开关测试，避免旧 ESP32 内部 DMA RAM 耗尽。

## 兼容性

- ESP32、ESP32-S3、ESP32-P4: 两个软件绘制单元，adapter worker 固定 Core 1。
- ESP32-C3: 一个软件绘制单元，adapter worker 固定 Core 0。
- 现有 `esp_bms_lvgl_bridge_lock` 保持所有应用侧 `lv_*` 调用互斥。

## 回滚

将绘制单元恢复为 1，或移除 worker core 覆盖，即可回到适配器默认调度行为。
