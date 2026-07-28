# Implementation Plan

1. Complete and review the active JK capacity-estimate task first, then
   rebase this task's assumptions on its finalized runtime/UI interfaces.
2. Add the minimal pure-C current integrator beside the existing capacity
   estimator, with the 0.5 A deadband, fractional remainder and gap result.
   Add its focused host self-test to the current capacity self-test command.
3. Extend runtime-owned capacity state with the integrator and add a
   current-based observer path. Reuse the existing mutex, identity validation,
   NVS persistence and snapshot projection; seed same-identity restarts from
   `last_accepted_cycle_mah` and reset all local state on identity change.
4. In `bms_apply_telemetry`, route only validated, non-partial Daly/彦阳
   telemetry with legal SOC through the new current-based observer. Keep ANT
   and JK on their native `total_cycle_mah` route. Do not alter poll requests
   or the 500 ms scheduler.
5. Expand the one runtime support policy and the existing disabled settings
   row to Daly/彦阳. Preserve JBD as unsupported, the existing HTTP fields,
   and no-action/dimmed row behavior.
6. Extend Daly/彦阳 protocol self-tests to assert their signed current and SOC
   input contracts. Test runtime identity reset/seeding where it can be done
   without ESP-IDF, otherwise cover it with an existing runtime-oriented
   harness or targeted device observation.
7. Run `./scripts/run-host-selftests.sh`, both landscape directions of the
   LVGL simulator, an `oldesp32` build, and a real BLE observation for each
   BMS. Before commit, run GitNexus impact analysis for every edited symbol
   and `detect-changes` against `main`.

## Risk Controls

- Do not infer capacity from `total_capacity_mah`, `capacity_remaining_mah`,
  or voltage.
- A rejected frame or gap must never add an inferred constant-current interval.
- Never persist every sample; persist only the existing estimate record after
  a meaningful estimate/identity change.
- Keep BMS type/MAC checks under the existing capacity-estimate lock so an
  asynchronous bind cannot mix two devices.
