# Full Source C Memory And Bit Optimization Review

## File Review Table

| File | Status | Rationale |
|---|---|---|
| `main/idf_main.c` | changed | Updated runtime/action event bool reads to explicit flag accessors. |
| `components/esp_bms_audio_feedback/esp_bms_audio_feedback.c` | changed | Collapsed private audio ready/active booleans into one byte flag mask. |
| `components/esp_bms_audio_feedback/include/esp_bms_audio_feedback.h` | unchanged | Public audio API has no storage/layout target. |
| `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` | changed | Runtime private flags, snapshot/action mask writers, hot byte traversal, overflow-safe little-endian reads, and BMS fault bit scanning. |
| `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h` | changed | Collapsed runtime bool group into `uint64_t flags` with inline accessors. |
| `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c` | unchanged | Static bridge booleans are seven one-byte globals; mask conversion would save only a few bytes and add accessor/code churn. |
| `components/esp_bms_lvgl_bridge/include/esp_bms_lvgl_bridge.h` | unchanged | Public config booleans are readable, low-count setup fields; compaction would be public API churn with negligible RAM win. |
| `components/esp_bms_lvgl_contract/include/esp_bms_lvgl_contract.h` | unchanged | Version/config assertions only; no mutable layout target. |
| `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` | changed | Collapsed UI state bool group, quick panel item bool arrays, draw-buffer init flags, and snapshot/action reads to masks. |
| `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` | changed | Converted public dashboard snapshot and action event booleans to explicit integer masks plus inline helpers. |
| `components/esp_bms_lvgl_ui/bluetoothon.c` | generated/no-op | Generated const font/image table; no safe hand rewrite. |
| `components/esp_bms_lvgl_ui/hotspoton.c` | generated/no-op | Generated const font/image table; no safe hand rewrite. |
| `components/esp_bms_lvgl_ui/settings_zh_16.c` | generated/no-op | Generated LVGL font table; no safe hand rewrite. |

## Impact Notes

- `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all --limit 80`: 9 changed files, 235 changed symbols, 88 affected flows, overall risk `critical` due broad task scope.
- `apply_dashboard_snapshot`: HIGH impact, 2 direct callers, 4 affected processes, 2 modules. No further expansion after existing snapshot mask conversion.
- `esp_bms_lvgl_ui_take_action_event`, `esp_bms_idf_runtime_apply_action_event`, `esp_bms_idf_runtime_tick`, audio feedback, and bridge public entry points are LOW when disambiguated to implementation symbols.
- Struct-only GitNexus impact for `esp_bms_dashboard_snapshot_t`, `esp_bms_lvgl_action_event_t`, and `esp_bms_idf_runtime_t` returned LOW/0 direct callers, so actual risk was assessed through consumers and detect-changes.

## Size Evidence

- Current `build/esp32_bms_gps_idf.elf`: `.dram0.bss` 32080 bytes, `.dram0.data` 21900 bytes.
- PRD baseline recorded `.dram0.bss` about 32264 bytes and `.dram0.data` about 21900 bytes.
- `s_ui`: now `0x810` bytes.
- Runtime instance symbol `runtime$0`: now `0x720` bytes.
- Public `esp_bms_dashboard_snapshot_t`: static assert updated from 644 to 604 bytes.
- Public `esp_bms_lvgl_action_event_t`: static assert added at 108 bytes.

## Checks So Far

- `git diff --check`: pass.
- `./scripts/esp-idf-env.sh build`: pass.
- `./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash`: pass; ESP32-D0WD-V3 MAC `20:e7:c8:5f:ab:a4`, data hashes verified, hard reset completed.
- Old bool field residual search across `main/` and `components/`: no matches.
