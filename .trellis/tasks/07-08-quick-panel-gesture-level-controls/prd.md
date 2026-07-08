# Quick Panel Gesture And Level Controls

## Goal

Make the TFT quick panel feel direct and consistent:

- Pulling the quick panel upward to return home should follow the finger instead
  of disappearing only after a threshold.
- Brightness and volume should behave like the other quick buttons at rest:
  icon-only tiles, single tap cycles through four preset levels, long press
  opens a centered slider overlay, dragging changes the value live, and release
  returns the tile to icon-only mode.
- Volume dragging must keep producing audible feedback while the user drags,
  not only once at the end.

## Confirmed Facts

- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1046` currently handles upward
  return in `process_return_swipe_event()`.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1078` closes the quick panel or
  returns home immediately after the upward swipe threshold is crossed; there is
  no drag-position feedback for this return gesture.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1104` already implements
  finger-following downward pull-to-open by moving `quick_panel` based on drag
  distance.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1621` maps slider pointer
  coordinates into brightness or volume percentages.
- `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c:1653` currently treats
  brightness and volume as always-visible slider tiles.
- `main/idf_main.c:164` triggers audio feedback when a changed
  `ESP_BMS_LVGL_ACTION_SET_VOLUME` event reaches the runtime.
- `components/esp_bms_audio_feedback/esp_bms_audio_feedback.c:82` can retrigger
  or extend a short volume feedback tone; this is suitable for repeated drag
  feedback if UI events continue to be queued during dragging.
- Firmware constraints in `.trellis/spec/backend/quality-guidelines.md` require
  LVGL/TFT changes to avoid `transform_scale`, animated opacity, and other
  effects that force off-screen layers on the constrained ESP32 heap.

## Requirements

- R1. Upward return from the quick panel must follow the finger while dragging.
- R2. Releasing before the return threshold must restore the quick panel to its
  open position without navigating home.
- R3. Releasing after the return threshold must complete the return action and
  restore internal gesture state.
- R4. Horizontal right-swipe cancellation must continue to prevent accidental
  return or accidental button action.
- R5. Brightness and volume quick tiles must be icon-only at rest, matching the
  visual density of the other quick buttons.
- R6. Single tap on brightness cycles through four preset brightness levels.
- R7. Single tap on volume cycles through four preset volume levels.
- R8. Long press on brightness or volume opens a centered slider overlay for
  that control.
- R9. The slider overlay must allow live drag adjustment and must disappear on
  release or press loss, returning the tile to icon-only state.
- R10. Volume slider dragging must produce repeated audible feedback while
  dragging.
- R11. Brightness changes must still route through
  `esp_bms_lvgl_bridge_set_brightness()` via the existing runtime action path.
- R12. Volume changes must continue updating `runtime.volume_percent` and the
  dashboard snapshot using the existing `ESP_BMS_LVGL_ACTION_SET_VOLUME` path.
- R13. The implementation must avoid LVGL effects known to allocate off-screen
  draw layers.
- R14. The firmware must build, flash through the RFC2217 bridge, and boot
  without WDT/panic after the interaction changes.
- R15. Brightness tap presets are `25%`, `50%`, `75%`, and `100%`.
- R16. Volume tap presets are `0%`, `35%`, `70%`, and `100%`.
- R17. Live dragging should apply the current value immediately, but persistent
  display-setting saves should happen only for committed interactions such as
  single-tap cycling or slider release.
- R18. The dashboard quick-pull affordance must not show a visible white/gray
  handle, but the invisible pull target should be easier to hit.
- R19. Brightness and volume single taps should show a short ASCII toast with
  the new level.
- R20. Long-press slider overlays must use a vertical track in portrait and a
  horizontal track in landscape.
- R21. Slider dragging should avoid adjacent-value jitter from touch noise.
- R22. Quick-panel upward return should start only from the bottom return area,
  so normal button taps inside the panel do not trigger panel-return drag.
- R23. Quick-panel button taps should perform their function and keep the quick
  panel open unless the action inherently rebuilds the display.
- R24. The hotspot quick icon should use `wlanJZ.c` and glyph U+E62B.
- R25. Dashboard pull-to-open must track the finger by the pointer's absolute
  y position, not only by delta from the press point.
- R26. Quick-panel open/close completion after release should use a short
  position transition instead of snapping instantly.
- R27. Quick controls must not accept button actions while the quick panel is
  partially pulled, returning, or settling.
- R28. Brightness/volume slider overlay dragging must not trigger the quick
  panel return gesture.
- R29. Volume feedback should play for each accepted UI volume event, including
  fast drags where the final release value matches the already-applied runtime
  value.
- R30. Bluetooth, hotspot, and Wi-Fi quick buttons should show an immediate
  active visual state after tap, without waiting for a later runtime snapshot.
- R31. Quick buttons should show a safe pressed transition for the whole tile
  without using LVGL transform-scale or opacity animation.
