# 实施计划：GPS 基础接入

1. 在修改前读取 backend/frontend 规范，使用 GitNexus 对计划修改的 runtime/UI 符号执行上游影响分析；若风险为 HIGH/CRITICAL，先向用户报告。
2. 扩展 runtime 状态和 snapshot 的启动时长/PPS 诊断字段；实现严格的 RMC UTC 日期时间字段解析。
3. 初始化 GPIO35 输入与 PPS 上升沿 ISR；在运行时 tick 完成超时判定和限频日志，保证 ISR 不做日志。
4. 将 GPS UART 轮询从当前活跃轮播页条件中解耦，确保始终接收；保留其他数据源的轮询行为。
5. 实现 GPS 页速度与启动时长布局、格式化和 snapshot 更新，覆盖纵横屏与无定位状态。
6. 构建固件并运行现有测试；必要时用 NMEA/PPS 可控输入做单元/主机级验证。
7. 使用项目 LAN RFC2217 流程刷写并监视硬件日志：验证 RMC、PPS、页面切换连续性与无 GPS/PPS 的降级。
8. 运行 Trellis quality check、GitNexus `detect_changes()`，确认仅影响预期符号与流程；更新规范、提交前再检查工作区范围。

## 风险点与回退

- UART0 与日志冲突：限频日志、避免字节接收路径打印；若实际接收失败，先验证控制台 UART 配置而非改动 NMEA 协议。
- GPIO35 没有内部上下拉：必须依赖外部 PPS 电平/偏置；无法稳定触发时记录硬件条件而非软件强拉。
- UI 轮播切换：仅改 GPS 读取条件和 GPS 页，不改变页面顺序、手势或其他页面。
- 本期不引入 TF/FATFS 依赖，降低 SPI/启动绑定位风险；后续持久化作为独立任务。
