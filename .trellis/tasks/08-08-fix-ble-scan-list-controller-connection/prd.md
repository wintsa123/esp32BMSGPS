# 修复蓝牙扫描列表与控制器连接状态

## Goal

让密集 BLE 环境中的 BMS/控制器候选可完整浏览并保留真实广播名，同时消除控制器仅完成 GAP 建链时产生的“专属设置瞬间出现后隐藏”，使专属设置只跟随已完成 FarDriver GATT 识别与订阅的真实可用状态。

## Background

- 当前公共候选上限为 6，BMS 和 Controller 的 runtime、snapshot、名称缓存及 TFT 列表都受同一上限约束；超过上限的 MAC 会被丢弃。
- TFT 列表仅在 snapshot 已无有效名称时生成 `设备 N`；名称丢失发生在广播解析/候选存储上游，而不是渲染器主动替换。
- 名称过滤当前只检查原始名称前 24 字节，并删除 `"` 与 `\`。前 24 字节为不可显示字节而后面存在 ASCII 时会错误得到空名；直接放开两个符号又会破坏当前未转义的 HTTP JSON。
- 控制器 GAP 建链后立即以 `conn_handle` 投影 `CONTROLLER_ONLINE=true`，但 service/characteristic/CCCD 失败会主动断开，造成 UI 的在线短脉冲。
- 当前源码只使用 `FFE0/FFEC/FFEF`，仓库 APK 研究和已完成历史任务则记录 Nordic UART 128 位 `6e400001/...0003/...0002`；两套 profile 都有项目证据。
- 当前工作区已有未提交的 BMS cycle capacity、BLE advertising default、snapshot ABI 和 UI 改动；实施必须保留并兼容这些修改。

## Requirements

### R1 - 候选容量与更多设备页

- BMS 与 Controller 必须继续共用一套候选合同、source adapter、列表渲染、确认弹层和返回链路。
- 单次扫描最多保留 12 个按首次发现顺序去重的候选；超过 12 个继续使用现有有界忽略策略，不引入动态容器。
- 候选不超过 6 个时，现有蓝牙列表直接显示全部候选。
- 候选超过 6 个时，首层显示前 5 个候选和 ASCII 入口 `More devices`；二级页显示第 6 至第 12 个候选。
- 二级页返回先回蓝牙首层，再返回 BMS/Controller 设置根页；刷新、选择、确认和取消语义保持现有行为。
- 兜底名称编号使用绝对候选序号，分页后不得重复或选择错误 MAC。

### R2 - 广播名称

- 有效 local-name 字段中的英文、数字、空格和所有 TFT 可显示的可打印 ASCII 符号必须按原顺序保留，最长显示 24 字节。
- 名称归一化必须遍历完整输入后再按输出容量截断，不能因前 24 个原始字节不可显示而丢失后续 ASCII。
- 同一 MAC 的有名 advertising/scan-response 必须补全先前无名候选；后续无名报告不得擦除已缓存名称。
- 空名称或没有任何可显示字符的名称继续使用稳定的 `设备 N` 占位。
- 接受 `"` 与 `\` 时，`/api/bms/candidates` 必须正确 JSON 转义并可被标准解析器往返解析。
- 不为没有 local-name 字段的外围设备伪造名称；仅用有界、低频诊断记录解析失败所需证据。

### R3 - 控制器连接与识别

