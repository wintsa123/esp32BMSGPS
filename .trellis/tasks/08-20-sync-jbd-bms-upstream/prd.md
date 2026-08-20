# 检查并同步 JBD BMS 上游协议

## Goal

TBD.

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
# 检查并同步 JBD BMS 上游协议

## Goal

以 `syssi/esphome-jbd-bms` 当前 `main`（调研基线 `99cf7c15dbc2056295c88abc3fae7c1d83e6fb7f`）为依据，确认本项目 JBD BLE 基础遥测协议是否过时，并明确最小必要同步范围。

## Confirmed Findings

- 本项目已实现 `DD A5 <function> <len> <payload> <checksum:u16> 77` 基础帧。
- 已使用上游默认 BLE 资源：服务 `0xFF00`、通知特征 `0xFF01`、写入特征 `0xFF02`。
- 已轮询 `0x03` 基本信息和 `0x04` 单体信息，并验证大端补码校验。
- 已跳过 `FF AA` 认证帧，但没有认证状态机。
- 上游新增认证流程、`0x05` 硬件版本、`0xAA` 错误计数和多个写寄存器动作。
- 上游按长度字段消费普通帧和认证帧，避免通知错位。

## Scope Decision Needed

推荐本轮只同步无写入风险的读取兼容：严格修正 JBD 可变长度/认证帧装配，补充 `0x05`/`0xAA` 的安全忽略或诊断解析，并补齐真实抓包自测；暂不实现密码认证和写寄存器。

当前固件公共遥测接口没有认证/写入需求，直接加入密码和写保护动作会扩大安全与误操作风险。若需要通过固件修改 JBD 参数，再单独规划认证凭据、加密流程和写入 UI。

## Acceptance Criteria

- 记录本项目与上游 JBD 协议差异及基线提交。
- 基础 `0x03/0x04` 遥测行为保持兼容。
- `DD` 普通帧和 `FF AA` 认证帧按各自长度独立消费，分片/尾随通知不会错位。
- 上游 `0x05`/`0xAA` 帧不会误报普通遥测或卡死。
- 固定帧测试覆盖 CRC、长度、认证帧和基础遥测。
- 不新增第三方依赖，不持久化密码，不实现未明确要求的写操作。

## Out of Scope

- JBD 密码认证、根密码、应用密钥和加密写入。
- 写寄存器、MOSFET、均衡器、SOC 重置等控制动作。
- JBD-UP 多设备链路或 UART 专用扩展协议。
