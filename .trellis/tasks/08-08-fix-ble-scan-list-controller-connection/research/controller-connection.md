# Research: controller connection state and settings visibility

- Query: 调查“控制器连接后专属设置瞬间隐藏”的根因，追踪控制器 BLE 扫描、绑定、GAP/GATT 连接、在线/类型识别、runtime snapshot 投影和 LVGL 设置显示条件。
- Scope: internal
- Date: 2026-08-08

## Findings

### Executive conclusion

这是两个问题叠加，不是 LVGL 动画或刷新防抖问题：

1. **已确认的状态语义根因**：runtime 在 GAP 建链拿到 `conn_handle` 后立即把 `CONTROLLER_ONLINE` 置 true，尚未验证 FarDriver 服务、notify/write 特征或 CCCD；GATT 发现失败后代码主动断开，`conn_handle` 被清除，在线位随即回 false。LVGL 正确地消费了这个 false -> true -> false 短脉冲，因此轮胎规格/传动比行先出现再消失。
2. **最高概率的实际断连根因**：当前固件只发现旧的 16-bit `FFE0/FFEC/FFEF`，但仓库内 APK 静态分析和已完成历史任务都记录现代 FarDriver 使用 Nordic UART 128-bit `6e400001/...0003/...0002`。当前源码明显回到了旧传输合同；对现代控制器，服务发现完成时找不到 `FFE0`，随后主动 terminate。

因此不能在 LVGL 里加延时、锁存在线状态或忽略离线 snapshot。那会违反真实断线回退合同。应修复 GATT 传输，并让“可显示控制器专属设置”的 ready 语义晚于协议识别。

### End-to-end state flow

