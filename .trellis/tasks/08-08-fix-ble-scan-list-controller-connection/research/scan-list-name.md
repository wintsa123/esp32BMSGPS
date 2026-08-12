# Research: BLE scan list advertised names

- Query: Why do some BMS/controller BLE scan rows lose valid ASCII advertised names and render as `设备 N`?
- Scope: mixed (project firmware/LVGL plus the pinned ESP-IDF NimBLE implementation)
- Date: 2026-08-08

## Findings

### Conclusion

The LVGL renderer is not replacing a valid name. It uses `candidate->name` whenever
`candidate->has_name` is true and the string is non-empty; `设备 N` is selected only
when the snapshot already says that the candidate has no name
(`components/esp_bms_lvgl_ui/ui_settings_system.c:57-63`). The fault boundary is
therefore upstream of rendering.

The best-supported acquisition-order root cause is that legacy advertisements and
scan responses are delivered as separate `BLE_GAP_EVENT_DISC` events for the same
address. An advertising packet often has no local-name field while its later scan
response does. If each event is treated as a complete candidate, an unnamed event
can create or overwrite a row before/after the named response. ESP-IDF v6.0.2's
NimBLE source confirms that every accepted report is dispatched independently
(`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c:1598-1617,
:2567-2577`), and the descriptor explicitly includes scan response as an event type
(`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h:505-523`).

The present worktree already contains the minimal protections for that ordering
problem on both paths:

- Both scans are active (`.passive = 0`) and disable duplicate filtering, allowing
  both advertisement and scan-response reports to arrive
  (`components/esp_bms_bms_ble/esp_bms_bms_ble.c:1119-1128`,
  `components/esp_bms_controller_ble/esp_bms_controller_ble.c:701-710`).
- Both callbacks parse every discovery event, including scan responses, and pass a
  missing name as `NULL` rather than manufacturing a label
  (`components/esp_bms_bms_ble/esp_bms_bms_ble.c:982-1005`,
  `components/esp_bms_controller_ble/esp_bms_controller_ble.c:620-640`).
- BMS candidate storage caches a non-empty name by MAC, restores it on a later
  unnamed event, and never clears an existing candidate name
  (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:1017-1047,
  :1050-1094`).
- Controller candidate storage implements the same cache/merge rule
  (`components/esp_bms_controller_ble/esp_bms_controller_ble.c:219-258,
  :556-612`).

These protections look purpose-built for the reported symptom. Because this
research role is forbidden from running Git operations, it could not compare the
worktree with `main`; it is not confirmed whether these protections are the
pre-existing implementation or part of the user's current uncommitted work.

### Remaining character-policy gap

For a populated NimBLE local-name field, both scan paths retain printable bytes in
`0x20..0x7e`, skipping non-ASCII/control bytes so a mixed name can still preserve
its ASCII portion. However, both also explicitly discard double quote and
backslash (`components/esp_bms_bms_ble/esp_bms_bms_ble.c:901-925`,
`components/esp_bms_controller_ble/esp_bms_controller_ble.c:192-216`). Therefore:

- ordinary letters, digits, spaces, `-`, `_`, `#`, `+`, parentheses, and similar
  printable ASCII already survive;
- a name consisting only of `"` and/or `\` becomes empty and therefore renders as
  `设备 N`;
- a mixed name containing these two bytes is displayed with those bytes removed.

This exclusion is not required by LVGL, but it currently protects the BMS HTTP
candidate endpoint, which interpolates names directly into JSON without escaping
(`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:3257-3278`). Consequently,
blindly accepting all printable ASCII in the scanner would make `/api/bms/candidates`
emit invalid JSON for quote/backslash names. If AC3 includes these two characters,
the fix must pair relaxed name filtering with JSON string escaping at this endpoint.
If AC3 means the existing JSON-safe printable subset, the live name-copy functions
already meet it.

The separate `runtime_bms_name_copy()` used when loading a persisted BMS binding
has the same filter (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:932-957,
:3925-3935`). Any character-policy change should keep persisted and freshly scanned
names consistent.