- Nordic UART 128 位 profile 作为首选，`FFE0/FFEC/FFEF` 作为显式兼容回退；不得用任意 capability-wide 特征猜测替代已知 profile。
- 每个 profile 使用其已有且已验证的命令节奏：Nordic UART 五字节只读轮询；`FFE0` profile 使用现有 open/keepalive 推送合同。
- GAP 建链仅代表“识别中”，不得发布 `CONTROLLER_ONLINE` 或显示控制器专属轮胎/传动比设置。
- 找到匹配 service、notify/write characteristic、CCCD 且订阅成功后，仅进入协议等待阶段；只有收到并成功解析首个实际更新转速、电流、控制器温度或电机温度等仪表遥测有效位的 FarDriver 帧，才发布 `CONTROLLER_ONLINE`、播放已连接提示并显示控制器专属设置。仅参数块或其它无仪表数据帧不足以判定在线。
- 订阅后若在有界时间内始终没有有效 FarDriver 帧，必须记录 profile/阶段并主动断开进入现有 backoff，避免伪在线和空白仪表。
- service/characteristic/CCCD/subscribe 失败和真实断开必须立即清除 ready 状态、订阅状态和遥测，按现有离线语义回退。
- 每个连接阶段、主动终止点和断连原因最多记录一条可操作日志，不增加逐包日志。

### R4 - 兼容与范围控制

- 不修改 07-24 已建立的单一 NimBLE scanner 仲裁，不改变 BMS/Controller 候选隔离、绑定 NVS 或 action 数值。
- 不新增依赖、通用 GATT 框架、UI 防抖、粘性在线状态或预留式抽象。
- 新增 TFT 文案仅使用 ASCII；不引入中文字体。
- 提高候选上限必须通过 legacy ESP32 无 PSRAM 构建和真机 heap/最大块检查。

## Acceptance Criteria

- [ ] AC1：注入 0、6、7、12 个 BMS 候选时，0/6 项直接显示，7/12 项出现 `More devices`，二级页能选择正确绝对索引和 MAC。
- [ ] AC2：同样的 0/6/7/12 边界在 Controller source 下通过，且返回顺序为更多页 -> 蓝牙首层 -> 控制器设置。
- [ ] AC3：`ANT-BMS_01`、`JK BMS #2`、包含 `"`/`\` 的名称、前缀不可显示但后缀为 ASCII 的名称均保留预期文本；空名/全不可显示名称仍稳定兜底。
- [ ] AC4：HTTP candidates 响应中的 `"`/`\` 名称可由标准 JSON 解析器解析并往返一致。
- [ ] AC5：控制器 GAP 成功、GATT 发现或仅完成订阅时，专属设置均不出现；收到首个有效 FarDriver 帧后持续出现；断开后立即隐藏。
- [ ] AC6：Nordic UART 128 位和 `FFE0` 16 位 profile 的选择、特征、命令合同均有最小可运行检查，失败不会误报在线。
- [ ] AC7：host self-tests、目标固件完整构建、`git diff --check` 和 GitNexus `detect_changes --scope compare --base-ref main` 通过；既有用户改动保持不变。
- [ ] AC8：通过固定 RFC2217 端点烧录并监控，启动、BMS/Controller 扫描、实际控制器识别/订阅/遥测无 panic、watchdog 或在线短脉冲；记录 legacy heap/最大块证据。
- [ ] AC9：实际控制器进入在线后，控制器仪表至少出现一个协议有效数值；仅收到参数块、无效或不完整通知时保持离线，并在超时后断开且日志可定位 profile 与阶段。

## Out of Scope

- 支持超过 12 个并发候选或改为动态分配。
- 重写 NimBLE 扫描仲裁、绑定持久化或 FarDriver 遥测字段。
- 通过 GATT 连接每个扫描候选以读取 Device Name。
- 新增 BMS/控制器协议、控制器调参或参数回写。
- 恢复当前缺失的完整 LVGL host simulator 工程；若仍缺失，只记录该门禁不可运行并使用现有 host/build/hardware 检查。

## Technical Notes

- `settings_bms_ble_refresh_rows` 的 GitNexus upstream impact 为 CRITICAL：3 个直接调用者、8 条流程、3 个模块；实施必须先扩展最小 UI smoke，再修改并验证 display service/page transition 回归。
- `runtime_project_controller_snapshot` 同为 CRITICAL；在线语义改动需覆盖 dashboard、speed source、toast、Web status 和断开路径。
- BLE 名称复制和 NimBLE 回调符号的静态 impact 为 LOW，但 callback 注册不一定被调用图完整建模，仍按硬件高风险验证。
