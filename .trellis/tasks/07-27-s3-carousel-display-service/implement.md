# 实施清单：S3 轮播性能与显示任务重构

## Implementation

- [x] 在 LVGL bridge 增加 flush/DMA 计时与失效面积计数读取接口；保留 adapter 初始化，
  停止从 firmware 启动 adapter worker。
- [x] 新建 `esp_bms_display_service` 组件和公开 API，使用静态任务栈、命令队列、单槽
  覆盖快照队列、动作队列和调用者同步通知。
- [x] 将 `main/idf_main.c` 的 bridge/UI/LVGL 锁、启动动画、校准、亮度、旋转、切页和
  投屏路径改为显示服务调用。
- [x] 将 runtime WebSocket 投屏块改为显示服务命令，删除 runtime 对 bridge 锁和
  `write_rgb565` 的直接访问。
- [x] 重构 BMS 横屏为真实 480x320 网格，分离静态与动态层，生成/复用 PSRAM RGB565
  snapshot 缓存。
- [x] 重构 Fireblade 横屏为真实 480x320 圆弧和面板布局，分离静态与动态层并建立第二张
  snapshot 缓存；保留指针与数据字段的动态更新。
- [x] 将 S3 双缓冲提升到 480x120，将 ST7796U catalog 时钟提升到 20 MHz，并扩展配置
  自测。
- [x] 扩展模拟器：480x320 BMS/Fireblade 拖动、吸附、旋转重建和 300 次高频快照；检查
  缓存分配次数、对象计数、拖动后最新数据和无增长堆使用。

## Verification

```bash
git diff --check
./tests/configurator_selftest.sh
cmake -S simulator -B simulator/build && cmake --build simulator/build
./simulator/build/esp_bms_lvgl_simulator --headless --resolution 480x320 --style fireblade
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

目标 profile 额外执行 S3 构建、RFC2217 烧录，慢拖和快速甩动各 20 次、至少 3 次冷启动，
收集 `drag_perf` 日志并计算 P95。

## Verification Status (2026-07-27)

- [x] `git diff --check`、`tests/configurator_selftest.sh`、模拟器 480x320
  Fireblade 压力路径和 S3 profile 构建通过。
- [x] GitNexus `detect-changes -r esp32BMSGPS --scope all` 已运行；当前大范围
  UI/运行时重构按预期报告 CRITICAL 影响面。
- [!] RFC2217 已连接并识别目标 ESP32-S3，但普通 stub 与 `--no-stub` 写入均在桥接
  侧卡住，未产生写入完成证据；设备随后已硬复位。真机拖动 P95、三次冷启动和全区域
  触摸仍待桥接恢复后执行。

## Risk Gates

- `dashboard_viewport` 和 `esp_bms_lvgl_ui_update` 的 GitNexus upstream 风险均为
  CRITICAL；每一轮 UI 修改后先跑模拟器再继续迁移 main/runtime。
- 20 MHz 通过构建但无法通过冷启动、全触摸区域或长时轮播时，只回退 catalog 时钟到
  10 MHz，保留已验证的软件优化。
- 若 PSRAM 缓存分配失败，功能须可用且明确记录 `cache=off`；该状态不能通过性能验收。
