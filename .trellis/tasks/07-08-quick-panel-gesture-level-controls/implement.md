# Quick Panel Gesture And Level Controls Implementation Plan

## Preconditions

- Task remains in planning until the user reviews these artifacts and approves
  implementation.
- Inline Codex mode is active, so implementation will be done by the main
  session; JSONL manifest curation is skipped.

## Implementation Checklist

1. Load development context before editing.
   - Read `trellis-before-dev`.
   - Read relevant spec indexes and `.trellis/spec/backend/quality-guidelines.md`.
   - Run GitNexus impact before editing each existing function or struct.

2. Add action-event commit semantics.
   - Extend `esp_bms_lvgl_action_event_t` with a boolean commit/persist field.
   - Clear the field in `esp_bms_lvgl_ui_take_action_event()`.
   - Make `main/idf_main.c` save display settings for committed display-setting
     actions, including release commits where the runtime value did not change
     during that loop.

3. Implement upward quick-panel return follow.
   - Add return drag distance state.
   - Move `quick_panel` with the active pointer during upward drag while open.
   - On below-threshold release, restore `quick_panel` y to `0`.
   - On above-threshold release, complete return and reset all gesture state.
   - Preserve right-swipe cancellation behavior.

4. Replace inline brightness/volume sliders with icon-only tiles.
   - Keep the tile positions in the quick-panel layout.
   - Use icon-only idle rendering.
   - Remove always-visible percentage label and inline track from the idle tile.
   - Keep visual state changes border/color based; do not use transform or
     opacity animations.

5. Add tap preset cycling.
   - Brightness presets: `25`, `50`, `75`, `100`.
   - Volume presets: `0`, `35`, `70`, `100`.
   - Single tap queues a committed action event.
   - Volume tap should trigger audio feedback when the runtime accepts the
     resulting value.

6. Add long-press slider overlay.
   - Add hidden overlay objects during screen creation.
   - Show overlay on brightness/volume long press.
   - Update overlay fill/knob from active pointer during drag.
   - Queue live non-committed events during drag.
   - Queue a committed final event and hide overlay on release/press lost.
   - Suppress the post-long-press click action.

7. Validate on host.
   - `git diff --check`
   - `./scripts/esp-idf-env.sh build`
   - `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`

8. Validate on hardware.
   - Flash through `rfc2217://192.168.2.10:4000?ign_set_control` at `115200`.
   - Monitor boot logs.
   - Confirm no startup panic/WDT.
   - Ask the user to test physical gesture feel and audio loudness if remote
     observation cannot verify touch/audio behavior directly.

## Risky Files / Rollback Points

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
  - highest interaction risk; contains gesture state and quick-panel objects.
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h`
  - action event contract changes affect runtime/main consumers.
- `main/idf_main.c`
  - save policy and audio feedback trigger points.

Rollback by reverting these files to the task-start diff if build or hardware
validation reveals regressions.

## Review Gate

Before `task.py start`, the user must approve:

- The four preset levels.
- The static overlay approach with no animation.
- The committed-save behavior: live drag applies immediately; NVS save happens
  on tap cycle or slider release.
