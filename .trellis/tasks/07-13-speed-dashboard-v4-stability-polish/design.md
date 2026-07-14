# 设计：速度仪表 V4 稳定性与绘制优化

## 边界与原则

- `components/esp_bms_idf_runtime/` 负责 NMEA 字节流分帧、RMC 有效性、GPS 速度规范化、行程积分和限频诊断。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 只消费 snapshot，决定标签是否变化及 `speed_art` 是否需要重绘；不在 UI 内重新解释 GPS 速度或定位状态。
- 不增加 snapshot 字段、不改变速度来源枚举/NVS/API，也不修改 CRITICAL 的 `runtime_update_snapshot_speed()`；规范化后的 GPS 速度继续沿用现有 `gps_speed_knots_milli → snapshot` 数据流。
- 不使用全屏 canvas、PSRAM 或动态帧缓存。新状态均为固定大小，正常镜像不输出逐帧日志。
- 三项修复共用一次真机集成验证，不拆为更多子任务：GPS 规范化改变色带输入，色带绘制成本又直接影响轮播性能，独立实现会遗漏交叉回归。

## 故障模型

### 静止速度与橙灯

```text
原始 RMC speed 1–3 km/h + status=A
  → 当前无死区，主速度显示 1–3
  → 行程开始后仍可能积分幽灵距离

RMC status=V 或连续 3 秒没有可解析 RMC
  → GPS_FIX_VALID=false
  → 卫星状态点变橙
```

速度漂移和橙灯是两个正交状态：规范化只处理 `status=A` 时的低速，不修改 `GPS_FIX_VALID`。PPS 继续仅用于诊断，不作为定位绿灯条件。

### 四频模块 NMEA 分帧

当前固定 96 字节缓冲接收所有 NMEA 类型。长 GSA/GSV 句溢出后，代码把长度清零却继续收集句尾，可能让残片进入解析/错误计数，并增加后续 RMC 超时的排查难度。

### 轮播卡顿

```text
LV_EVENT_SCROLL
  → 整个 320×240 viewport 失效
  → speed_art 进入可视区时每帧重绘
  → 32 段 × 2 个软件三角形
  → 每个三角形各自申请/释放 mask 行缓冲
  → CPU/临时分配压力，滚动帧变慢
```

此外，每秒 uptime 变化会触发 snapshot 更新，而 `set_gps_dashboard()` 当前无条件 invalidate `speed_art`。已有堆日志无持续下降、panic 或 WDT，因此先按重绘热路径处理，同时保留堆/最大连续块验证，不能把“不是主因”误写为“无需监控”。

### 色带接缝

每段四边形被拆为两个抗锯齿三角形，共享斜边两侧分别计算覆盖率。背景可能从共享边透出，形成用户看到的三角形内部细线。

## GPS 数据流

```text
UART1 byte
  → GPS stream framer
  → 完整 `$...\n` 句
  → RMC checksum/字段解析
  → fix 状态 + 原始 knot 速度
  → 2/4 km/h 迟滞规范化
  ├─> gps_speed_knots_milli → 现有 snapshot 投影 → TFT
  └─> trip efficiency sample → GPS 距离积分
```

### NMEA stream framer

新增小型纯 C 分帧模块，拥有固定 96 字节缓冲、长度和 `discarding` 状态：

- 只有 `$` 才开始收集；行首之前的杂字节直接忽略。
- 新 `$` 无条件开始新句，可从缺失换行或损坏句中自恢复。
- `\r` 忽略，`\n` 只发布未溢出的完整句。
- 到达上限后整句进入 `discarding`，忽略到下一个 `$` 或 `\n`；不得把尾部当新句。
- 溢出单独累计为 `overflow_lines`，不冒充 RMC 语法/校验错误。

该模块不扩大 UART 缓冲，也不尝试解析不需要的 GSA/GSV。标准且已实测较短的 GNRMC 继续进入现有严格解析器。

### 2/4 km/h 迟滞

在无 ESP-IDF 依赖的速度仪表数学模块中加入固定状态滤波器：

- fix 无效：清除运动态并输出 0。
- 静止态：原始速度达到 `4.0 km/h` 才进入运动态，否则输出 0。
- 运动态：原始速度降到 `2.0 km/h` 及以下时回到静止态并输出 0；高于 2.0 时保留原值。
- 阈值用 knot-milli 与整数比例比较，避免浮点和单位舍入；mph 只在现有 snapshot 投影阶段转换。
- RMC timeout、runtime reset/恢复默认值都重置滤波器，防止失效前的运动态泄漏到恢复后的 1–3 km/h。

