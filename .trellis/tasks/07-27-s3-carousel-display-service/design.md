# 设计：S3 轮播性能与显示任务重构

## Boundary

新增 `esp_bms_display_service` 组件，作为 firmware 上唯一能调用 LVGL、
`esp_bms_lvgl_ui_*` 和 `esp_bms_lvgl_bridge_*` 的任务。`app_main` 与 runtime
保留业务状态、网络、BLE 和持久化职责，但不再拿 LVGL 锁或操作显示对象。

`esp_lv_adapter_init()` 仍用于时钟、递归锁、显示和触摸注册；不调用
`esp_lv_adapter_start()`。显示服务任务在每次处理完命令和最新快照后，持有 adapter
锁调用 `lv_timer_handler()`，按返回延迟休眠。这保留已验证的 flush/touch bridge，
同时只保留一个 LVGL worker。

## Service Contract

```text
runtime/main -- publish_snapshot (static depth-1 overwrite queue) --> display service
runtime/main -- submit_command (static bounded queue, synchronous result) --> display service
display service -- take UI action --> static bounded action queue --> runtime/main
runtime HTTP cast block -- submit cast command --> display service --> bridge write
```

- `start(config, brightness, snapshot)` 在显示任务内初始化 bridge、加载校准、创建 UI、
  显示启动动画并等待 ready；调用者不会触碰 LVGL 对象。
- `publish_snapshot()` 用 `xQueueOverwrite()` 合并快照。拖动/吸附期间 UI 继续保存最新
  快照，结束后由既有 UI 路径一次应用。
- `submit_command()` 用静态命令队列和单调用者互斥锁。命令带直接任务通知回复，调用方在
  返回前得到 `esp_err_t`，不会把临时指针留给显示任务。
- UI 动作由显示任务从现有单槽 UI 动作状态取出，校准动作在服务内部完成；其它业务动作
  写入有界静态队列，由主循环读取。
- 服务公开稳定数据源的原子快照，主循环无需读取 LVGL 状态。

## Commands

服务命令覆盖启动动画更新/结束、亮度、旋转、页面切换、显示 dashboard、触摸校准结果、
重置触摸校准以及 RGB565 投屏块。旋转在服务内调用 bridge 后重建 UI，并在成功时保留
新的底图缓存；投屏块在 HTTP 缓冲仍有效的同步命令期间写入。

## Native Dashboard Rendering

BMS 和 Fireblade 的横屏布局使用当前显示的真实 `width`/`height` 计算间距、网格、
圆弧中心和半径，不再创建 320x240 视口后设置 `transform_scale`。竖屏继续以 240x320
的现有原生尺寸建立页面，旋转时走现有重建流程。

每个横屏仪表建立两个层：静态层包含底色、面板、刻度、标题、图标和分隔线；动态层只含
数值、状态、指针等会变对象。静态层创建后：

1. 以 `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` 分配固定 RGB565 缓存；
2. 以 `lv_draw_buf_init()` 包装该缓存，调用 `lv_snapshot_take_to_draw_buf()`；
3. 用 `lv_image` 显示缓存，删除原静态层，保留动态层；
4. 只在 UI 初始化或旋转重建时重新生成缓存，正常快照/手势路径不分配内存。

两张 480x320 RGB565 缓存约 614,400 bytes，总量由 S3 的 8 MB PSRAM 承担。缓存分配
失败时必须记录并回退为原对象树，保证显示可用但不声称性能目标已达成。

## Buffers And Bus

S3 默认 `ESP_BMS_LVGL_BRIDGE_SPI_DRAW_BUFFER_HEIGHT=120` 与双缓冲，桥接层按逻辑分辨率
分配两块 RGB565 绘制缓冲。ST7796U I80 catalog 的 `PIXEL_CLOCK_HZ` 调为 20 MHz；其余目录
不变。时钟失败的运行时/冷启动验证将该值回退为 10 MHz，不回退服务或缓存架构。

## Metrics

bridge 记录每次 flush 的开始/结束、DMA 等待和失效面积；服务记录一次拖动会话的
`RENDER`、`FLUSH`、等待 DMA、失效面积、命令/快照队列延迟与 PSRAM/DMA 最低空闲。
滚动结束时打印一条固定格式 `drag_perf` 日志，供脚本计算 P95。

## Compatibility And Rollback

- 模拟器仍是 LVGL 单线程所有者并继续直接调用 UI API；它新增 480x320 横屏压力路径来
  验证原生布局、缓存重建和快照合并。
- 原有局部失效诊断开关保持独立，不与本任务的布局或服务切换耦合。
- 20 MHz 是唯一可单独回退的硬件参数；其余重构只在构建/模拟器/真机验证失败时以代码修复。
