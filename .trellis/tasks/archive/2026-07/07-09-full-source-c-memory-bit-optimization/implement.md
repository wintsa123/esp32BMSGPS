# Implementation Plan

## Pre-Execution Gate

- [x] User answers Q1 in `prd.md` or explicitly accepts the recommended flag strategy.
- [x] Task is already `in_progress`.
- [x] Load `trellis-before-dev` again in Phase 2 before editing.

## Ordered Work

1. Inventory and baseline
   - [x] Record file review table for every C/H file under `main/` and `components/`.
   - [x] Record current build section sizes and largest project symbols.
   - [x] Keep the existing runtime micro-optimization diff as batch 0 unless it conflicts with later changes.

2. Internal UI state flags
   - [x] Run GitNexus impact on `esp_bms_lvgl_ui_t` consumers or affected functions before edits.
   - [x] Convert repeated UI bool arrays / UI state booleans to mask-backed flags where it reduces `s_ui`.
   - [x] Consider `_Static_assert` for internal state size; skipped for private `s_ui` because LVGL pointer/config drift would make it noisy, while public ABI sizes are locked.
   - [x] Build.

3. Runtime private flags
   - [x] Run GitNexus impact on `esp_bms_idf_runtime_t` consumers and affected runtime functions.
   - [x] Convert runtime readiness/pending/connection booleans to mask-backed flags where it reduces runtime instance size and keeps call sites readable.
   - [x] Preserve NVS, Wi-Fi, BLE, HTTP, and setup AP behavior.
   - [x] Build.

4. Public snapshot and action event flags
   - [x] Apply the user-approved strategy from Q1.
   - [x] Update runtime writers, UI readers, JSON writers, and `_Static_assert(sizeof(esp_bms_dashboard_snapshot_t) == ...)`.
   - [x] Build.

5. Remaining source pass
   - [x] Review bridge config/globals and audio flags.
   - [x] Review hot loops in UI drawing/layout and runtime parsing for safe pointer traversal or bit operations.
   - [x] Explicitly mark generated font files unchanged unless a safe generated-table optimization exists.
   - [x] Build.

6. Final checks
   - [x] `git diff --check`.
   - [x] `./scripts/esp-idf-env.sh build`.
   - [x] `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all`.
   - [x] RFC2217 flash once at 115200 baud.
   - [x] Summarize changed/unchanged files and memory/layout impact.

## Risk Notes

- Changing public snapshot layout is broad because runtime and UI rely on structure assignment and `memcpy`.
- C bit-field layout is compiler-defined; explicit masks are safer for cross-component structs.
- `packed` can create unaligned accesses and should not be applied to general runtime structs.
- `restrict` can silently introduce undefined behavior if aliasing assumptions are wrong.
