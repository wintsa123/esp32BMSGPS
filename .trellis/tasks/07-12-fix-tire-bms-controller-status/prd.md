# 修复轮胎选择、BMS 电流语义与控制器状态显示

## Goal

让轮胎规格滚轮的当前选择清晰可辨；以用户视角表达 BMS 电流方向；并让已连接的 FarDriver 控制器立刻结束“连接中”提示、持续接收数据并在控制器轮播页显示遥测。

## Confirmed Facts

- 轮胎规格编辑页由 `settings_show_controller_tire_edit()` 创建三个普通 LVGL roller；`settings_controller_roller()` 仅以背景色高亮 `LV_PART_SELECTED`，没有视觉定位线。扁平比滚轮也使用该公共构造器（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:3539-3718`）。
- BMS 首页在 `set_dashboard()` 中将 `current_deci_amps` 原值传给 `format_deci_amps()`，同时把正值判断为 charging；原始 BMS 极性与用户要求的“用电为正、充电为负”相反（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:5515-5561`）。
- 连接确认后 UI 会无限期显示“连接...”；仅 BMS 在线会停止该提示，控制器在线不会（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1582-1633, 6057-6072`）。
- FarDriver GAP 已连接即播放“控制器已连接”语音，但控制器遥测订阅、通知解析和保活均限制为当前稳定页面是控制器页。页面切换和订阅完成的时序可使控制器页没有数据（`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3807-3811, 3953-3969, 5046-5125`）。
- 现有控制器页已包含速度、挡位、功率、RPM、控制器温度和电机温度标签；本修复必须保留离线时显示 `-` 的合同（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:5637-5679`）。

## Requirements

### R1 — 轮胎规格滚轮

- 三个轮胎规格 roller（包括扁平比）滚动时必须让设置导航栏保持显示，不触发标题隐藏/收缩。
- 选中行必须有清楚的中部定位标记：使用选中行上下两条强调线，且保持现有选中背景和文字对比度。
- 仍使用有界、非循环 roller，确认/取消的既有提交语义不变。

### R2 — BMS 电流语义

- BMS 原始电流只在 UI 投影时取反：用户看到正值表示用电/放电，负值表示充电。
- 电池图标和 SOC 填充的充电状态必须同步使用取反后的用户语义；不改动协议解析、runtime snapshot、Web API 或 NVS 中的原始值。

### R3 — 控制器连接状态与遥测

- 控制器连接成功并取得连接句柄后，UI 必须结束“连接...”转圈提示并显示短暂“绑定成功”；不等待 CCCD 发现或首帧遥测。
- 保持 FarDriver 特征的通知订阅和帧解析，不因用户暂时离开控制器轮播页而取消；控制器页进入时应已有最新缓存数据。
- 仍只在控制器页执行 2 秒主动 gather 保活，避免扩大非当前页面的主动 BLE 流量。
- 当控制器断开时，状态和遥测有效位仍必须按现有逻辑清除；不得将断线误报为在线。

## Acceptance Criteria

- [ ] 在轮胎规格页滚动任一 roller 时，导航栏不隐藏；扁平比 roller 的当前选中行具有明显上下定位线。
- [ ] BMS 显示电流中，原先的充电负值显示为负、原先的放电正值显示为正；充电时电池视觉状态正确。
- [ ] 接受控制器绑定后，已听到连接语音时“连接...”提示停止并显示成功反馈，设置页显示已连接设备。
- [ ] 控制器成功连接后，即使先停留在电池/GPS 页再切到控制器轮播页，也能显示接收到的 FarDriver 数据；未收到字段仍仅该字段显示 `-`。
- [ ] 断线后控制器页面保留但遥测恢复 `-`，不回归 BMS 连接、滚轮确认或设置返回手势。
- [ ] 针对性静态检查、`git diff --check`、完整 ESP-IDF 构建及 GitNexus 变更检测通过。

## Out of Scope

- 修改 FarDriver 协议字段、控制器参数写回、控制器首页的视觉重设计。
- 改变 BMS 原始协议、持久化或 HTTP/Web API 的电流符号。