### Parse behavior and malformed advertisements

`ble_hs_adv_parse_fields()` clears the output and parses TLVs in order; when it
reaches a shortened or complete local-name TLV it stores a pointer and length
(`/vol1/1000/toolchains/esp-idf-v6.0.2/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_adv.c:917-927,
:1088-1115`). The callbacks intentionally ignore the parser return code, so a
malformed field *after* the name does not discard the already parsed name. A
malformed field *before* the name still prevents the parser from reaching it and
will leave `has_name=false`. That is a peripheral-payload caveat, not evidence that
the renderer mishandles ASCII.

### Snapshot and rendering flow

The complete data path is:

```text
NimBLE advertisement or scan response
  -> bms_gap_event / controller_scan_gap_event
  -> bms_name_copy / controller_name_copy
  -> per-MAC cache and candidate merge
  -> runtime dashboard snapshot
  -> settings_bms_ble_refresh_rows
  -> original name or `设备 N`
```

BMS projection copies the candidate struct, including `name` and `has_name`,
without another transformation (`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:960-984`).
Controller projection likewise copies the array as-is
(`components/esp_bms_idf_runtime/esp_bms_idf_runtime.c:791-797`). There is no
later sanitizer or fallback conversion between storage and LVGL.

### GitNexus evidence and blast radius

GitNexus query located both scan flows, then `context` confirmed:

- `bms_name_copy` has one caller, `bms_gap_event`.
- `controller_name_copy` has one caller, `controller_scan_gap_event`.
- `esp_bms_idf_runtime_bms_scan_store_candidate` has one caller,
  `bms_gap_event`.
- `controller_store_candidate` has one caller, `controller_scan_gap_event`.

Upstream `impact` results for all four acquisition/merge symbols were **LOW**:
one direct caller, one affected process, one affected module each. In contrast,
`settings_bms_ble_refresh_rows` was **CRITICAL**: 36 affected symbols, 11
execution flows, and 3 modules (`esp_bms_lvgl_ui`, `esp_bms_display_service`,
`esp_bms_lvgl_bridge`). This strongly favors fixing or retaining the low-risk
acquisition/merge behavior rather than changing the shared renderer.

GitNexus line numbers are slightly behind the live source (for example it reports
`bms_name_copy` at line 895 while the live file has it at line 901), so source
citations above use live `nl -ba` positions. Symbol relationships and impact
results remain useful, but exact indexed line positions may reflect a stale index.

### Candidate minimum fix

1. Do not change the LVGL fallback branch. Preserve the current per-MAC
   cache/merge behavior in both BMS and controller scan paths; this is the smallest
   shared-point fix for separate unnamed advertisement and named scan-response
   events.
2. Treat the current live code as potentially already fixed until its diff against
   `main` is reviewed by a role permitted to run Git. Do not re-implement the same
   cache or add another UI-side workaround.
3. Only if the accepted character contract explicitly includes `"` and `\`, relax
   all three name-copy filters to the full printable ASCII range and add proper
   JSON string escaping in `runtime_http_bms_candidates_handler`. Do both in one
   change; relaxing only the scanner is unsafe for the embedded Web API.
4. Do not add a dependency. A short bounded JSON escape loop or an existing local
   JSON writer, if one is found during implementation, is sufficient.

### Runnable validation suggestions

- Add one small host self-test for name normalization and event-order merging:
  `unnamed ADV -> named SCAN_RSP -> unnamed ADV` and the reverse order must both
  end with the exact ASCII name. Cover `ANT-BMS_01`, `JK BMS #2`, empty input,
  UTF-8-only input, mixed UTF-8/ASCII, truncation at 24 bytes, and (if accepted)
  quote/backslash.
