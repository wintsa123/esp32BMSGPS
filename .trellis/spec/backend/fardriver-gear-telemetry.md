# FarDriver Gear Telemetry Contract

## 1. Scope / Trigger

Apply this contract when decoding FarDriver compact real-time frames or
displaying `controller_gear` on a dashboard.

## 2. Signatures

- `parse_compact(esp_fardriver_state_t *state, uint8_t index, const uint8_t *data)`
- `esp_bms_dashboard_snapshot_t.controller_gear`

## 3. Contract

For compact frame index 0, preserve `data[2] & 0x03U` in `state->gear`.
The runtime projects that value unchanged to `snapshot.controller_gear`.
Dashboard formatting is one shared mapping: `0 -> N`, `1 -> D`, `2 -> R`,
and `3`, invalid data, or an offline controller -> `-`.

`P Gear` is a controller configuration capability, not confirmed real-time
telemetry. Do not display `P` without a verified live protocol field.

## 4. Validation & Error Matrix

| Condition | Required display |
| --- | --- |
| Online controller, valid raw `0` | `N` |
| Online controller, valid raw `1` | `D` |
| Online controller, valid raw `2` | `R` |
| Raw `3`, invalid, or offline | `-` |

## 5. Good / Base / Bad Cases

- Good: test all four low-bit values while preserving unrelated high bits.
- Base: project the decoded byte unchanged through the runtime snapshot.
- Bad: extract bits 2-3, remap zero, or use a numeric fallback such as `1`.

## 6. Tests Required

- `tests/fardriver_protocol_selftest.c` must assert compact-frame values 0 through 3.
- The LVGL simulator smoke check must assert the shared `N/D/R/-` mapping,
  including offline and invalid states.

## 7. Wrong vs Correct

```c
/* Wrong: reads unrelated bits and changes neutral to an invalid value. */
state->gear = (data[2] >> 2U) & 0x03U;
state->gear = state->gear == 0U ? 3U : state->gear;

/* Correct: preserve the APK-compatible real-time field. */
state->gear = data[2] & 0x03U;
```
