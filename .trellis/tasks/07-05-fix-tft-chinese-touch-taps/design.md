# Design

## Scope

Touch and TFT text only. The default display direction fixed by the previous
task stays unchanged.

## Touch

- Revert the runtime touch decision to the known-simple IRQ path first.
- Add a diagnostic sample function that can read raw x/y and pressure values
  without deciding a tap. This gives serial evidence when the UI does not move.
- Keep runtime tap handling based on `read_raw_average()` so callers do not
  duplicate touch semantics.
- Keep release waiting bounded to the same effective runtime touched predicate.

## Chinese TFT Text

- Add a tiny built-in 16x16 monochrome glyph table for the Chinese characters
  used by the settings/dashboard static labels.
- Add `draw_text_zh()` alongside the existing ASCII `draw_text()`.
- Use Chinese labels for static UI strings; keep ASCII for dynamic fields:
  firmware version, SSID/password, `ZH`/`EN`, units, and unknown glyph fallback.

## Compatibility

- Host library code remains unchanged unless needed for testable helpers.
- No new runtime dependencies.
- No full CJK font.

## Rollback

Revert the changed display/touch files and this task if the board needs to go
back to the prior ASCII-only firmware.
