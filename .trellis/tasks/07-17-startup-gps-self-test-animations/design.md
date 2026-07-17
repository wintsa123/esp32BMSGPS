# Design: GPS 启动自检与双风格启动动画

## Architecture

### Capability contract

在公共 LVGL snapshot 契约中新增：

- `esp_bms_gps_module_state_t`: `PROBING`、`AVAILABLE`、`UNAVAILABLE`。
- `esp_bms_boot_animation_style_t`: `CHARGE`、`GAUGE_SWEEP`。
- snapshot 字段 `gps_module_state` 与 `boot_animation_style`。

`gps_module_state` 是硬件/协议能力，不是定位状态；`GPS_FIX_VALID` 继续只表示当前定位有效。这样未来轨迹、零百、Web API 与本地 UI 不必各自推断 UART 字节计数。

### Startup sequence

```text
runtime init + UART configure
          |
          v
load NVS settings before display (boot style / dashboard style / rotation)
          |
          v
LVGL bridge + production dashboard UI
          |
          v
boot_start(style) ---- charge overlay OR existing gauge page
          |
          v
start configured BLE clients + 3 s runtime_tick/GPS probe loop
          |
          v
finalize GPS probe -> boot_finish(real snapshot) -> battery page -> normal loop
```

调用 `runtime_tick()` 驱动探测，避免复制 UART 读取/解析路径。启动循环每 50 ms 更新一次真实阶段进度，并继续让 LVGL port 自己刷新显示。

### GPS evidence and recovery

`runtime_feed_gps_byte()` 在两个协议边界确认模块：

1. CASBIN parser 产出完整有效帧；
2. NMEA parser 产出完整行且 checksum 正确（不限定为 RMC）。

原始 UART 字节、乱码和仅仅驱动安装成功都不构成证据。探测超时只把状态置为 `UNAVAILABLE`，不卸载 UART；因此后续有效帧可复用同一状态转换函数恢复。状态转换同时刷新派生速度有效性并触发 snapshot changed。

### Speed-source fallback

`runtime_update_snapshot_speed()` 保持现有单位换算和滤波不变，只调整来源决策：

```text
configured GPS + GPS available       -> GPS
configured controller + online       -> controller
configured source unavailable, alternate available -> alternate
neither available                    -> configured enum retained, SPEED_VALID=false
```

UI 通过 snapshot 的实际能力禁用单一/无可用来源时的切换；runtime 对 action 再次校验，防止模拟器、旧 UI 或未来 Web 调用绕过。

### Carousel rendering

生产 UI 仍只维护一份现有速度页对象，避免重写仪表。`speed_page_renderable = GPS AVAILABLE || CONTROLLER ONLINE`：

- false：给速度页加 `HIDDEN`，投屏页从 `2 * width` 前移到 `1 * width`，轮播索引映射压缩；若当前停在速度页则回到电池页。
- true：移除 `HIDDEN`，投屏页恢复到 `2 * width`。

所有手势阈值、throw-begin 冻结和 `-1/0/+1` 限制保持原样；仅修改 page-to-x 与 x-to-page 映射。

### Boot animations

#### Charge

在生产 UI 顶层创建全屏、不响应触摸的 boot overlay。使用 LVGL 基础对象绘制：边框角标、细网格、10 段电池、扫描条、百分比与 ASCII 阶段标签。`boot_update()` 只改变已有对象的可见性、颜色、位置和静态文本缓冲，不在每帧创建对象。

#### Gauge sweep

不创建新仪表。`boot_start()` 把轮播切到现有速度页，选取当前 Fireblade 或 S1000RR 风格；`boot_update()` 构造仅供显示的局部 snapshot，让现有 `speed_dashboard_style_apply()` / `set_gps_dashboard()` 更新速度。演示 snapshot 不进入 runtime，也不调用行程采样。结束时用真实 runtime snapshot 覆盖并回到电池页。

### Persistence and settings

新增 NVS u8 键 `boot_anim`。读取使用 optional 语义：旧 NVS 无此键时取 `CHARGE`；保存与恢复默认走现有 display settings 事务。新增 action `SET_BOOT_ANIMATION_STYLE`，numeric payload 只接受枚举范围。系统设置中新增两项选择页。

## Compatibility

- snapshot 仅在固件内部与同仓库模拟器共享，不是持久化二进制 ABI；新增尾部字段对现有 NVS 无影响。
- 旧 NVS 不需要强制迁移写回；下一次正常设置保存会写入新键。
- GPS UART、PPS GPIO、协议解析和 RMC 定位语义不变。
- 启动画面仅用已启用字体和基础 LVGL widget，不增加图片、ThorVG 或 PSRAM 依赖。

## Risk Controls

- `runtime_update_snapshot_speed`、snapshot apply 和轮播映射均被 GitNexus 标记为 HIGH/CRITICAL；分别用纯派生逻辑、四组合矩阵和双方向模拟器隔离验证。
- 进入/退出速度页前停止滚动动画，避免隐藏当前 snappable 子对象时保留越界 scroll_x。
- boot overlay 和动画指针在旋转重建、完成和重复调用时统一清理，避免 LVGL 悬空对象。
- 任何 GPS 探测失败都只降级功能，不中止启动。

## Rollback

- GPS 能力字段和探测状态机可独立回退，不改变 UART parser 数据格式。
- 启动动画 API 只包围现有 UI 初始化与主循环之间的阶段；移除调用即可恢复原启动路径。
- 新 NVS 键对旧固件无害，旧固件会忽略它。