1. 控制器蓝牙页使用 `START_CONTROLLER_BIND` 同时表达“无 MAC 时开始扫描”和“带 MAC 时绑定”：`settings_bms_ble_start_scan()` 排队 action，见 `components/esp_bms_lvgl_ui/ui_settings.c:611-620`；runtime 在无 `CONTROLLER_MAC_VALID` 时启动扫描，在有 MAC 时保存绑定、开启连接并启动自动连接，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:5402-5435`。
2. 控制器扫描把每一个可解析 MAC 的 BLE 广播都写入候选表，没有服务 UUID、名称或 manufacturer-data 过滤，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:556-617`、`:627-645`。绑定后的识别仅靠后续 GATT 发现；扫描阶段没有控制器类型识别。
3. 扫描再次看到绑定 MAC 时调用 `controller_connect()`；它取消 discovery，调用 `ble_gap_connect()` 并进入 `CONNECTING`，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:443-464`。
4. GAP connect 成功立即保存 `controller_conn_handle`、播放“控制器已连接”事件、进入 `DISCOVERING_SERVICE` 并按固定 `CONTROLLER_SERVICE_UUID` 开始发现，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:476-506`。
5. runtime snapshot 此时只用 `controller_conn_handle != 0xFFFF` 判定 `CONTROLLER_ONLINE`，完全不检查 `controller_ble_phase`、service/characteristic handles 或 `CONTROLLER_SUBSCRIBED`，见 `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:736-743`。
6. 主循环在 dirty/action/tick 变化后发布 snapshot，见 `main/idf_main.c:505-510`；display service 转交给 `esp_bms_lvgl_ui_update()`，见 `components/esp_bms_display_service/esp_bms_display_service.c:357-369`。
7. `apply_dashboard_snapshot()` 检测 `CONTROLLER_ONLINE` 变化并在控制器根页调用 `settings_show_controller_detail()` 重建对象树，见 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:966-1005`、`:1068-1079`。
8. `settings_show_controller_detail()` 以 `online` 决定主卡片是 1 行还是 3 行；轮胎规格和传动比只在 online 时创建，见 `components/esp_bms_lvgl_ui/ui_settings.c:1573-1586`、`:1610-1667`。所以 snapshot 的在线短脉冲会直接表现为“专属设置瞬间出现再隐藏”。
9. 服务、特征或 CCCD 发现任一失败都会无日志地调用 `ble_gap_terminate()`，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:334-358`、`:361-410`、`:413-440`、`:496-501`。disconnect 回调随后清空 handle、subscription 和 telemetry，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:517-525`，产生第二个 offline snapshot。

### Device type recognition is implicit and currently misleading

- 当前 snapshot 没有 controller type 或 protocol-ready 字段；runtime 结构只有 handle、GATT handles 和 `controller_ble_phase`，见 `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h:165-183`。
- LVGL 的“控制器类型”值固定写成“远驱”，不是识别结果，见 `components/esp_bms_lvgl_ui/ui_settings.c:1602-1608`。根设置入口也固定显示 `FarDriver`，见 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:839`。
- 因而当前真实识别门只有“能否发现目标 service + notify/write + CCCD”。但 UI 在该门完成前已经显示 online 和“绑定成功”，见 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1022-1025`。
- 特征能力 fallback 只发生在已经找到固定 service 之后，见 `components/esp_bms_controller_ble/esp_bms_controller_ble.c:69-72`、`:376-405`；它无法挽救 service UUID 本身从 Nordic UART 变成 `FFE0` 的不匹配。

### Ranked root-cause candidates

1. **High, code and historical evidence agree: FarDriver GATT transport regression/mismatch.** Current code declares `FFE0/FFEC/FFEF` at `components/esp_bms_controller_ble/esp_bms_controller_ble.c:63-67`. APK-backed task evidence says Nordic UART `6e400001`, notify `...0003`, write `...0002`, at `.trellis/tasks/07-26-fardriver-apk-protocol/prd.md:9-23` and `.trellis/tasks/07-26-fardriver-apk-protocol/design.md:3-21`. Archived completed child says firmware had already used Nordic UART at that time, at `.trellis/tasks/archive/2026-07/07-28-fix-fardriver-write-no-response/prd.md:7-13`; its task is completed at `.trellis/tasks/archive/2026-07/07-28-fix-fardriver-write-no-response/task.json:1-15`. Current code also reverted from APK five-byte read polling to old eight-byte open/keepalive commands (`components/esp_fardriver_protocol/esp_fardriver_protocol.c:6-13`, `:67-76`), so this is broader than a single UUID typo.
2. **Certain contributor to the visible flash: premature online projection.** Any GAP connection, including a foreign peripheral or incompatible FarDriver transport, creates online=true before discovery. This explains the UI transition exactly even without knowing the final disconnect reason.
3. **Medium: user can bind a non-controller candidate.** The scan list accepts all advertisements and the UI hardcodes type. In a dense BLE environment, selecting a plausible name can produce the same GAP-success/GATT-failure pulse. Advertising-service filtering may be unsafe because a valid controller may omit service UUIDs; protocol validation should remain the authoritative gate.
4. **Unknown pending logs: remote disconnect or a different GATT/CCCD failure.** The code has no phase/status/reason logs on its four active terminate paths and no controller disconnect-reason log. A real controller could expose the expected service yet fail characteristic/CCCD discovery. Current logs cannot distinguish these cases.

### Existing Trellis contracts and conflict

- `.trellis/tasks/07-12-fix-tire-bms-controller-status/prd.md:28-33` explicitly chose GAP handle acquisition as “connected” so the success toast would not wait for CCCD. That older decision created the false-positive semantics now observed.
- `.trellis/tasks/07-12-controller-ble-tire-ratio/design.md:51-58` gates controller settings editability on online state and requires snapshot changes to rebuild only affected views.
- `.trellis/tasks/07-26-fardriver-apk-protocol/task.json:1-24` remains `in_progress`, while its Nordic UART write-no-response child is archived `completed`. The live source no longer matches either task's APK-backed transport contract. The implementation agent should treat this as likely regression/spec drift and reconcile task ownership before duplicating protocol work.
- Frontend spec requires controller tire/ratio rows to follow controller online state and terminal transitions to stop the connecting toast: `.trellis/spec/frontend/component-guidelines.md` under “LVGL Settings Detail Rows”. The current task's R3/R4 supersede any temptation to latch stale online state.

### GitNexus evidence and blast radius

- `query()` found the controller scan flow `Controller_scan_gap_event -> runtime_project_controller_snapshot` and the key definitions `controller_connect`, `runtime_project_controller_snapshot`, and `settings_show_controller_detail`.
- `context(runtime_project_controller_snapshot)` reports 8 direct callers and participation in controller GAP/scan, runtime initialization, settings load and HTTP config flows.
- `impact(runtime_project_controller_snapshot, upstream)` is **CRITICAL**: 24 impacted symbols, 8 direct callers, 9 affected processes and 5 modules. A global online semantic change requires controller dashboard, speed-source fallback, toast, Web JSON and disconnect regression coverage.
- `impact(settings_show_controller_detail, upstream)` is **CRITICAL**: 36 impacted symbols, 5 direct callers, 12 processes and 3 modules. Patching visibility only inside this renderer is therefore both high-risk and symptom-level.
- `impact(apply_dashboard_snapshot, upstream)` is **CRITICAL**: 29 impacted symbols, 5 direct callers, 10 processes and 3 modules. Do not add UI hysteresis here.
- `impact(controller_gap_event/controller_service_cb/controller_dsc_cb, upstream)` reports LOW/0 callers because GitNexus does not model their NimBLE callback registrations as call edges. Source registration at `components/esp_bms_controller_ble/esp_bms_controller_ble.c:454-459`, `:496-499`, `:400-404` proves these are live hardware callbacks; the LOW result is an undercount, not low runtime risk.
- GitNexus `detect_changes(scope=all)` reports the existing dirty worktree as CRITICAL (18 changed symbols in 12 tracked files), but no changed symbol is in `esp_bms_controller_ble`, `runtime_project_controller_snapshot`, `apply_dashboard_snapshot`, `settings_show_controller_detail`, or the controller protocol test. The overlapping `esp_bms_idf_runtime.c` and snapshot header edits are unrelated BMS cycle-capacity/default-advertising changes and must be preserved.

### Recommended minimum repair

1. **Restore the APK-backed Nordic UART transport and its read-only polling contract first.** Reuse the already documented 07-26/07-28 implementation shape; do not invent a generic GATT adapter. If hardware logs prove this device is legacy `FFE0`, add only an explicit NUS-first/legacy-second transport fallback, not capability-wide discovery.
2. **Stop projecting protocol readiness from GAP handle alone.** The smallest existing-state gate is valid handle plus completed FarDriver discovery/subscription (`controller_ble_phase == ONLINE` or, without exposing the private enum, valid handle plus `CONTROLLER_SUBSCRIBED`). Clear the ready state on all discovery/subscription errors and disconnects. This removes the false online pulse without hiding a real disconnect.
3. Keep LVGL `settings_show_controller_detail()` driven by the corrected snapshot. Do not add timers, debounce, sticky rows or a second UI-only connection state.
4. Preserve the older fast feedback by separating text if product policy still wants it: GAP success can say “已连接，识别中”/continue the spinner, while “绑定成功” and controller-specific rows wait for protocol ready. TFT text must remain within the repository's font/language constraints; no new wording is required for the minimal fix.

### Logs needed on real hardware

Record one bounded line per transition, not per packet:

- selected/bound MAC and printable name;
- GAP connect status and handle;
- target service UUID and service discovery completion status/handles;
- notify/write characteristic UUIDs, properties and selected handles;
- CCCD discovery status/handle and CCCD write completion status;
- each local terminate reason tagged `service`, `characteristic`, `cccd`, or `subscribe`;
- disconnect reason, last phase, handle, and whether termination was local;
- snapshot edge `online old->new`, phase and subscribed flag.

Expected discriminators:

- GAP success -> service `EDONE` with start handle 0 -> current UUID mismatch/foreign candidate.
- Service found -> missing notify/write -> characteristic contract mismatch.
- Handles found -> no CCCD/write failure -> subscription compatibility failure.
- Phase ONLINE/subscribed -> remote disconnect reason -> real link/controller issue, not recognition.

### Minimum validation

1. Host protocol self-test must again cover APK compact/extended checksums and five-byte read request bytes; the current `tests/fardriver_protocol_selftest.c:96-105` only locks the reverted eight-byte open/keepalive commands.
2. Add one state-projection check for `CONNECTING/DISCOVERING -> online=false`, `protocol ready -> online=true`, and `DISCONNECT -> online=false`. It should fail under the current `conn_handle`-only predicate.
3. Extend the LVGL simulator with one controller-settings snapshot sequence: offline has no tire/ratio rows; protocol-ready online has both; disconnect removes them once. This checks rendering but must not be used as a substitute for runtime state testing.
4. Build the target firmware, then flash/monitor the real controller. Acceptance requires stable ready state plus at least one valid notification, not merely GAP handle acquisition or the audio event.
5. After implementation run GitNexus impact again for every changed symbol and `detect_changes(scope=compare, base_ref=main)`; the recommended runtime projection point is CRITICAL.

## Files Found

- `components/esp_bms_controller_ble/esp_bms_controller_ble.c` - scan candidates, binding match, GAP/GATT discovery, subscription, termination and disconnect state.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` - controller snapshot projection, action handling, dirty snapshot publication and connection settings.
- `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h` - runtime connection/GATT handles and BLE phase storage.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` - snapshot diffing, success toast and active settings view refresh.
- `components/esp_bms_lvgl_ui/ui_settings.c` - controller settings row visibility and shared BLE selection UI.
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` - public dashboard snapshot contract; it has online flags but no controller type/ready field.
- `main/idf_main.c` - runtime action application and snapshot publication.
- `components/esp_bms_display_service/esp_bms_display_service.c` - queued snapshot delivery to LVGL.
- `components/esp_fardriver_protocol/esp_fardriver_protocol.c` - currently reverted/legacy command and frame assumptions.
- `tests/fardriver_protocol_selftest.c` - current protocol check, which reinforces the legacy eight-byte command path.
- `.trellis/tasks/07-12-fix-tire-bms-controller-status/` - prior GAP-handle online decision.
- `.trellis/tasks/07-12-controller-ble-tire-ratio/` - settings/snapshot state-machine contract.
- `.trellis/tasks/07-26-fardriver-apk-protocol/` - APK-backed Nordic UART and frame/polling evidence; still in progress.
- `.trellis/tasks/archive/2026-07/07-28-fix-fardriver-write-no-response/` - completed child documenting Nordic UART firmware behavior.

