# PCB走线信号完整性与四层回流优化 - 实施计划

## Phase 0 - 基线与回滚

- [ ] 回读当前 EasyEDA 工程/PCB UUID，确认唯一活动窗口和 PCB 文档。
- [ ] 复制当前 `.eprj2` 为 `pre-routing-si-*` 快照，记录字节数、SHA-256 和 SQLite `integrity_check`。
- [ ] 记录器件/焊盘/网络/线段/过孔/覆铜/禁布/DRC 基线。

## Phase 0A - 用户覆盖 FPC 槽长

- [x] 创建 `pre-slot-length-increase-20260720-103319.eprj2`，SHA-256 `ce540db23aae58277379ea247723e9c8c145c105c6048bbda43a8b1ff73de7ce`，SQLite `integrity_check=ok`。
- [x] 40Pin/6Pin 实体槽从 `3×22mm / 3×5mm` 分别加长为 `3×23mm / 3×6mm`，中心、宽度和 `R1.5` 圆端不变。
- [x] 同步把全层无铜包络扩展为 `4×24mm / 4×7mm`；器件禁布保持三侧约束，40Pin 上侧按 C16 实际外框采用 `0.9mm`，其余受约束侧保持 `1mm`。
- [x] 重建 L2_GND 覆铜，并以严格 45°局部重布 `3V3 / EFUSE_SS / GNSS_VBAT_CHG` 受影响线段；严格 DRC 的 Clearance/Keepout/Short 恢复为 0。
- [x] 保存、关闭、重开后回读为 132 个器件、411 个焊盘、89 个网络、2 个实体槽、12 个机械禁布区；严格 DRC 仅剩原有 `Connection Error (93)`。生成 `post-slot-length-increase-20260720-105340.eprj2`，SHA-256 `c1b857360fb5de4b89ab7fb4786c1e9015969b0a4e706d354e3e69b3912996f1`，SQLite `integrity_check=ok`。

## Phase 1 - 规则和层职责

- [ ] 确认 Top/L2_GND/L3_PWR_SIGNAL/Bottom 四层启用且其它内层禁用。
- [ ] 建立/检查 `POWER / USB_HS / GNSS_RF / LCD_I80 / I2S / LOW_SPEED` 网络类和网络归属。
- [ ] 保留 USB 两组差分对，将组内长度偏差锁定为不大于 0.254mm。
- [ ] 建立 LCD 串阻后 8 位数据 + WR 等长组，并记录 5mm 长度差目标。
- [ ] 绑定 GNSS RF 和 I2S 的专用线宽/间距规则；记录层叠假设和投板前阻抗复核门。

## Phase 2 - 地平面与回流网络

- [ ] 在 L2_GND 建立连续 GND 平面，保留 M3、FPC 槽、天线和现有机械禁布。
- [ ] 在 Top/Bottom 建立低优先级 GND 覆铜，排除天线和开关节点敏感区。
- [ ] 为接口/ESD/RF/层切换和板边添加必要的 GND 缝合/回流过孔。
- [ ] 检查 L2_GND 非 GND 线段为 0，覆铜没有非预期孤岛或窄颈。

## Phase 3 - 关键网络人工布线

- [ ] 按完整 API 签名调用嘉立创EDA自动布线生成整板初稿，记录成功/失败网络、线段、过孔和 DRC 变化。
- [ ] 审计自动结果的层职责、机械禁布、45°、线宽和过孔；删除 L2_GND 非 GND 走线及任何越禁布路径。
- [ ] 完成输入/eFuse、两路 Buck 和电源主干；检查热回路、SW/BST/FB 和线宽。
- [ ] 完成 GNSS RF 50Ω 拓扑路由，保持 Top-L2_GND 连续参考与天线净空。
- [ ] 完成 USB 两组差分路由，检查对内间距、等长、过孔、支路和串阻/ESD 顺序。
- [ ] 完成 LCD I80 和 I2S，检查串阻位置、长度差、并行间距和噪声源隔离。
- [ ] 完成 Class-D BTL 输出、局部去耦和大电流路径。

## Phase 4 - 已有走线修正

- [ ] 重建 `3V3_EXP` 的 68.792°/135.309° 线段为严格 45°路径。
- [ ] 缩短并重建 `DBG_UART_TX_GPIO43` 的 178.900°/103.542°/18.607° 线段。
- [ ] 缩短并重建 `DBG_UART_RX_GPIO44` 的 2.193° 线段。
- [ ] 对 7 个已布网络单独执行连通和 Clearance/Keepout 回归。

## Phase 5 - 普通网络收敛与后备路由

- [ ] 优先收敛嘉立创EDA自动布线后的剩余飞线和不合格普通路径。
- [ ] 若仍需后备路由，导出当前 DSN，保留机械禁布，从可路由层中排除 L2_GND。
- [ ] 仅对剩余普通网络使用 FreeRouting 生成 SES 候选路径。
- [ ] 对 SES 执行网络、层、线宽、过孔、禁布、45°和未路由数静态检查。
- [ ] 分批通过 EasyEDA 原生 Line/Via API 回写，每批执行严格 DRC 和回滚门。

## Phase 6 - 收敛与持久化

- [ ] 飞线/Connection Error 收敛为 0，处理全部 Short/Clearance/Keepout/长度/差分错误。
- [ ] 执行全板角度、长度、过孔、层分布、长平行间距和回流过孔检查。
- [ ] 保存、关闭、重开 PCB，重复全部回读和严格 DRC。
- [ ] 创建 `post-routing-si-*` 快照，校验 SHA-256 和 SQLite `integrity_check`。
- [ ] 更新 `docs/hardware/04-constraints.md`、`06-decisions.md`、`07-schematics.md` 与父任务 PCB 状态。

## Validation Commands / Evidence

- `curl http://127.0.0.1:49620/health`
- EasyEDA API：`pcb_PrimitiveLine.getAll()`、`pcb_PrimitiveVia.getAll()`、`pcb_PrimitivePour.getAll()`、`pcb_PrimitivePoured.getAll()`、`pcb_Net.getNetLength()`、`pcb_Drc.check(true,false,true)`
- EasyEDA API：主要器件/焊盘/板框/槽/禁布回读与基线对比
- 角度门：对每个有网络 Line 计算与 0/45/90/135° 的最小偏差，非规范计数必须为 0
- 层职责门：L2_GND 上非 GND Line 计数必须为 0
- `sqlite3 <snapshot> 'pragma integrity_check;'`
- `sha256sum <snapshot>`
- `node .gitnexus/run.cjs detect-changes --base main` 或等价 GitNexus `detect_changes(scope=compare, base_ref=main)`

## Rollback Points

- RP0：`pre-routing-si-*` 完整工程快照。
- RP1：规则和地平面完成后快照。
- RP2：关键网络手工布线完成后快照。
- RP3：每批普通网络回写前图元 ID 清单，只删除该批图元。
