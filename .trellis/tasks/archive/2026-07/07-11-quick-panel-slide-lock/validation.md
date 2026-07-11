# Validation

## Passed

- `git diff --check`
- `python3 -m py_compile preview/bms_lvgl_ui_preview.py preview/lvgl_render_compat.py`
- `idf.py build` with ESP-IDF v5.5.4
  - image size: `0x159cd0`
  - smallest app partition free: `0x86330` (28%)
- Desktop LVGL preview at 240x320 and 320x240
  - files: `preview/screen_unlock_portrait.png`, `preview/screen_unlock_landscape.png`
  - no bounds overflow; 56px track and 48px knob remain touch-friendly in both orientations
- Quick-panel lock icon preview at 320x240
  - file: `preview/quick_lock_icon_landscape.png`
  - the LVGL primitive-based shackle/body reads clearly as a padlock in the final 4x2 tile layout
- RFC2217 flash at `rfc2217://100.118.146.11:4000?ign_set_control`, 115200 baud
  - target: ESP32-D0WD-V3 revision 3.1
  - MAC: `20:e7:c8:5f:ab:a4`
  - all flashed segments passed hash verification
- Boot monitor
  - ST7789/LVGL adapter initialized
  - XPT2046 touch registered and calibration loaded
  - no panic, watchdog reset, or reboot loop observed
- GitNexus `detect-changes --scope compare --base-ref refs/heads/main`
  - whole dirty worktree: CRITICAL, 14 files / 148 symbols / 44 flows
  - critical scope is dominated by pre-existing controller/runtime work; lock-screen edited functions were impact-checked individually and HIGH/CRITICAL core functions were called without modifying their bodies

## Physical Touch Check Remaining

- Confirm the quick-panel lock icon is recognizable and comfortable to hit.
- Confirm every locked horizontal/vertical swipe leaves the current carousel page unchanged.
- Confirm tap-to-show, below-threshold reset, 3-second hide, and 85% release-to-unlock on the physical XPT2046 panel.
