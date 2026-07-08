# Quick Panel Gesture And Level Controls Design

## Architecture

This task stays in the existing ESP-IDF/LVGL component boundaries:

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` owns quick-panel objects,
  touch gesture state, overlay state, and queued LVGL action events.
- `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c` remains the owner of
  runtime display-setting state and snapshot fields.
- `main/idf_main.c` remains orchestration: it applies bridge brightness changes,
  saves committed display settings, and triggers audio feedback for accepted
  volume updates.
- `components/esp_bms_audio_feedback/` remains the GPIO26/GPIO4 audio feedback
  implementation.

No new external assets, non-ASCII TFT labels, or opacity/transform animations
are required.

## Gesture Design

### Downward Open

The existing downward pull-to-open behavior already follows the finger by moving
`quick_panel` from `-height` toward `0`. Keep this behavior.

### Upward Return From Quick Panel

Add a return drag distance state for the quick panel. When the quick panel is
open and an upward return gesture starts:

1. Keep `quick_panel` visible.
2. During `LV_EVENT_PRESSING`, clamp upward `dy` and set `quick_panel` y to a
   negative offset so the panel follows the finger.
3. If the user releases before the return threshold, reset `quick_panel` y to
   `0` and keep the panel open.
4. If the user releases after the threshold, call the same logical return path
   that currently closes the panel / returns home, then reset internal drag
   state.

Right-swipe cancellation remains authoritative. Once a right swipe is detected,
the gesture must stop tracking return and suppress later click actions from the
same press.

## Brightness And Volume Tile Design

### Idle State

Brightness and volume become icon-only quick tiles at rest. They should visually
match the other quick buttons:

- brightness icon: use an enabled LVGL built-in symbol where available,
  currently `LV_SYMBOL_EYE_OPEN` is already used in the codebase.
- volume icon: use `LV_SYMBOL_VOLUME_MID`.
- no percentage label or inline track in idle state.

### Single Tap

Single tap cycles through fixed four-step presets:

- brightness: `25`, `50`, `75`, `100`
- volume: `0`, `35`, `70`, `100`

The next preset is the first value greater than the current value; if the
current value is at or above the last preset, wrap to the first preset.

Single-tap events are committed interactions. They should update the runtime and
allow display settings to be saved.

### Long Press Overlay

Long pressing brightness or volume opens a full-screen overlay for that control.
The overlay is not an animated layer. It is a normal LVGL object with a static
background and a centered slider track:

- The original tile remains the event owner for that press.
- The overlay is shown on `LV_EVENT_LONG_PRESSED`.
- The overlay's centered slider is updated from the active pointer location
  during subsequent `LV_EVENT_PRESSING` events.
- On `LV_EVENT_RELEASED` or `LV_EVENT_PRESS_LOST`, hide the overlay and return
  the tile to icon-only state.
- A long-press interaction must not also run the single-tap cycle on release.

The same pointer-to-percent calculation can be reused conceptually from
`quick_level_set_from_pointer()`, but should target the overlay track rather
than the tile's old inline track.

### Live Update And Commit

Dragging the overlay should apply values live so users see brightness changes
and hear volume feedback while dragging. Live drag updates should not cause NVS
saves on every move. Add a commit flag to the LVGL action event contract:

- live drag update: `committed = false`
- tap cycle and slider release: `committed = true`

`main/idf_main.c` should save display settings only for committed display-setting
actions. If a release commits the same value that was already applied during a
live drag, saving the current runtime state is still valid.

## Data Flow

Brightness:

`quick tile tap/overlay drag -> esp_bms_lvgl_action_event_t ->
esp_bms_idf_runtime_apply_action_event() -> main loop ->
esp_bms_lvgl_bridge_set_brightness() -> optional committed NVS save`

Volume:

`quick tile tap/overlay drag -> esp_bms_lvgl_action_event_t ->
esp_bms_idf_runtime_apply_action_event() -> main loop ->
esp_bms_audio_feedback_play_volume() on accepted value -> optional committed
NVS save`

## Compatibility

- Preserve existing Web UI config behavior.
- Preserve `runtime.volume_percent` and `runtime.brightness_percent` as
  percentages from `0..100`.
- Keep existing rotation and quick-panel layout persistence behavior unless it
  conflicts with the new icon-only tile design.
- Keep the firmware bootable even if audio feedback initialization fails.

## Risks

- Gesture state regressions can leave `quick_panel` at a negative y position.
  Every release/lost path must reset y consistently.
- LVGL long-press can still emit a click on release. The implementation must
  explicitly suppress the click after opening the overlay.
- Frequent live drag events can overwrite the single pending event. This is
  acceptable for UI feedback, but release must queue a committed event so the
  final setting can be saved.
- The workspace already contains unrelated dirty changes; validation reports
  must separate whole-worktree risk from this task's focused changes.

## Rollback

Revert the task-scoped changes in:

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c`
- `components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h` if the action event
  contract changes there
- `main/idf_main.c` if committed-save behavior changes there

The audio component itself should not need rollback unless this task changes its
public API.
