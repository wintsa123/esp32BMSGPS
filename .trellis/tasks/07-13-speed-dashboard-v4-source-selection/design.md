# 设计：统一速度仪表 V4 与速度来源选择

## 边界与所有权

- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` 是跨层契约唯一来源：页面/采集/action 枚举、`esp_bms_speed_source_t` 和 dashboard snapshot 显式字段均在此定义。
- `components/esp_bms_idf_runtime/` 拥有 NVS 迁移、来源状态机、GPS 本地时间、采样对齐、距离/能量积分、HTTP 配置和 snapshot 投影。
- 纯时间/电耗数学放入 runtime 组件内独立、无 ESP-IDF 依赖的模块，供固件与 host selftest 共用，避免测试复制公式。
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 只消费 snapshot、绘制仪表、管理三页轮播和设置交互，不在 UI 内推导实际来源或积分。
- `main/idf_main.c` 继续只负责 action 保存和采集模式转发；不承载业务计算。
- `main/web/index.html` 只投影 `/api/config` 契约并显示离线提示。

## 数据流

```text
GPS RMC ───────────────┐
                      ├─> 对齐采样/时间换算 ─> runtime 状态 ─> snapshot ─> V4 TFT
BMS 电压/原始电流 ────┘                                    └─> /api/config + Web

