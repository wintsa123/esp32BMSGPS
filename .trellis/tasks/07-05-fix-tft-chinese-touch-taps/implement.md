# Implementation Plan

## Checklist

- [x] Run GitNexus impact for the display and touch symbols that will change.
- [x] Inspect prior touch behavior and current runtime tap path.
- [x] Restore IRQ-first runtime touch reads and add raw diagnostic sampling.
- [x] Add minimal Chinese glyph table and Chinese draw helper.
- [x] Switch static TFT settings/dashboard labels to Chinese.
- [x] Verify host and ESP builds.
- [x] Run GitNexus `detect_changes` before commit.

## Validation Commands

```bash
cargo fmt --check
cargo test --target x86_64-unknown-linux-gnu --lib
cargo clippy --target x86_64-unknown-linux-gnu --lib -- -D warnings
. "$HOME/export-esp.sh"
cargo +esp check --bin esp32-bms-gps -j1
cargo +esp clippy --bin esp32-bms-gps -j1 -- -D warnings
cargo +esp build --release -j1
espflash save-image --chip esp32 --partition-table partitions.csv --target-app-partition factory target/xtensa-esp32-none-elf/release/esp32-bms-gps /tmp/esp32-bms-gps-factory.bin
node .gitnexus/run.cjs detect_changes --scope compare --base-ref main
```

## Risk

Touch remains hardware-sensitive. If taps still do not work after restoring the
IRQ path, serial diagnostics should show whether GPIO36 IRQ or raw SPI samples
change during a tap.