RMC 入站先得到规范化速度，再同时写入 `gps_speed_knots_milli` 和行程样本。这样显示与距离来源仍是 GPS，但不会各自实现不同死区。

### 定位诊断

- 保留现有 `RMC timeout`、PPS first/recovered/lost/stable 日志。
- 累计 RMC `A`/`V` 数量、NMEA overflow 和真实 RMC parse error。
- fix 状态变化只输出限频 transition 日志；60 秒汇总包含最终 fix、PPS、A/V、overflow、parse error 和 bytes，用于区分卫星丢失、RMC 断流与分帧异常。
- 日志在主任务上下文执行，不在 PPS ISR 打印；正常每句 RMC 不打印。

## LVGL 绘制设计

### 有条件重绘

为 `speed_art` 保存一个紧凑渲染签名，只包含真正影响自绘图形的离散状态：

- 色带活动段数 `0..32` 和速度有效位；
- GPS fix 点；
- BMS online、SOC valid 和电池活动段数 `0..8`。

速度数字、时间、温度、挡位、电耗和刻度文字仍由现有持久缓冲/label 更新。仅渲染签名变化或对象重建/旋转时 invalidate `speed_art`；uptime、未跨段的速度细微变化、温度或文本变化不再重画整张色带。

### 无内部三角接缝的色带

保留 33 组内外曲线点和 32 段着色规则，但每段改为一条宽线：

- 线段端点取相邻内外点的中心。
- 宽度取两端带宽的整数近似平均，维持原有渐宽/曲线外形。
- 端部使用轻微重叠或圆端消除相邻段空隙；绘制顺序保持活动渐变、灰色和危险红色边界可预测。
- 外轮廓、刻度、卫星、电池和状态点继续复用现有 line/triangle/rect helper。

该方案把色带主体从每帧 64 个软件三角形降为约 32 个宽线任务，消除四边形内部共享斜边，同时不引入 framebuffer。若真实 LVGL 预览显示轮廓偏移，将只调整中心/带宽几何，不恢复共享边三角拆分。

### 滚动整屏失效 A/B

`CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE` 最初用于避免页面分块刷新/残影，属于 HIGH 风险路径，采用门槛式决策：

1. 先在保持 normal profile 为 `y` 的情况下验证宽线色带和有条件重绘。
2. 使用诊断 profile 分别构建 full-invalidate `y/n`，记录滚动期间 draw count、draw elapsed、空闲堆、最小堆和最大连续块。
3. 只有 `n` 在电池↔速度↔投屏、横竖屏、手动拖动和程序化动画中均无残影/分块，且卡顿明显更低，才把 normal default 改为 `n`。
4. 若 `n` 有任何残影，normal 保持 `y`，性能收益只来自绘制任务减半和无条件重绘消除。

诊断计数只在现有 drag diagnostics 配置下启用，滚动结束时一次性打印，避免逐帧 UART 日志反向制造卡顿。

## 兼容性

- GPS/Controller 来源状态机、控制器离线回退、在线无效不借 GPS、km/h↔mph、UTC+8、NVS 和 Web API 均不改契约。
- 控制器速度不进入 2/4 km/h GPS 滤波。
- BMS 离线继续隐藏电池和电耗；挡位无效继续显示 `1`；温度和状态栏布局不改。
- 行程起算仍为首次有效、规范化后的 GPS 速度达到 5 km/h；BMS 对齐、长间隔丢弃、回充抵扣及 0.1 km 发布门槛不改。
- 页面枚举和三页物理位置不改；新绘制逻辑继续覆盖 320×240 与 240×320。

## 风险与回退

- `runtime_update_snapshot_speed`：CRITICAL，21 个上游、6 个直接调用者、8 条流程。本任务设计为不修改；若执行中发现必须修改，停止并重新报告风险。
- `runtime_reset_state`：HIGH，4 个上游、2 个直接调用者、3 条流程。只增加滤波/分帧状态复位，回归启动和恢复默认值。
- `invalidate_dashboard_viewport`：HIGH，10 个上游、2 个直接调用者、3 条流程。只按 A/B 门槛决定配置，不先删除函数或调用点。
- 其余计划修改点为 LOW；`detect_changes --scope all` 必须确认没有扩散到设置导航、BLE、HTTP 或快捷面板语义。
- 回退顺序：先恢复 full-invalidate normal 配置，再回退宽线色带，最后回退 GPS 规范化/分帧；不得覆盖本任务以外的工作区内容。
