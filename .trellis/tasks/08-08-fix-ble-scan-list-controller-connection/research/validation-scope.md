# Research: validation scope and minimum implementation boundary

- Query: 调查本任务可复用的扫描/列表/连接机制、自动化与真机验收边界，并检查未提交改动冲突。
- Scope: internal
- Date: 2026-08-08

## Findings

### Executive conclusion

本任务应拆成三个窄边界验证，不能用一个 UI 状态补丁覆盖全部症状：

1. **列表容量/二级页**：只扩展现有共享 BLE 选择 UI。BMS 与 Controller 已共用 source adapter、列表渲染、点击索引、确认弹层和返回链路，不应修改 07-24 已完成的 NimBLE 扫描仲裁。
2. **广播名**：现有 BMS、Controller 和 runtime 三条路径已经保留可打印 ASCII，并缓存 scan response 后补到同 MAC 候选。先用明确样本做回归；普通英文、数字、`-_.:/` 等若仍变占位名，根因不在列表渲染器。不要先重写名称策略。
3. **控制器专属设置闪现**：已有源码足以解释症状。snapshot 在 GAP 连接句柄刚建立时就发布 `CONTROLLER_ONLINE=true`，早于 FarDriver service/characteristic/CCCD 识别；识别失败后的真实断连又将其清除。正确的最小修复点是 runtime 的在线状态投影，不能在 UI 中延迟隐藏或伪造在线。

若“完整扫描结果”只表示当前有效上限内的全部候选，最小实现应保留 6 个候选上限，仅把首屏放不下的条目放到“更多设备”子页。若产品要求同时发现并保留 **超过 6 个**设备，则需单独确认容量与 RAM 预算；这不是纯分页改动。

### Existing task evidence that can be reused

- 07-12 已建立单一 BLE 选择页和 `BMS` / `Controller` source adapter，要求候选数组、状态、scan/bind action 只换数据源，不复制渲染器；pending candidate 复制到 UI 固定缓冲，避免 snapshot 更新后悬空（`.trellis/tasks/07-12-controller-ble-tire-ratio/design.md:43-49`）。本任务应继续沿用。
- 07-12 已定义 Controller `ROOT/BLE_LIST/TIRE_EDIT/RATIO_EDIT` 状态以及 snapshot 变化驱动重绘（同文件 `:51-58`）。不需要新导航框架。
- 07-12 的验证记录证明完整 ESP-IDF build、RFC2217 flash 和正常启动可作为基线，但明确没有完成真实 FarDriver 广播/连接/参数同步（`.trellis/tasks/07-12-controller-ble-tire-ratio/implement.md:53-70`）。因此旧记录不能替代本任务的真机控制器验收。
- 07-24 已确认 NimBLE discovery 是唯一共享资源，BMS/Controller 候选与状态必须隔离，并完成 requested/active 扫描交接合同（`.trellis/tasks/07-24-fix-controller-ble-legacy-display/prd.md:18-24`）。除非新证据显示仲裁回归，本任务不应再改扫描所有权状态机。
- 07-29 已确认 `runtime_init_ble_host()` 是 CRITICAL 路径，BMS 扫描、Controller 扫描和本机广播必须一起回归（`.trellis/tasks/07-29-fix-ble-scan-init-panic/prd.md:9-14`）。其 host self-test/build 已通过，但 RFC2217 scan-path 验证当时仍 pending（`.trellis/tasks/07-29-fix-ble-scan-init-panic/implement.md:18-26`）。

### Reusable code patterns

#### Shared BLE selection UI

- 公共候选合同当前上限为 6，BMS/Controller snapshot 各持有一组相同候选类型：`components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h:235-236`, `:336-360`。
- `settings_show_bms_ble_popup()` 已按 source 选择 BMS 或 Controller view，并建立唯一的状态栏、刷新按钮和候选列表对象：`components/esp_bms_lvgl_ui/ui_settings.c:658-730`。
- `settings_bms_ble_refresh_rows()` 已按 source 选择候选数组并复用格式化逻辑：`components/esp_bms_lvgl_ui/ui_settings_system.c:13-31`；现实现遍历全部 6 项并生成一段静态文本：同文件 `:51-83`。
- 点击通过行高计算 index，再按 source 取候选并进入同一确认弹层：`components/esp_bms_lvgl_ui/ui_settings_system.c:360-401`。
- 顶部/边缘返回已根据 BMS/Controller 子状态回到各自 root：`components/esp_bms_lvgl_ui/ui_settings.c:257-283`。
- snapshot 刷新只在候选身份、名称、数量或控制器视图相关字段变化时更新列表；RSSI 变化不会重建对象树：`components/esp_bms_lvgl_ui/ui_settings.c:2034-2062`, `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:997-1005`, `:1068-1079`。