- Extend the existing LVGL simulator smoke around
  `components/esp_bms_lvgl_ui/ui_simulator.c:444-474` with one exact ASCII name
  assertion plus one truly unnamed fallback assertion. The current smoke only
  checks that a pre-filled `设备 6` row is present; it does not exercise BLE parsing
  or fallback selection.
- If quote/backslash are accepted, request `/api/bms/candidates` and parse the
  response with a real JSON parser; assert a round trip of both characters.
- Run existing host and UI gates:

```bash
./scripts/run-host-selftests.sh
./scripts/run-lvgl-simulator.sh --headless
./scripts/run-lvgl-simulator.sh --headless --portrait
./scripts/esp-idf-env.sh build
```

- Hardware validation should scan a known device whose name is in the scan
  response rather than the advertising packet, then verify the same MAC retains
  the exact name through repeated reports. If the symptom remains, temporarily
  log `event_type`, parser return, `fields.name_len`, MAC, and raw advertising
  bytes; this distinguishes a missing/malformed name field from a storage/UI bug.
  Complete the project-required RFC2217 flash/monitor validation after firmware
  changes.

### Files found

- `components/esp_bms_bms_ble/esp_bms_bms_ble.c` - BMS discovery callback,
  ASCII filter, active scan configuration, and handoff.
- `components/esp_bms_controller_ble/esp_bms_controller_ble.c` - controller
  discovery callback, ASCII filter, per-MAC name cache, merge, and active scan.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` - BMS candidate cache,
  snapshot projection, persisted-name normalization, and HTTP JSON serialization.
- `components/esp_bms_lvgl_ui/ui_settings_system.c` - final row formatting and
  the only `设备 N` fallback decision.
- `components/esp_bms_lvgl_ui/ui_simulator.c` - existing list smoke coverage.
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` - scan candidate shape,
  name length 24, and current maximum count 6.
- `main/web/index.html` - Web UI consumes optional candidate names safely via
  `textContent`; malformed JSON would fail before rendering.
- `scripts/esp-idf-env.sh` - pins the project toolchain path to ESP-IDF v6.0.2.
- ESP-IDF v6.0.2 NimBLE `ble_gap.c`, `ble_hs_adv.c`, and `ble_gap.h` - external
  implementation evidence for separate discovery reports and AD-name parsing.

### Related specs

- `.trellis/tasks/08-08-fix-ble-scan-list-controller-connection/prd.md` - R2,
  AC3, AC4, and AC6 define the advertised-name contract and required checks.
- `AGENTS.md` - TFT ASCII constraint and mandatory GitNexus impact policy.
- `.trellis/spec/frontend/quality-guidelines.md` - Web candidate contract uses
  optional `name`; no fake candidates.
- `.trellis/spec/frontend/component-guidelines.md` - required landscape and
  portrait LVGL headless validation.
- `.trellis/spec/backend/quality-guidelines.md` - ESP-IDF build and RFC2217
  hardware validation gates.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - preserve the exact name
  contract across scanner, snapshot, HTTP, and UI.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - reuse the current shared
  candidate/snapshot path; avoid renderer-side duplicate validation.

## Caveats / Not Found

- No Git command was run: the research role forbids all Git operations. The
  requested inspection of uncommitted changes therefore remains for the main or
  implementation agent. Existing worktree edits were not reverted or overwritten.
- No dedicated BLE advertised-name or advertisement/scan-response ordering test
  was found under `tests/`.
- The actual affected peripheral payload and name were not supplied. Without a
  raw report or hardware reproduction, malformed-before-name TLVs cannot be ruled
  out.
- Whether `"` and `\` belong to “common symbols” is a product-contract ambiguity.
  Their current rejection is the only confirmed printable-ASCII exception.
- The fixed six-candidate capacity is a separate cause of incomplete lists, but
  it does not convert a valid stored name into `设备 N`; pagination/capacity is
  outside this research topic.
