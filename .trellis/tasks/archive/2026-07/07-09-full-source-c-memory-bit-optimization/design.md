# Design

## Scope And Boundaries

The optimization pass covers product C/H files under `main/` and `components/`.

The main mutable-memory targets are:

- Runtime state: `esp_bms_idf_runtime_t` in `components/esp_bms_idf_runtime/include/esp_bms_idf_runtime.h`.
- Public UI snapshot and action event contracts: `esp_bms_dashboard_snapshot_t`, `esp_bms_lvgl_action_event_t`, and `esp_bms_wifi_scan_candidate_t` in `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`.
- Internal UI state: `esp_bms_lvgl_ui_t` in `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`.
- Bridge config and globals: `esp_bms_lvgl_bridge_config_t` and `s_touch_*` / readiness flags.
- Hot byte-processing paths in runtime and UI drawing helpers.

Generated font assets are reviewed for compiler/linker placement only. They are not hand-edited unless the change is obviously mechanical and build-stable.

## Flag Representation Strategy

Use three tiers:

- Tier 1: private/internal state with many independent booleans. Convert to `uint32_t` or `uint64_t` flag masks with named bit constants and small inline/static helpers.
- Tier 2: public cross-component structs. Use explicit mask fields plus access/update helpers over C bit-fields.
- Tier 3: tiny local structs or configuration structs. Keep `bool` if the size win is negligible or readability/regression risk dominates.

Literal C bit-fields are only appropriate when:

- The struct is private to one C file or internal component boundary.
- The field order and compiler are controlled enough for this firmware.
- The struct is not persisted, transmitted, compared bytewise, or exposed to external ABI.
- `_Static_assert(sizeof(type) == expected)` locks the result.

## Data Flow And Contracts

The runtime owns current device state and projects it into `esp_bms_dashboard_snapshot_t`. The UI copies snapshots with `memcpy` into `s_ui.last_snapshot` and `s_ui.deferred_snapshot`. Any snapshot layout change therefore has to update runtime writers, UI readers, JSON writers, and static size checks together.

Action events flow from UI to `main/app_main` to runtime. Any action event flag compaction has to preserve `esp_bms_lvgl_ui_take_action_event()` and `esp_bms_idf_runtime_apply_action_event()` semantics.

Bridge config is constructed by macro and read during initialization. Config bools can be compacted only if all macro designated initializers and function reads are updated.

## Compatibility Notes

- ESP32 unaligned access and packed structs are not assumed safe for general runtime data.
- Existing `memcpy` of snapshots must remain correct after any representation change.
- Existing NVS values are scalar keys and should not require migration for in-memory flag compaction.
- Build uses optimization level O3; some pointer-loop micro-optimizations may not improve generated code and should be kept only if they remain clearer or remove overflow risk.

## Rollback Strategy

Work in small batches:

1. Internal UI state flags.
2. Runtime private flags.
3. Public snapshot/action masks if approved.
4. Remaining hot loops and small structs.

After each batch, build. If a batch introduces wide risk or unclear behavior, revert only that batch and keep earlier verified changes.
