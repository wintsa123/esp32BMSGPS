# Fix TFT rotation touch and language display

## Goal

Fix the post-flash TFT hardware regressions with the smallest firmware change:
the device should boot into a stable landscape TFT UI, touchscreen taps should
work even when the XPT2046 IRQ line is unreliable, and the TFT settings page
should show the language state with ASCII labels.

## Background

- The current hardware path is a 320x240 TFT with factory touch calibration
  aligned to landscape orientation.
- RDDID may be unavailable on this board. An unavailable ID must not force a
  portrait-first or rotation-probing boot path by default.
- TFT only has ASCII bitmap fonts today. Web UI remains Chinese by default, but
  TFT language state must be shown as `ZH` or `EN`.
- Persisted user settings remain authoritative. If an existing device is saved
  as English, do not overwrite it on boot; users can change it from the TFT
  language row once touch works.

## Requirements

- Use the board TFT fallback controller and the persisted display rotation as
  the default runtime display orientation.
- Keep the default bring-up path stable in landscape for this hardware unless a
  dedicated manual auto-probe switch is enabled.
- Keep one active display rotation source and use it for display dimensions,
  touch mapping, and calibration.
- Touch reads must not depend only on `TOUCH_IRQ` low level. Raw pressure/sample
  detection must allow taps when IRQ is floating, disconnected, or otherwise
  unreliable.
- `read_raw_average()` and `wait_for_release()` must share the same effective
  touched predicate: IRQ active or pressure/raw sample valid.
- Touch diagnostics must print rate-limited serial lines with raw coordinates,
  IRQ state, and mapped screen coordinates.
- TFT settings language display must use `ZH` / `EN`, with
  `DeviceSettings::default().language == Chinese` preserved.
- Do not add a Chinese TFT font in this task.

## Acceptance Criteria

- [ ] After flash, the TFT no longer automatically flips between portrait and
      landscape; default boot is stable landscape.
- [ ] Touching the bottom settings area enters settings; touching the top back
      area returns.
- [ ] Touching `ROT` changes orientation and keeps touch hit testing aligned
      with the new display size.
- [ ] Touching `LANG` toggles between `ZH` and `EN`.
- [ ] Serial output includes useful touch raw/mapped diagnostics without
      logging secrets.
- [ ] Host formatting/tests/clippy pass where supported.
- [ ] ESP check/clippy/build pass where the local ESP toolchain supports them.

## Notes

- Hardware acceptance still requires physical flash/tap validation by a person
  with the board.
