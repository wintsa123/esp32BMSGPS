# BMS Link and Telemetry Contract

## 1. Scope / Trigger

Applies to the optional BMS NimBLE driver, pure-C protocol parsers, snapshot
projection, and device configuration/status JSON. All protocols share one
runtime owner; this is not a multi-device driver.

## 2. Signatures

- `esp_bms_ant_protocol_feed(stream, stream_len, capacity, chunk, chunk_len,
  telemetry, device_info)` consumes complete new ANT frames. Its return means
  a status was decoded; `device_info` independently reports an identity frame.
- `esp_bms_em02_feed(..., response_index, identity_known, telemetry)` returns
  one validated page and classifies its response index by byte count, independent
  of the most recently sent request. NULL/empty input drains buffered pages.
- `GET/POST /api/config`: `ant_channel` is an integer 0 (FFE1/FFE1),
  1 (FFF3/FFF4), or 2 (FFF5/FFF6), persisted in NVS `ant_channel`.
- `GET /api/status`: optional `bms_ant_diagnostics` contains read-only cell,
  MOS, balance, SOH, signed power, and optional time fields.

## 3. Contracts

- Default ANT channel is 0. A/B selection is explicit; UI explains that A
  occupies the Bluetooth display interface and B occupies the charger interface.
  Do not automatically probe these channels. Require a writable FFF5 capability
  and valid selected characteristic properties before using A/B.
- Every asynchronous scan/connection/GATT callback belongs to a session token.
  Reused numeric connection handles alone cannot establish ownership. Establishment
  is bounded by a 10-second deadline; write/subscribe failure enters disconnect
  recovery. Retry a failed termination every 3 seconds before reconnecting.
- CCCD always uses an acknowledged write. Polls prefer WRITE when supported,
  otherwise WRITE_NO_RSP. EM02 requires WRITE. Descriptor search ends before the
  next characteristic declaration so another characteristic's CCCD cannot match.
- ANT primary and auxiliary segments each have independent Modbus CRCs. Consume
  by lengths; only the complete validated compound frame with AA55 is published.
  Payload AA55 bytes are not terminators. Preserve incomplete suffixes and bound
  all buffers; a single compound frame must fit the provided stream capacity.
- Full telemetry preserves all-field semantics. `partial=true` uses
  `present_fields`; zero values are valid, absent fields retain the snapshot.
  A present temperature group replaces its validity bitmap, including clearing
  old sensors. An absent group does not clear temperatures. Bluebabe identity
  capacity pages do not establish online state or refresh the live heartbeat.
- Diagnostic fields flow through the C snapshot to JSON; missing optional
  extensions serialize as null. Disconnect/reset clears diagnostics. Snapshot
  size assertions must be updated together with all same-header consumers.
- EM02 field integers are little endian: Motexa `v6.w0.e/f` independently agrees
  with maibms LuXingProtocol. Requests retain Modbus register/network byte order;
  receive CRC remains little endian. Model at frame offset 67 must include EM02
  before publishing telemetry; realtime/auxiliary replies cannot bypass this gate.
  Reference-source agreement is not evidence of physical EM02 validation.

## 4. Validation & Error Matrix

| Input or event | Required result |
| --- | --- |
| CCCD write error or establishment deadline | Disconnect and bounded retry, no fake online |
| Old session callback with reused handle | Ignore; never clear the new session |
| No WRITE property on EM02 | Reject channel, do not use no-response writes |
| Missing ANT selected channel/capability | Offline with discovery error, no silent alternate |
| Bad segment CRC/length | No telemetry publication; stream resynchronizes |
| Partial capacity-only page | Retain live readings and heartbeat timestamp |
| ant_channel omitted / old NVS key absent | Retain current choice / default to 0 |
| ant_channel -1, 3, 256, decimal, string | Reject config with 400; queue unchanged |
| Late EM02 response | Classify by validated byte count, not request index |

## 5. Good / Base / Bad Cases

- Good: Bluebabe realtime -> identity -> zero-current realtime preserves capacity
  and displays zero current/SOC as valid.
- Base: short ANT extension exposes only fields completely present in payload.
- Bad: accepting a main ANT segment before validating its corrupt auxiliary CRC,
  or treating an identity page as successful live telemetry.

## 6. Tests Required

`bash scripts/run-host-selftests.sh` includes ANT/EM02 C parser regression and
`test_bms_*.py`, which compile actual C functions for transport recovery,
telemetry projection/status JSON, and config/pending/NVS behavior. Cover split
headers, multiple frames, CRC errors, embedded AA55, optional-extension bounds,
zero and absent values, callback ownership, property selection and strict config
integers. Build the current hardware profile and record RFC2217 flash outcome.
Real-device long-duration connection stability needs independent observation.

## 7. Wrong vs Correct

Wrong: clear all numeric fields for every partial frame, or infer EM02 page from
an incremented poll counter. Correct: apply explicit present fields and classify
CRC-checked response byte counts. Never weaken CRC or model validation to make
an incompatible device appear online.