控制器 BLE ─> 在线/速度/挡位/温度 ─> 来源状态机 ────────────────┘
TFT action / HTTP POST ─> 同一 speed_source preference ─> NVS
```

统一速度页返回 `ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD`。runtime 在该模式同时执行 BMS 周期请求和控制器 gather；GPS UART 无条件轮询。行程累计状态属于 runtime 开机生命周期，不依赖当前页或当前选定速度来源。

## 公共契约

### 枚举兼容

- 页面枚举现有数值完全不变。
- `ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD` 追加到现有 data source 枚举末尾。
- `ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE` 追加到 action 枚举末尾。
- `esp_bms_speed_source_t`：`GPS=0`、`CONTROLLER=1`，便于新机零初始化默认 GPS。

### Snapshot 显式字段

- `speed_source`：持久化偏好。
- `active_speed_source`：经过在线降级后的实际来源。
- `gps_local_time_valid`、`gps_local_hour`、`gps_local_minute`。
- `average_consumption_valid`、有符号的 `average_consumption_deci_wh_per_distance`；该值已按当前速度单位投影，UI 只格式化。

既有 `SPEED_VALID` 与 `speed_deci_units` 表示“当前实际来源的主速度”；既有 GPS/controller 原始有效位继续保留供状态显示。32 位 `flags` 不扩展。

## 速度来源状态机

```text
preference = GPS                         -> active = GPS
preference = CONTROLLER + controller up -> active = CONTROLLER
preference = CONTROLLER + controller down/off -> active = GPS
```

- 在线判断使用已经投影的控制器连接句柄状态，不使用“速度字段有效”作为在线条件。
- active=CONTROLLER 且速度无效：`SPEED_VALID=false`，不切 GPS。
- active=GPS 且 fix 无效/超时：`SPEED_VALID=false`。
- 控制器连接状态变化、GPS 新句子/超时、速度单位变化、来源偏好变化都调用同一个主速度投影函数。

## NVS 与兼容迁移

- 新键使用短名称 `speed_src`，值为 `esp_bms_speed_source_t`。
- 加载时区分“键不存在”和“值非法”：
  - 不存在：读取旧 `ctl_page`，1→Controller、0→GPS；加载完成后补写 `speed_src`。
  - 非法：返回无效配置并沿用现有保存默认值流程。
- `ctl_conn` 继续独立保存，不再被页面结构强制清零。
- 保存时把 `ctl_page` 同步写为 `speed_source == CONTROLLER`，支持旧固件降级读取。
- `controller_page_enabled` 若继续保留在 runtime 内部，仅作为旧 action/NVS 兼容别名，不能再控制页面创建。

## 时间与平均电耗数学

### RMC 本地时间

- 严格校验 RMC 时间和日期后，通过纯函数将 UTC 加 8 小时；日期按公历闰年和月长进位。
- 每次校验通过的 RMC 更新本地时间；RMC 超时清除 snapshot 有效状态。

### 对齐规则

- 用 `esp_timer_get_time()` 的单调微秒时间戳记录 GPS RMC 和最近一次 BMS 完整状态帧。
- 首次有效 GPS 速度达到 5 km/h 只置 `started`；只有 GPS fix、包电压、电流有效且 BMS 样本不超过 2 秒才建立积分锚点。
- 相邻两个锚点间隔必须大于 0 且不超过 3 秒；否则丢弃区间并把当前样本作为新锚点。
- 任一端无效或失去新鲜度时断开锚点，防止恢复后补算缺失时段。

### 积分与单位

- 距离使用相邻 GPS knot 速度的梯形积分，累计为 `uint64_t` 毫米。
- 显示电流 `I_display_deciA = -I_bms_deciA`；功率使用相邻样本的梯形积分，累计为有符号 `int64_t` 微瓦时。回充形成负功率并抵扣能量。
- 距离达到 100000 mm 后才发布平均值。
- `Wh/km = energy_uWh / distance_mm`；英制按 1 mile = 1609344 mm 转换。snapshot 保存 0.1 Wh/单位，计算饱和到 `int32_t`。
- 单位切换只重新投影显示平均值，不修改累计距离或能量。

## LVGL 对象树与绘制

统一速度页仅创建一次对象树：

```text
speed_page
├─ speed_art (一个透明 lv_obj，LV_EVENT_DRAW_MAIN)
├─ speed_value / speed_unit
├─ soc / consumption
├─ controller_temp / motor_temp
├─ gear
├─ local_time
└─ scale_labels[5]
```

- `speed_art` 的绘制回调使用约 33 个内外曲线点，将每段四边形拆为两个三角形；无需像素 framebuffer 或 canvas。
- 32 段色带按当前速度比例着色。活动段在深蓝、蓝、浅蓝、近白间插值；未活动段深灰；最后危险区固定红色。比例先按 0..max clamp，主数字不 clamp。
- 同一回调用 line/triangle/rect 描述符绘制外轮廓、刻度、卫星、电池、状态点和电量斜段。
- 方向差异仅存在于 `speed_dashboard_apply_layout()` 的对象位置/尺寸及绘制 geometry 选择；创建与数据更新路径不含分散的横竖屏条件。
- 快速变化标签使用 `s_ui` 持久缓冲与 `lv_label_set_text_static`，仅内容变化时刷新；绘制状态变化时调用 `lv_obj_invalidate(speed_art)`。

## 页面映射

- 物理 scroll X：Battery=0、Speed=width、Cast=2×width。
- Controller/GPS 传入导航前都规范化到统一 Speed；从 scroll X 反推时返回一个固定 canonical page。
- 页面结构不再依赖 `CONTROLLER_PAGE_ENABLED`，因此来源切换不触发整屏重建。
- 统一页 stable data source 固定返回组合采集模式。

## 设置与 Web

- TFT 复用既有 settings card/row/helper 和 BLE popup，不改变导航栈。根行及详情标题改为“速度仪表”。
- 详情行按 controller online 动态组装：基础四行始终存在；专属参数只在线时追加，避免隐藏对象留下空白。
- Web select 的值直接为 `gps/controller`；`loadConfig()` 同时读取 active source 和 online 状态生成本地化提示，不通过提示反推配置。
- HTTP POST 在请求线程只校验并写 pending state；主循环消费 pending、更新 snapshot 并保存 NVS，沿用现有互斥锁边界。

## 风险控制与回退

- CRITICAL 设置入口：不改返回/滑动/BLE 回调签名，只替换行模型和在线条件；逐一回归 5 个直接调用者。
- CRITICAL 速度投影：所有来源变化统一经过该函数；逐一覆盖 6 个直接调用者，避免状态更新遗漏。
- RAM：绘制使用栈上固定描述符和小型点数组，无全屏缓冲；新增 runtime 累计状态为固定大小。
- 性能：仅 snapshot 变化时 invalidate；每次重绘约 64 个三角形加少量线段，目标 320×240，无动画定时器。
- 回退时删除新 NVS 键读取仍可由兼容 `ctl_page` 恢复旧页面偏好；不触碰其他持久化键。
