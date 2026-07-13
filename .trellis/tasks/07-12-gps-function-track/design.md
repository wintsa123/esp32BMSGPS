# 设计：GPS 基础接入

## 边界

运行时组件拥有 UART、NMEA 解析、GPIO35 PPS 采样和 snapshot 投影；LVGL 组件只根据 snapshot 绘制 GPS 页。日志由运行时主循环输出。不上 TF 卡、NVS 或 Web API。

## 数据流

`UART1 RX/GPIO27 → RMC 校验/解析 → runtime GPS 状态 → dashboard snapshot → LVGL GPS 页`

`GPIO35 PPS 上升沿 → 极短 ISR（记录计数/最近 tick） → runtime tick 的失效检测和限频日志`

UART 数据无论当前轮播页为何都持续轮询。`active_data_source` 仍可用于 BMS/控制器的协议轮询，但不能再决定 GPS UART 是否读取。

## 状态与契约

- 扩展 runtime 私有状态：最近有效 RMC 的 UTC 日期/时间、PPS 计数、最后 PPS 的单调时钟、首次/恢复/超时日志状态。
- 扩展 `esp_bms_dashboard_snapshot_t`：GPS 页需要的本次启动秒数；速度继续复用 `SPEED_VALID` 与 `speed_deci_units`，不复制速度来源。
- RMC 解析扩展为读取字段 1（UTC 时分秒）和字段 9（日期），只有校验和、语句类型、字段格式均合法时才发布授时日志数据。
- GPIO ISR 不调用 `ESP_LOG*`、不分配内存、不操作 LVGL；仅写最小的 ISR 安全状态。运行时 tick 用超时窗口（建议 3 秒）判定 PPS 丢失并限频记录状态变化。

## UI

在既有 `gps_page` 创建速度主数字、单位、启动时长和 ASCII 状态文字。布局同时适配当前纵横屏尺寸；不新增中文 TFT 文案。UI 更新只在 snapshot 变化时改动标签文本。

## 授时日志

日志仅用于调试：首次获得有效 RMC 时打印 UTC，PPS 首次/恢复/丢失时打印状态；稳定状态最多每 60 秒汇总一次。这样既能验证 PPS 与 NMEA，又避免 UART0 被每秒日志占满。

## 兼容与回退

GPIO35/PPS 配置失败只记录警告，GPS UART 与 UI 继续工作。UART0 与控制台冲突时保留现有保护逻辑。移除本改动即可恢复原先行为，不触碰持久化数据格式。