## External References

- No network documentation was needed. The transport evidence used here comes from the repository's prior static analysis of local `far.apk` (`DEX La/Ul;.<clinit>`, `La/El;.onServicesDiscovered`, and `libfd_protocol.so` offsets) recorded in `.trellis/tasks/07-26-fardriver-apk-protocol/design.md:3-12`.
- GitNexus index: `esp32BMSGPS`, indexed 2026-08-08, 4211 symbols / 300 processes. The index sees the current working tree but does not model NimBLE callback registrations as incoming calls.

## Related Specs

- `.trellis/spec/frontend/component-guidelines.md` - controller settings rows, BLE list reuse, snapshot rebuild and terminal connection-state rules.
- `.trellis/spec/frontend/quality-guidelines.md` - LVGL host/simulator quality gates.
- `.trellis/spec/backend/logging-guidelines.md` - bounded, actionable runtime logging.
- `.trellis/spec/backend/quality-guidelines.md` - target build and validation expectations.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - end-to-end data-flow verification.

## Caveats / Not Found

- No real controller log or packet capture was available in this research turn, so the final disconnect branch (service missing vs characteristic/CCCD failure vs remote disconnect) is not directly observed. The false online pulse is code-confirmed; the Nordic UART mismatch is the highest-probability underlying cause.
- The prior APK protocol parent task remains `in_progress`, while a child claiming Nordic UART write behavior is completed and the current source no longer matches it. This inconsistency should be resolved during implementation rather than assuming the archived child is the live baseline.
- Current scan advertisements are not sufficient to prove type. Do not require advertised service UUID unless hardware evidence shows the controller always includes it.
- No product code, spec file or task metadata was modified during this research.
