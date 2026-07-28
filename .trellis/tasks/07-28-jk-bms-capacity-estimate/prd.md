# JK BMS Real Capacity Estimate

## Goal

Show a persisted, non-editable estimate of usable pack capacity for a bound JK
BMS. Reuse the existing SOC-span estimator and JK's hardware cumulative
charging capacity counter.

## Confirmed Facts

- The existing estimator calculates `delta cumulative mAh * 100 / delta SOC`
  and is currently restricted to ANT.
- JK protocol `0x02` telemetry contains the cumulative charging capacity as a
  little-endian `uint32` at `154 + dynamic`, where `dynamic` is `0` for the
  24-slot layout and `32` for the 32-slot layout.
- The official JK Android app v6.0.10 presents this raw field divided by 10,
  so the firmware converts it from deci-Ah to mAh by multiplying by 100.
- The BMS settings page currently has no capacity-estimate row. Its data comes
  exclusively from `esp_bms_dashboard_snapshot_t`.

## Requirements

1. Decode JK cumulative charging capacity with the official-app scaling,
   populate `total_cycle_mah`, and mark it valid without changing JK polling.
2. Allow the existing persistent estimate path for ANT and JK only, keyed by
   BMS type and bound MAC. Existing ANT NVS data must remain valid.
3. Project the current estimate into the dashboard snapshot and clear it on a
   BMS type or bound-MAC change so the settings view never shows another BMS's
   result.
4. Add a grey, non-clickable `Real capacity estimate` row to BMS settings.
   It shows the value for a ready ANT/JK estimate, `Estimating` while supported
   data is incomplete, and `ANT / JK only` for all other BMS types.
5. Do not implement Daly, Yanyang, JBD, local current integration, new BLE
   polling, a new FreeRTOS task, or a new settings action in this task.

## Acceptance Criteria

- [ ] JK self-test covers both layout offsets, little-endian decoding, the
      official 0.1 Ah scaling, and `total_cycle_valid`.
- [ ] JK telemetry reaches the existing capacity estimator, including after
      restart with the same type and MAC; an ANT record remains loadable.
- [ ] Changing BMS type or bound MAC clears the projected estimate until the
      correct BMS supplies telemetry.
- [ ] The BMS settings page exposes the read-only row, refreshes when its
      value changes, and cannot trigger an action.
- [ ] Host self-tests, targeted simulator checks, and the relevant firmware
      build succeed.

## Out Of Scope

- Daly, Yanyang, and JBD capacity estimation.
- A current-integration fallback or any extra BLE polling.
