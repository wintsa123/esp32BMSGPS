# Fix TFT Chinese UI and touch taps

## Goal

Fix the remaining hardware acceptance failures after the TFT rotation fix:
touch taps must work again, and the local TFT UI must display Chinese text
instead of English labels.

## Background

- User confirmed the TFT direction is now correct.
- User confirmed taps still do not react.
- User clarified that the previous touch calibration screen was visually
  normal, so the fix should not destabilize the display orientation path.
- Current TFT settings/dashboard labels are hard-coded English in
  `src/display.rs`; this is not merely a persisted `Language::English` setting.
- Current TFT only has ASCII drawing code, so Chinese requires a minimal bitmap
  glyph path for the characters used on screen.
- The previous touch change modified the shared `Xpt2046::read_raw_average()`
  path, which affects both calibration and runtime taps.

## Requirements

- Preserve the currently working default display direction.
- Restore reliable touch tap handling for the settings screen and dashboard
  bottom-tap entry point.
- Prefer the previously working IRQ-gated XPT2046 read behavior as the runtime
  main path; keep diagnostics that can prove whether raw samples are present.
- Do not force a boot-time calibration flow unless explicitly enabled by the
  existing board flag.
- Add rate-limited serial touch diagnostics that include IRQ state and raw x/y
  values even when no tap action is produced.
- Render the TFT settings UI in Chinese by default using a minimal built-in
  bitmap font for only the needed Chinese characters.
- Keep ASCII fallback available for version strings, SSID/passwords, numeric
  values, and any unimplemented glyphs.
- Do not add heap-heavy font rendering, external font files on device, or a
  general full CJK font.

## Acceptance Criteria

- [ ] TFT still boots in the currently correct landscape direction.
- [ ] Tapping the bottom dashboard/settings area produces a visible settings
      transition or serial touch diagnostic.
- [ ] Tapping the top settings area returns to the dashboard.
- [ ] Tapping the language row toggles state once touch works.
- [ ] TFT static labels for the settings screen are Chinese, not English-only.
- [ ] Build/test/check commands pass on host and ESP toolchains.
- [ ] `espflash save-image` still fits the factory app partition.

## Notes

- Hardware final acceptance still requires flashing and tapping the real board.
