# 修复义雄安控制器蓝牙连接卡住

## Goal

让当前绑定的义雄安控制器 `YuanQuFOC972` 完成 BLE GATT 识别、订阅和
FarDriver 遥测连接，使 TFT 的“连接...”提示在成功或失败后按现有状态机收敛，
而不是因特征 UUID 不匹配反复连接和断开。

## Background

- 2026-08-14 真机日志确认目标为 ESP32-S3，控制器 MAC
  `FA:26:07:16:19:CA`，广播名 `YuanQuFOC972`。
- GAP 建链成功；目标不支持 Coded PHY，但该警告不阻断 GATT。
- Nordic UART 服务不存在，现有代码正确回退到 `FFE0` 服务。
- `FFE0` 服务句柄为 `22-25`，只发现一个句柄 24、properties `0x16`
  的特征。它具备 read/write/notify 能力，但当前代码只接受
  `FFEC` notify 与 `FFEF` write，最终记录
  `connection failed: profile=FFE0 stage=characteristic status=14` 并主动断开。
- 当前工作区已有 BLE 候选名称、UI、背光和 runtime 未提交改动；本任务必须保留。

## Requirements

- 保留 NUS 优先、FFE0 回退的既有顺序和状态机。
- FFE0 profile 继续接受既有 `FFEC` notify + `FFEF` write 双特征设备。
- FFE0 profile 同时接受目标设备实际提供的 `FFE1` 单特征；仅当该特征同时具备
  notify 与 write/write-no-response 能力时，才复用同一 value handle 作为收发句柄。
- 不使用任意 capability-wide 特征猜测，不新增通用 GATT 抽象或依赖。
- 继续通过 CCCD 写成功后才进入等待首帧；继续通过首个有效 FarDriver 仪表帧才
  投影在线，失败或超时按现有逻辑断开并结束“连接...”状态。
- 保留 Coded PHY 不支持时的兼容降级，不把它升级成连接失败。
- 新增或调整的日志保持每阶段一条，不输出敏感数据或逐包刷屏。

## Acceptance Criteria

- [ ] 目标真机日志不再出现 `stage=characteristic status=14`，而是发现 FFE1
  收发特征、找到 CCCD 并成功订阅。
- [ ] 收到有效 FarDriver 仪表帧后出现 `controller ready`，TFT“连接...”提示消失，
  控制器页面显示至少一个协议有效值。
- [ ] 若目标设备未返回有效帧，10 秒首帧超时仍主动断开且提示不永久停留。
- [ ] NUS 与既有 FFE0/FFEC/FFEF 行为不回归；不接受缺少 notify 或 write 能力的 FFE1。
- [ ] 最小 host/source contract 检查、完整固件构建和 `git diff --check` 通过。
- [ ] GitNexus `detect-changes --scope compare --base-ref main` 只报告预期 BLE
  profile/特征发现路径。
- [ ] 通过固定 RFC2217 端点烧录并监控，无 panic/watchdog；实际连接结果由日志验证。

## Out Of Scope

- 改写 FarDriver 帧解析、轮询命令或 TFT 弹窗实现。
- 修改扫描候选名称、分页、绑定 NVS 或默认蓝牙可发现策略。
- 为未知控制器实现通用特征能力探测。
