# Full Source C Memory And Bit Optimization

## Goal

Optimize the ESP-IDF C firmware source tree for memory layout, bit operations, and low-level data traversal without reducing firmware correctness, debuggability, or hardware compatibility.

The user explicitly wants a full-source pass rather than the earlier local optimization only. Bit operations and bit-field-style compaction are a priority.

## Confirmed Facts

- Product runtime code is C on ESP-IDF 5.5.4 targeting ESP32-D0WD-V3 with 4 MB flash.
- Source scope is the product C/H files under `main/` and `components/`: 13 C/H files, including generated LVGL font assets.
- Generated font files (`bluetoothon.c`, `hotspoton.c`, `settings_zh_16.c`) are large const tables; they are in scope for review but should not receive hand-written structural rewrites unless a clear generator-safe optimization exists.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:109` locks `sizeof(esp_bms_dashboard_snapshot_t) == 644`, so public snapshot layout changes are intentional ABI changes and must update all C consumers.
- The binary currently has `.dram0.bss` about 32264 bytes and `.dram0.data` about 21900 bytes.
- Largest project-owned mutable objects from the built ELF include `s_ui` at about 0x88c bytes and the runtime instance at about 0x75c bytes.
- Current uncommitted source optimization already applied to `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`: BMS fault-mask bit scanning, pointer traversal in CRC/NMEA/Base64/GPS paths, and overflow-safe little-endian reads.
- User approved the recommended flag strategy: use explicit integer mask-backed flags for public/cross-component structs, and reserve literal C bit-fields for private structs where size benefit and layout risk are both controlled.

## Requirements

- R1. Perform a full-source C optimization review across `main/` and `components/`, including runtime, UI, bridge, audio, generated assets, and public headers.
- R2. For every edited function, class-like struct, or public symbol, run GitNexus `impact --direction upstream` first and record any HIGH/CRITICAL blast radius before editing.
- R3. Prioritize optimizations that reduce RAM use, stack use, flash size, CPU cycles in hot paths, or repeated memory traversal.
- R4. Evaluate all `bool` groups and flag arrays for bit-mask or bit-field-style storage, including `esp_bms_idf_runtime_t`, `esp_bms_dashboard_snapshot_t`, `esp_bms_lvgl_action_event_t`, `esp_bms_lvgl_ui_t`, bridge globals, and small candidate structs.
- R5. Use explicit integer flag masks plus accessor helpers for cross-component/public structs when C bit-fields would make layout, enum width, or ABI behavior harder to control.
- R6. Use C bit-fields only for private/internal structs where the layout win is real, access patterns remain simple, and `_Static_assert` can lock the resulting size.
- R7. Avoid `__attribute__((packed))` on normal runtime structs unless the struct maps a wire protocol or storage format and every access is alignment-safe on ESP32.
- R8. Avoid `restrict`, atomics, memory barriers, and lock-free patterns unless the aliasing or concurrency contract is provable from code.
- R9. Keep generated font assets behaviorally unchanged unless the optimization is mechanical, safe, and build-verified.
- R10. Preserve current product constraints from `AGENTS.md`, including Setup AP SSID/password behavior, Chinese-first web UI language policy, and TFT ASCII language markers.
- R11. Do not force bit compaction into tiny structs or booleans where the measured size win is negligible or the readability/regression cost dominates.

## Acceptance Criteria

- [ ] AC1. Every product C/H file under `main/` and `components/` is reviewed and categorized as changed, intentionally unchanged, or generated/no-op with rationale.
- [ ] AC2. Bit flag strategy is implemented where it has a favorable risk/reward ratio, especially in runtime/UI internal state.
- [ ] AC3. Public ABI-impacting changes are accompanied by updated `_Static_assert` checks and all affected consumers are updated.
- [ ] AC4. `./scripts/esp-idf-env.sh build` passes.
- [ ] AC5. `git diff --check` passes.
- [ ] AC6. `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope all` is run and its affected scope is explained, including pre-existing unrelated edits.
- [ ] AC7. One RFC2217 flash attempt is run through `rfc2217://192.168.2.10:4000?ign_set_control` at 115200 baud and the result is reported.
- [ ] AC8. Final summary lists concrete memory/layout/bit-operation changes and any full-source areas deliberately left unchanged.

## Out Of Scope

- Rewriting generated LVGL font tables by hand.
- Introducing a custom allocator or memory pool unless evidence shows repeated dynamic allocation in product runtime paths.
- Adding lock-free concurrency or memory barriers without a concrete shared-state race or measured mutex bottleneck.
- Changing UI behavior, BLE protocol semantics, Wi-Fi provisioning policy, NVS key names, partition layout, or hardware pin assignments for optimization alone.