- R32. Bluetooth, hotspot, and Wi-Fi active state should change only the icon
  glyph color, not the quick tile background color.
- R33. Tapping Bluetooth, hotspot, or Wi-Fi again should toggle the local visual
  active state off instead of repeatedly firing the enable/open action.
- R34. Pressed quick buttons should keep their icon visually centered while the
  tile is inset.
- R35. The root settings page should be a stacked-card scrolling list with only
  these top-level options: Wi-Fi, hotspot, Bluetooth, BMS/protection board,
  system, and about device.
- R36. The TFT settings page should use Chinese labels and a light stacked-card
  UI with a small compiled font subset for the required glyphs.
- R37. The settings page should return with a left-to-right swipe: detail pages
  return to the settings root, and the settings root returns to the dashboard.
  It should not use a visible BACK button or upward-swipe return.
- R38. Each settings category should have a detail page with status rows and
  basic function rows for Wi-Fi reprovisioning, hotspot/config entry, Bluetooth
  or BMS binding scan, display rotation, language toggle, restore defaults, and
  about-device information.
- R39. Volume feedback should be carried as an explicit UI action-event feedback
  field so fast drag feedback still plays even when the runtime volume value did
  not change on the final event.

## Acceptance Criteria

- [ ] AC1. With the quick panel open, dragging upward moves the panel with the
      finger instead of instantly closing at the threshold.
- [ ] AC2. A short upward drag below threshold leaves the quick panel open.
- [ ] AC3. A long upward drag above threshold returns to the dashboard/home
      state and does not leave the panel at an intermediate y position.
- [ ] AC4. Right-swipe cancellation in the quick panel still suppresses return
      and accidental button action.
- [ ] AC5. Brightness and volume tiles show icons only when idle.
- [ ] AC6. Tapping brightness cycles exactly four preset levels and applies the
      new brightness.
- [ ] AC7. Tapping volume cycles exactly four preset levels, applies the new
      volume, and emits feedback for the selected level.
- [ ] AC8. Long-pressing brightness opens a centered brightness slider overlay;
      dragging updates brightness live; release hides the overlay.
- [ ] AC9. Long-pressing volume opens a centered volume slider overlay; dragging
      updates volume live and repeatedly emits audible feedback; release hides
      the overlay.
- [ ] AC10. `git diff --check`, `./scripts/esp-idf-env.sh build`, GitNexus
      change detection, RFC2217 flash, and boot monitor all complete.
- [ ] AC11. Dashboard shows no visible quick-pull handle, while a larger
      invisible top area still opens the quick panel.
- [ ] AC12. Brightness/volume tap shows a short toast with the resulting level.
- [ ] AC13. Brightness/volume long-press slider is vertical in portrait and
      horizontal in landscape, with no visible adjacent-value jitter.
- [ ] AC14. Quick-panel button taps do not accidentally start upward return and
      do not close the quick panel for normal toggle/action buttons.
- [ ] AC15. Hotspot uses the `wlanJZ.c` U+E62B icon.
- [ ] AC16. Pulling the dashboard down halfway shows about half of the quick
      panel; releasing beyond threshold finishes opening with a short y-position
      transition.
- [ ] AC17. While the quick panel is not fully open and settled, touching drift
      over buttons does not fire quick actions.
- [ ] AC18. Dragging brightness/volume overlay back and forth never returns to
      the dashboard/home state.
- [ ] AC19. Fast volume drags still produce feedback on the final release event.
- [ ] AC20. Bluetooth/hotspot/Wi-Fi buttons visibly change active state
      immediately after tapping.
- [ ] AC21. Quick buttons visibly press inward and restore on release without
      using transform-scale or opacity animation.
- [ ] AC22. Bluetooth/hotspot/Wi-Fi active state changes only recolor the glyph;
      tile backgrounds remain unchanged.
- [ ] AC23. A second tap on Bluetooth/hotspot/Wi-Fi clears the local active
      visual state and does not re-run the open/enable action.
- [ ] AC24. The pressed inset state keeps each quick icon centered inside the
      shrunken tile.
- [ ] AC25. Settings preview and firmware show a scrollable stacked-card root
      settings page with only Wi-Fi, hotspot, Bluetooth, BMS, system, and about
      options.
- [ ] AC26. Settings root and detail pages are Chinese, have no BACK button,
      and use left-to-right swipe for return.
- [ ] AC27. Settings detail pages exist for Wi-Fi, hotspot, Bluetooth,
      protection board, system, and about device, with the expected basic
      action/status rows.
- [ ] AC28. Volume drag events request audio feedback through
      `volume_feedback_valid`/`volume_feedback_percent`, independent of whether
      the runtime state changed.

## Out Of Scope

- Replacing the current GPIO26/GPIO4 audio feedback hardware path.
- Adding a full CJK font; only the explicit settings-page glyph subset is in
  scope.
- Adding LVGL opacity/transform animations or bitmap assets for this interaction.
- Changing Web UI configuration behavior.

## Open Questions

- None.
