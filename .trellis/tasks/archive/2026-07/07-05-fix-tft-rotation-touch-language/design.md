# Design

## Scope

This task changes the existing TFT and XPT2046 firmware paths only. It does not
add a new font, a new UI framework, or a new display driver.

## Display Rotation

- Default target init uses `board::tft::CONTROLLER` and
  `app_state.settings.display_rotation`.
- RDDID fallback does not trigger auto-probe by default.
- A single `active_display_rotation` value is maintained after initialization
  and used for display drawing, touch mapping, and settings-row hit testing.
- Manual auto-probe remains available behind an explicit bring-up switch so a
  future hardware debugging session can opt into it.

## Touch Detection

- Existing XPT2046 SPI reads stay in place.
- Touch presence is true when IRQ is active or pressure/raw sampling indicates a
  valid touch.
- `read_raw_average()` and `wait_for_release()` use that shared predicate.
- Serial diagnostics are rate-limited and include raw x/y, IRQ state, and
  mapped screen x/y.

## Language Display

- `DeviceSettings::default()` remains Chinese.
- TFT renders language state as ASCII `ZH` or `EN`.
- Persisted settings are not force-migrated from English back to Chinese.

## Compatibility

- Existing web UI language defaults stay unchanged.
- Existing setup AP SSID/password policy is untouched.
- Host tests should remain no-std friendly and avoid target-only peripherals.

## Rollback

The rollback point is the TFT/touch files touched by this task. Reverting those
files should restore the previous display and touch behavior.
