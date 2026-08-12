# Validation

## Passed

- `./scripts/run-host-selftests.sh`
- Legacy ESP32 no-PSRAM profile build through `start.sh compile-local`
- Final application size: `0x12cb50`; smallest app partition free: `0xb34b0` (37%)
- `git diff --check`
- GitNexus compare against `origin/main`: 19 files, 94 symbols, 69 flows, CRITICAL aggregate risk

## Independent Review Fixes

- Controller speed-source resolution now reuses the projected `CONTROLLER_ONLINE` truth instead of treating a GAP handle as usable telemetry.
- Controller private first-frame and command timers are cleared on connection failure, GAP failure, disconnect, stop, and BLE reset.
- The blank row below the six-row first BLE page no longer maps to a hidden candidate; the simulator smoke includes the invalid-row assertion.
- A parsed parameter block no longer marks the controller online. `ONLINE` now requires RPM, current, controller-temperature, or motor-temperature telemetry that can populate the controller instrument; the FarDriver host self-test covers the parameter-only case.

## Automated Limits

- Direct `./scripts/esp-idf-env.sh build` is intentionally rejected by the profile-generation guard. The supported isolated `start.sh compile-local` path passed with temporary build/output roots.
- The repository has no `simulator/` directory, so the existing LVGL headless smoke source could not be executed.

## Hardware Blocker

- Endpoint TCP connect succeeds: `192.168.2.10:4000`.
- Server initially advertises TELNET options `fffb01fffb03fffd00fffb2c`, including RFC2217 COM-PORT-OPTION.
- A standard RFC2217 client negotiation is reset by the Windows peer before Flash write starts.
- Flash attempt 1: `Remote does not accept parameter change (RFC2217)`.
- Flash attempt 2: `we-RFC2217:False(REQUESTED)`.
- Minimal pySerial negotiation with debug and `timeout=10` is reset by peer with the same missing RFC2217 response.
- Linux has no competing esptool/monitor process or established TCP session. Windows ports 22, 5985, and 5986 are closed, so the bridge cannot be restarted remotely from this host.

Restart the Windows COM6 bridge, then resume with RFC2217 flash and monitor. Validate boot, scan names and pagination, NUS/FFE0 selection, subscription readiness, telemetry, disconnect reason, heap, and absence of panic/watchdog.