**最小分页边界建议**：继续使用这一套对象树和 source adapter，只增加一个共享页偏移/是否在 more 页的 UI 状态，并让渲染与点击都用 `absolute_index = page_offset + row_index`。主页面仅追加一个 ASCII 可显示的 `More >`/既有字体可支持入口；返回 more 页时先回主 BLE 列表，再从主列表回设置 root。不要为 BMS 和 Controller 各复制一页，也不要改 scan action、snapshot 来源或候选确认合同。

首屏条数必须由 320x240 最小布局验证后确定。当前 landscape 候选行高为 48 px、portrait 为 56 px（`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui_internal.h:102-112`）；直接渲染 6 行会产生 313 px 的列表高度（`components/esp_bms_lvgl_ui/ui_settings.c:725-731`），超过 legacy landscape 可视区。

#### Broadcast name handling

- BMS GAP 路径对广告和 scan response 使用同一 DISC 处理，保留 `0x20..0x7e` 中可显示字符并跳过不可显示字节：`components/esp_bms_bms_ble/esp_bms_bms_ble.c:901-925`, `:982-1005`。
- Controller 路径有等价处理：`components/esp_bms_controller_ble/esp_bms_controller_ble.c:192-217`, `:620-640`。
- runtime BMS 候选路径还有第三份等价清洗及按 MAC 名称缓存：`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:932-958`, `:1017-1047`, `:1050-1124`。
- UI 仅在 `has_name && name[0] != '\0'` 为假时生成 `设备 N`，不会主动覆盖有效 ASCII：`components/esp_bms_lvgl_ui/ui_settings_system.c:57-75`。
- 当前过滤器额外删除 `"` 和 `\\`，因为 BMS candidates HTTP JSON 仍用未转义 `%s` 写名称：`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3257-3278`。若验收把这两个字符也定义为“常用符号”，必须同时补 JSON escaping；不能只放宽 BLE 过滤。

因此，普通 ASCII 名称已由现实现覆盖。最小实现应先留一个失败样本（原始 advertising/scan-response bytes、name length、MAC），再决定是否改 parser。若只是增加测试，不要为了三份重复代码立即引入新依赖或新框架；只有确实修改清洗规则时，才值得把纯 ASCII 归一化收敛为一个可 host-test 的共享 helper。

#### Controller connection truth

- GAP connect 成功立即写入 `controller_conn_handle`，随后才开始 FarDriver service discovery：`components/esp_bms_controller_ble/esp_bms_controller_ble.c:476-500`。
- 完成 service、characteristic 和 CCCD discovery 后才把 phase 设为 `CONTROLLER_BLE_PHASE_ONLINE` 并发起订阅：同文件 `:334-355`；phase 定义位于 `:42-51`。
- 当前 snapshot 却仅以 `controller_conn_handle != 0xffff` 设置 `CONTROLLER_ONLINE`：`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:736-743`。这会在“物理链路已连、FarDriver 身份尚未确认”的窗口提前显示在线。
- 识别失败会 terminate；真实 disconnect 清句柄、phase、订阅标志并清遥测：`components/esp_bms_controller_ble/esp_bms_controller_ble.c:517-532`。UI 根据 `CONTROLLER_ONLINE` 在 1 行与 3 行布局间切换：`components/esp_bms_lvgl_ui/ui_settings.c:1579-1586`, `:1610-1667`，故会表现为专属设置短暂出现后消失。

**最小修复边界建议**：让 `CONTROLLER_ONLINE` 代表既有 `CONTROLLER_BLE_PHASE_ONLINE`（必要时再要求既有 `CONTROLLER_SUBSCRIBED`），保留 disconnect 立即回退。不要在 `settings_show_controller_detail()` 保存“曾经在线”状态。真实 FarDriver 若在完成识别后仍断开，应继续隐藏，符合 R4。

订阅写回调目前只记录失败，不清订阅标志或终止连接：`components/esp_bms_controller_ble/esp_bms_controller_ble.c:263-276`。若真机日志显示 CCCD write 异步失败却仍被标记在线，这属于同一真实状态合同，需要在 BLE 层处理；没有该证据时不要预先扩大修复。

### Automated acceptance boundary

可自动化：

