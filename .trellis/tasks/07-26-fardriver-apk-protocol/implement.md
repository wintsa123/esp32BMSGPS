# Implementation

## Ordered Checklist

1. Load backend specifications and record the current dirty-worktree baseline.
2. Run GitNexus upstream impact on every changed protocol and BLE symbol.
   Report the blast radius before edits; stop for any HIGH/CRITICAL warning.
3. Extend the host self-test first with APK-format compact and extended frames,
   known read-request bytes, checksum rejection and high-bit layout selection.
4. Update `esp_fardriver_protocol` for per-layout validation, app-derived
   compact telemetry scaling, and bounded poll/config address access.
5. Update controller BLE service/characteristic discovery to Nordic UART,
   keep separate notify/write handles, and replace gather with a paced
   read-only poll sequence. Do not add a FarDriver register-write or control
   payload.
6. Run the protocol self-test and affected host checks after each layer.
7. Run `trellis-check`, `git diff --check`, the configured ESP-IDF build, and
   GitNexus change detection before any hardware action.
8. 不刷写也不连接真实控制器；记录硬件验证为后续独立验收项。

## Validation

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Icomponents/esp_fardriver_protocol/include \
  tests/fardriver_protocol_selftest.c \
  components/esp_fardriver_protocol/esp_fardriver_protocol.c \
  -lm -o /tmp/fardriver_protocol_selftest
/tmp/fardriver_protocol_selftest

git diff --check
./scripts/esp-idf-env.sh build
node .gitnexus/run.cjs detect_changes --scope compare --base-ref main
```

## Rollback Points

- The parser and self-test are the first rollback point.
- The GATT UUID/poll change is confined to `esp_bms_controller_ble`.
- No persistent configuration or controller parameter is modified.
