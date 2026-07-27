# 技术设计：仪表设置归位与 GPS 搜星信息

## Boundaries

- `esp_bms_gps_stream` 负责无 ESP-IDF 依赖的 NMEA 分类、校验和及 GSA/GSV 字段解析。
- `esp_bms_gps` 负责把有效 GSA/GSV 聚合结果发布给运行时，并维护卫星数据陈旧超时。
- `esp_bms_idf_runtime` 与 dashboard snapshot 只增加卫星聚合字段和有效性，不改变速度来源、NVS 或 Web API。
- `esp_bms_lvgl_ui` 新增独立仪表设置导航域；GPS、仪表、控制器详情各自只持有所属控件。

## Satellite Data Flow

1. UART 流继续按完整 `$...*HH` 行交付；流容量提高到可容纳接收机的长 GSV。
2. GSA 解析定位维度和非空卫星 ID 数；GSV 解析总可见数及当前分句中的最大 `C/N0`。
3. 每个校验和有效的 GSA/GSV 独立更新对应字段并刷新卫星数据超时；解析失败不污染 RMC 错误计数或最后有效值。
4. 模块离线或卫星数据超时后清除有效性，UI 显示 `--`；数值变化只重绘当前 GPS 根详情。

## Settings Navigation

- 新增 `SETTINGS_DETAIL_DASHBOARD` 和独立 `settings_dashboard_view_t`。
- 根入口在 GPS 或控制器任一启用时显示“仪表”。
- 仪表页显示已编译样式的选择器与速度单位；速度来源只在 GPS+控制器时出现。
- 现有选择器复用动作协议，但返回目标从 GPS/控制器状态判断改为仪表导航域。
- GPS 页只显示模块、定位、时间及四项聚合搜星信息；控制器页保留连接与车辆参数。

## Compile-Time Trim

- S1000RR 绘制回调对卫星图标调用和 GPS FIX 渲染签名使用 `#if ESP_BMS_FEATURE_GPS`。
- GPS 启用时保持原图标和颜色；禁用时不绘制、不保留占位图元、不因 FIX 位触发重绘。

## Compatibility

- 不修改 NVS key、LVGL action 枚举值、Web manifest、GPS RMC/PPS/A-GNSS 行为。
- snapshot 是固件内部跨组件结构；新增字段后由现有 ABI 静态检查和生产构建共同验证。
- 现有并行 GT1151Q 手势修改继续统一调用 `settings_navigate_back()`，新增仪表域接入该共享返回路径。

## Rollback

- 解析、快照、UI 三层按提交前 diff 可整体回退；不涉及持久化迁移或设备配置转换。
