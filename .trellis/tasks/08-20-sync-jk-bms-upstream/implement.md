# Implementation Plan

1. 读取相关 Trellis 后端规范，并用 GitNexus 对所有待修改符号执行 upstream impact 分析；HIGH/CRITICAL 时先告警。
2. 从上游 `60e28f2` 提取三种协议的最小真实帧夹具和字段差异，记录来源，不复制 ESPHome 业务层。
3. 在 JK 协议模块加入协议枚举、连接级识别状态、设备信息解码和稳健的 300/320 字节流装配。
4. 修正 JK02 24S/32S 与 JK04 的遥测偏移、比例、符号及错误位宽，保持现有遥测接口。
5. 在 BLE 驱动中按 `FFE1` 属性匹配写入和通知句柄，并在断线时重置 JK 协议识别状态。
6. 扩展 `tests/jk_bms_protocol_selftest.c`，必要时增加一个最小 BLE 特征选择自测；覆盖三协议、设备信息、分片、坏校验和及尾随数据。
7. 运行格式化、JK 目标自测、相关组件构建/静态检查；按固件影响任务使用 LAN RFC2217 技能烧录并观察运行日志。
8. 运行 GitNexus `detect_changes({scope: "compare", base_ref: "main"})`，确认影响范围后执行 Trellis 检查和收尾。

## Rollback Points

- 协议解析与 BLE 特征选择分开提交或保持可独立回退。
- 不迁移持久化格式，不需要数据回滚。
