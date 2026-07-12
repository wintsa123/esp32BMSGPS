# Design

## Boundaries

- `esp_fardriver_protocol`：纯 C 协议解析、参数有效性、轮胎周长/传动比与车速派生。
- `esp_bms_idf_runtime`：NVS 兼容迁移、控制器参数同步、BLE 候选/绑定、snapshot 投影和 action 应用。
- `esp_bms_lvgl_ui`：BMS/Controller 参数化蓝牙子页、动态控制器设置布局、roller 草稿与确认交互。
- `esp_bms_lvgl_contract`：编译期 LVGL widget 能力检查；数据合同继续由 `esp_bms_lvgl_ui.h` 提供。

## Protocol Contract

- 扩展帧仍按 6 个连续 16-bit block 存入 `blocks[address]`。
- 只有 `D2`、`D3`、`D4` 三个 block 全部有效时才尝试解析控制器速度参数。
- `D2` 提供扁平比与轮辋，`D3` 提供胎宽，`D4` 提供 `RateRatio`；具体字节按 FarDriver 小端 block 合同读取并由自检锁定。
- 轮胎外径：`rim_inch * 25.4 + 2 * width_mm * aspect_percent / 100`；周长为外径乘 π并四舍五入到毫米。
- 传动比 centi 值为 `round(RateRatio * 100 / 60)`。
- 速度 deci-km/h 为 `rpm * circumference_mm * 60000 / (ratio_centi * 1,000,000)`；使用 64 位中间量并检查零值。
- 控制器参数有效时优先派生；否则使用本机 fallback 周长和传动比。
- 参数暂时无效时清除控制器参数有效位，但保留 block 缓存和本机 fallback；解析失败不修改状态。

## Snapshot and Action Contract

- snapshot 分开表达：
  - 实际用于计算/展示的控制器参数；
  - 本机备用轮辋、扁平比、胎宽、传动比；
  - 参数来源（未设置、旧周长、本机规格、控制器同步）。
- 新增绝对设置 action，并追加 enum 数值；event 使用独立 valid flag 和定宽字段，避免 UI/runtime 对裸 payload 做隐式解释。
- roller 页面只持有本地草稿。确认构造一次 action event；取消只导航，不发 action。

## Runtime and NVS Compatibility

- 保留 `ctl_wheel` 与 `ctl_ratio`；新增 `ctl_rim`、`ctl_aspect`、`ctl_width`。
- 加载顺序：
  1. 三个新规格键全部存在且范围有效时，以规格计算 fallback 周长。
  2. 新规格缺失而旧 `ctl_wheel` 有效时，保留遗留周长并标记旧来源。
  3. 无有效规格/旧周长时，轮胎未设置。
  4. `ctl_ratio` 缺失或无效时使用 `1.00`。
- 保存新规格时同时刷新兼容周长键，避免旧固件读到过期值；未设置规格时保存零值语义。
- 控制器同步只在完整有效参数元组发生变化时处理。范围内值可同步到本机规格/传动比并调度一次持久化；范围外值只读使用并输出一次拒绝同步日志，不覆盖 fallback。
- 保存继续通过现有延迟/dirty 机制聚合，禁止通知回调直接每帧写 NVS。
- restore defaults 清空规格/遗留周长并把 ratio 设为 100，保持绑定 MAC/名称。

## BLE Selection Reuse

- 引入蓝牙选择数据源枚举 `BMS` / `Controller`，单一页面状态保存当前 source、待确认候选和是否打开。
- 状态格式化、候选数组选择、bound name/MAC、scan action、bind action 由小型 source adapter 提供。
- 页面对象树、刷新按钮、候选行、确认弹层和返回逻辑仅保留一套。
- pending candidate 复制名称和 MAC 到 UI 固定缓冲，不引用 snapshot 临时内存。
- 控制器主卡片只显示蓝牙选择入口，不渲染扫描候选。

## Controller Settings State Machine

- `ROOT`：控制器设置主卡片；page off 创建 2 行，page on 创建 6 行。
- `BLE_LIST`：共享蓝牙选择页。
- `TIRE_EDIT`：三个有界普通 roller；未设置草稿为 `12/70/90`。
- `RATIO_EDIT`：单个有界普通 roller；默认草稿 `1.00`。
- snapshot 更新只在影响当前对象树/文本/disabled 状态的字段变化时更新，不因 RSSI 或遥测高频字段重建。
- 离线为 `LV_STATE_DISABLED`；在线且 controller source 有效时为 disabled + 只读来源；在线且无参数才允许打开编辑页。

## UI and Localization

- 复用既有设置卡片、导航、返回手势、确认弹层和绿色主按钮样式。
- roller 使用 normal mode 与有限选项字符串，不允许无限循环。
- 新中文字符串加入现有字体字符集合或重新生成对应 10/13/16 字体，并运行缺字审计。
- 新预览如有更新只写入仓库根目录 `preview/`。

## Risk and Rollback

- snapshot 投影与滚动收尾是 CRITICAL 路径；action 派生、协议派生和设置渲染器为 HIGH 风险，必须在编辑前逐符号跑 GitNexus upstream impact。
- 修改顺序固定为协议测试 → runtime/NVS → UI，以便每层独立验证。
- 协议变更可通过自检失败立即回滚；NVS 新键为可选键，不要求破坏性迁移。
- BLE 参数化若失败，可回退 adapter 而不改变 runtime BMS/Controller 扫描实现。
- 不覆盖当前工作区的控制器首页、字体、设置滚动与卡片收缩改动。
