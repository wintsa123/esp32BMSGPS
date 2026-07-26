# Design

## APK Evidence

| Contract | Evidence | Decision |
| --- | --- | --- |
| GATT transport | DEX `La/Ul;.<clinit>` and `La/El;.onServicesDiscovered` | Use Nordic UART service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, notify `...0003...`, write `...0002...`. |
| Read request | `NativeProtocol_fdBuildReadPacket` at `libfd_protocol.so+0xa20c` | Build five bytes: address twice, `0x80`, then the existing `0x3C/0x7F` CRC high then low. |
| Compact notification | native handler at `+0x9ef4` | Require 16 bytes and `0xAA`; for `frame[1] < 0x80`, checksum is the big-endian sum of bytes 0 through 13. |
| Extended notification | native handler `+0x9f90` and CRC helper `+0x127d0` | For `frame[1] & 0x80`, verify the existing `esp_fardriver_crc` over bytes 0 through 13, then index with `frame[1] & 0x7F`. |
| Register blocks | native table at `+0x52a9` | Keep the existing `FLASH_READ_ADDR` mapping, but make it authoritative only for high-bit extended responses. |
| Poll schedule | native `fdGetPollRegisters` at `+0xb07c`, `fdGetConfigRegisters` at `+0xb0d4` | Replace the one-off gather write with sequential app-derived real-time and configuration read requests. |

## Firmware Boundaries

- `esp_fardriver_protocol`: expose one frame parser that selects checksum and
  layout from the high bit; retain the existing CRC only for extended frames
  and requests. Add a read-request builder and the app-derived address lists.
- `esp_bms_controller_ble`: discover the 128-bit Nordic UART service, retain
  separate notify and write handles, subscribe to notify, and send one
  request at a time from the app-derived poll schedule.
- `esp_bms_idf_runtime`: project only corrected protocol state; no NVS or UI
  migration is required for this repair.
- `tests/fardriver_protocol_selftest.c`: use APK-format compact and extended
  frames plus known request bytes to prevent a return to the guessed format.

## Parsing Contract

- Compact index 0 provides raw electrical speed at bytes 6-7. Mechanical RPM
  is `raw * 4 / pole_pairs` when the controller reports a usable pole-pair
  count; otherwise preserve raw RPM rather than inventing a `/4` scale.
- Compact index 1 provides voltage at bytes 2-3 (0.1 V) and current at bytes
  4-5 (signed, 0.25 A); power is their product. The existing d/q-based power
  derivation is removed.
- Compact index 4 controller temperature is byte 4 and index 13 motor
  temperature is byte 2. Treat values above the APK's signed threshold as
  signed values; keep the last valid value on invalid frames.
- Extended responses use the existing six-word block storage. The D0 block
  contains D2/D3/D4, from which aspect, rim, tire width and `RateRatio` are
  derived as already specified in the parent task.
- The pole-pair source must be identified in the APK configuration response
  during implementation. Until a valid value arrives, RPM is marked valid as
  raw controller RPM but speed continues to use the existing wheel/ratio
  calculation. No guessed constant is introduced.

## Compatibility And Rollback

- Do not send any FarDriver write/configuration/control command. The only
  outbound payload is the APK's five-byte read request sent to the BLE write
  characteristic; it asks the controller to return data and does not mutate
  controller state.
- All validation happens before state mutation, so malformed notifications
  retain the last valid telemetry.
- The Nordic UART UUID change affects the full controller connection path and
  is a HIGH-risk integration change. Rollback is confined to the controller
  BLE component and protocol parser.
- 本任务不进行真实硬件验证；服务发现、通知、轮询节奏和 RPM 极对数行为
  是后续独立的硬件验收项。
