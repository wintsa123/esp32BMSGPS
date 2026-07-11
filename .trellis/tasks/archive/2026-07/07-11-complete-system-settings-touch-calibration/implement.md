# Implementation Plan

1. Load frontend/backend specs and C style guidance before editing.
2. Run GitNexus upstream impact for every symbol to be modified; stop and warn before HIGH or CRITICAL changes.
3. Fix shared settings row text sizing/long-mode behavior and verify root/detail rows in both orientations.
4. Add System subview routing for brightness, volume, level position, and touch calibration while preserving existing back/swipe navigation.
5. Implement brightness and volume pages with native LVGL sliders and reuse the existing queued value action path.
6. Implement the three-choice level-position page using existing UI-local state and layout refresh.
7. Add restore-default confirmation and clear calibration only after confirmation.
8. Add appended calibration action kinds and the minimal sample payload to the UI contract.
9. Add bridge calibration session APIs, canonical inverse-rotation conversion, validation, linear correction, and versioned NVS load/save/reset.
10. Connect calibration start/sample/cancel/result handling in `main/idf_main.c` without moving touch work out of the existing LVGL lock.
11. Add the four-point calibration wizard, invalid/success result states, cancellation, and navigation cleanup.
12. Update or add preview rendering under `preview/`; generate portrait and landscape PNGs and inspect them visually.
13. Run focused checks: formatting, `git diff --check`, relevant host checks, and ESP-IDF build.
14. Run `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS` and verify only expected symbols/flows changed.
15. Use `esp32-lan-rfc2217-flash` to flash and monitor hardware; verify calibration, reboot persistence, restore-default reset, rotations, and System page interactions.

## Risk And Rollback Points

- `touch_read_with_diagnostics`: shared touch path; incorrect correction can affect every TFT interaction. Keep factory fallback and session cancel restoration.
- `esp_bms_lvgl_action_event_t`: shared UI/runtime contract; append actions and update ABI assertions consistently.
- `settings_show_detail` and back navigation: shared settings flow; retain existing BMS/hotspot special pages.
- `app_main`: boot/action coordinator; keep changes limited to calibration dispatch and reset handling.

## Validation Commands

```bash
git diff --check
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect-changes -r esp32BMSGPS
```

Hardware validation follows `.agents/skills/esp32-lan-rfc2217-flash/SKILL.md`.
