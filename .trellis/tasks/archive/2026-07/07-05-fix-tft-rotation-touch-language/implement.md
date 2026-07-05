# Implementation Plan

## Checklist

- [x] Refresh GitNexus index and run upstream impact on edited symbols.
- [x] Inspect existing display init, display rotation, settings UI, language,
      and XPT2046 touch paths.
- [x] Disable default RDDID-miss auto-probe and keep manual probe opt-in.
- [x] Thread `active_display_rotation` through display drawing, touch mapping,
      and calibration/hit testing.
- [x] Add XPT2046 pressure/raw-sample touch fallback shared by
      `read_raw_average()` and `wait_for_release()`.
- [x] Add rate-limited touch diagnostics.
- [x] Ensure TFT language text remains ASCII `ZH` / `EN` and default language
      remains Chinese.
- [x] Run host and ESP validation commands as far as the local toolchain allows.
- [x] Run GitNexus `detect_changes()` equivalent before completion.

## Validation Commands

```bash
node .gitnexus/run.cjs analyze
cargo fmt --check
cargo test --target x86_64-unknown-linux-gnu --lib
cargo clippy --target x86_64-unknown-linux-gnu --lib -- -D warnings
. "$HOME/export-esp.sh"
cargo +esp check --bin esp32-bms-gps -j1
cargo +esp clippy --bin esp32-bms-gps -j1 -- -D warnings
cargo +esp build --release -j1
espflash save-image --chip esp32 --partition-table partitions.csv --target-app-partition factory target/xtensa-esp32-none-elf/release/esp32-bms-gps image.bin
```

## Risk

Risk is medium until impact analysis is run because the display loop owns the
runtime path for settings, touch, and provisioning visibility.