- 名称规则：纯 helper/self-check 覆盖英文、数字、`-_.:/`、空名、全不可显示字节、混合 UTF-8+ASCII、24 字节截断和 NUL 终止。仅 snapshot 注入测试不能证明 advertising parser 正确。
- 分页映射：覆盖 count `0`、首屏容量、首屏容量+1、最大 6；分别验证 BMS/Controller 的 more 入口、绝对 index、确认候选 MAC、more->主列表->root 返回顺序，以及 snapshot 刷新后 offset 不越界。
- UI 可见性：注入 offline/discovery/online/disconnected snapshots，验证轮胎/传动比行只在稳定 online 时出现，断开后消失。
- 常规门禁：host self-tests、目标 ESP-IDF build、GitNexus compare/detect changes。

现有最合适的 UI 自动化承载点是 `esp_bms_lvgl_ui_simulator_settings_scroll_smoke()`：它已构造满 6 个候选、验证列表对象不重建、验证返回取消语义（`components/esp_bms_lvgl_ui/ui_simulator.c:425-488`）。生产 simulator hooks 也仍在公共头中：`components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h:448-463`。

但是当前工作树 **没有 `simulator/` 目录**，而 `scripts/run-lvgl-simulator.sh:4-9` 固定从该目录 CMake configure；因此以下 simulator 命令当前会在 configure 前失败，不能作为现状可执行门禁。README 仍声称该目录存在（`README.md:143-155`），规范也把 simulator 定义为 UI 主机首要检查（`.trellis/spec/frontend/component-guidelines.md:103-112`, `:154-159`）。恢复 simulator 是独立的仓库完整性问题；除非主会话确认纳入本任务，不应为本 bug 顺带重建整个 host harness。

### Hardware-only acceptance boundary

下列行为不能由当前 host self-tests或 snapshot 模拟证明，必须真机：

- 真实广告包与 scan response 的名称字段到达顺序、同 MAC 名称补全、RSSI 更新和实际多设备密度。
- 320x240/240x320 TFT 上“更多设备”入口、滚动/点击命中、顶部返回和左边缘返回；TFT 新文字必须为 ASCII。
- BMS 与 Controller 扫描交接没有双 active/requested、没有错误 callback ownership，也没有 07-29 的 NimBLE 初始化 panic/reset 回归。
- FarDriver 的 FFE0 service、FFEC notify、FFEF write/回退能力、CCCD 订阅、持续遥测；完成识别后专属设置持续可见。
- 真实断开、主动关闭和识别失败后 UI 及时隐藏专属设置，不能用保活 UI 掩盖断连。
- legacy ESP32 无 PSRAM 的空闲 heap/最大块；若提高候选上限必须额外测 RAM。

### Suggested validation commands

```bash
# Existing host checks. Does not currently cover BLE scan pagination/name parsing.
./scripts/run-host-selftests.sh

# Firmware compile/link and snapshot ABI assertions.
./scripts/esp-idf-env.sh build

# Only after simulator/ is restored or otherwise supplied.
./scripts/run-lvgl-simulator.sh --headless
./scripts/run-lvgl-simulator.sh --headless --portrait

# Required pre-commit scope review.
node .gitnexus/run.cjs detect_changes --scope compare --base-ref main

# Fixed project hardware flash/monitor.
./scripts/esp-idf-env.sh \
  -p "rfc2217://192.168.2.10:4000?ign_set_control" \
  -b 115200 flash monitor
```

Hardware log checklist: `NimBLE synced`; source-specific scan start/complete/handoff; selected MAC/name; GAP connect status; service/characteristic/CCCD discovery; subscription result; disconnect reason; no `Guru Meditation`, `StoreProhibited`, watchdog, or `rst:0xc`.

### Worktree conflict assessment

No Git command was executed. Read-only GitNexus `detect_changes(scope=all)` reports **18 changed symbols in 12 files, CRITICAL aggregate risk**.

Direct overlap / coordination required:

- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` is already touched (`esp_bms_dashboard_snapshot_t`). Raising candidate count or changing snapshot fields overlaps this ABI and the size assertion at `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui_internal.h:346-347`.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui_internal.h` is already touched (`esp_bms_lvgl_ui_t`). Adding a pagination offset/state touches the same struct.
- `components/esp_bms_lvgl_ui/ui_simulator.c` is already touched. Extending the existing settings smoke is a same-file conflict even though the reported changed symbols are other simulator helpers.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` is already touched (`runtime_clear_bms_telemetry`, `runtime_reset_state`). The proposed online projection is a different function but the same file.
- `components/esp_bms_bms_ble/esp_bms_bms_ble.c` is already touched in telemetry functions. Name-parser work is a different region but same-file coordination is required.

No changed symbol was reported for `ui_settings.c`, `ui_settings_system.c`, or `esp_bms_controller_ble.c`; these are the lowest-conflict implementation regions. GitNexus did not report deletion/addition of `simulator/`, but the directory is absent, so its baseline state cannot be inferred from this research role without a Git operation.

### Candidate-capacity caveat

`ESP_BMS_BMS_SCAN_MAX_CANDIDATES` is part of the public snapshot ABI and runtime arrays, not merely a UI constant. The build asserts the snapshot is exactly 1112 bytes (`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui_internal.h:346-347`), while runtime, display service and UI keep several snapshot copies/queues. Increasing 6 to 12 adds roughly 540 bytes to each snapshot (two candidate arrays) plus runtime candidate/name caches and stack copies. On the no-PSRAM legacy target, this must be treated as a RAM-budget change, not the default pagination fix.

## Files Found

- `.trellis/tasks/08-08-fix-ble-scan-list-controller-connection/prd.md` - current requirements and acceptance criteria.
- `.trellis/tasks/07-12-controller-ble-tire-ratio/{prd,design,implement}.md` - shared BLE selection UI and prior hardware evidence.
- `.trellis/tasks/07-24-fix-controller-ble-legacy-display/{prd,design,implement}.md` - single NimBLE scanner arbitration contract.
- `.trellis/tasks/07-29-fix-ble-scan-init-panic/{prd,design,implement}.md` - legacy BLE-host risk and incomplete hardware scan validation.
- `components/esp_bms_lvgl_ui/ui_settings.c` - shared BLE page, controller settings and navigation.
- `components/esp_bms_lvgl_ui/ui_settings_system.c` - candidate rendering/click mapping.
- `components/esp_bms_lvgl_ui/ui_simulator.c` - existing settings candidate smoke hooks.
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` - candidate/snapshot ABI.
- `components/esp_bms_controller_ble/esp_bms_controller_ble.c` - controller scan, name cache, GATT identification and connection phases.
- `components/esp_bms_bms_ble/esp_bms_bms_ble.c` - BMS advertising/scan-response name parsing.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` - BMS candidates, HTTP projection and controller snapshot truth.
- `scripts/run-host-selftests.sh` - current host checks; no BLE scan-list test.
- `scripts/run-lvgl-simulator.sh` - simulator launcher whose source directory is currently absent.
- `scripts/esp-idf-env.sh` - ESP-IDF 6.0.2 build/flash entry.

## External References

- No web research was needed. Live source and project specs are authoritative for this scope.
- Toolchain version: ESP-IDF 6.0.2, enforced by `scripts/esp-idf-env.sh:8-43`.
- UI simulator contract specifies repository LVGL 9.5.0 and SDL2: `.trellis/spec/frontend/component-guidelines.md:127-137`.
- GitNexus live index was available: `esp32BMSGPS`, 200 files, 4211 symbols, 300 processes. Query/context confirmed `controller_scan_gap_event -> controller_store_candidate -> esp_bms_idf_runtime_project_controller_snapshot -> runtime_project_controller_snapshot`; source was then verified directly.

## Related Specs

- `.trellis/spec/frontend/component-guidelines.md:103-159` - simulator is the required repeatable host check for settings/navigation/snapshot visibility.
- `.trellis/spec/backend/hardware-build-flash.md:49-61` - build and fixed RFC2217 flash/monitor commands.
- `.trellis/spec/backend/hardware-build-flash.md:206-239` - fixed transport and flash acceptance contract.
- `.trellis/spec/backend/quality-guidelines.md:1390-1475` - simulator/production boundary and quality expectations.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - reuse the existing source adapter/render path.
- `.trellis/spec/guides/cross-layer-thinking-guide.md:19-50`, `:105-125` - trace source -> snapshot -> UI and preserve one state authority.

## Caveats / Not Found

- No current log contains a real FarDriver scan/connect/disconnect sequence or the raw advertisement responsible for placeholder naming. The control-state root cause is strongly supported by source ordering, but the device-specific disconnect reason still requires hardware logs.
- Existing host self-tests cover protocols and build helpers, not NimBLE GAP parsing, scan candidate capacity, BLE list pagination, or controller phase projection (`scripts/run-host-selftests.sh:8-88`).
- The simulator production hooks exist, but the root `simulator/` source directory is missing. UI automation recommendations are implementable only after that harness is restored or a narrower supported host target is provided.
- Read-only GitNexus change detection identifies symbol/file overlap but not exact diff hunks. Main/implement agent must inspect and preserve the user's changes before editing overlapping files.
- The current PRD does not state whether the candidate store must exceed 6. Default to paginating the existing six to keep the change small; expanding scan retention requires explicit capacity/RAM acceptance.
