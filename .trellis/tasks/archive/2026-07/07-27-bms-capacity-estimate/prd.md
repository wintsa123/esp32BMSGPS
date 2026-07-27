# BMS Real Capacity Estimate

## Goal

Show an estimated real battery capacity in the BMS information of the device, local Web UI, and Vercel configurator. The estimate uses the ANT BMS total-cycle Ah counter and stable SOC intervals; users do not need to discharge to 0% or charge to 100%.

## Confirmed Facts

- A BMS may initially report stale SOC after connecting and later recalculate it. That correction must never be treated as charge throughput.
- ANT BMS.apk 2.4.1 exposes `packAllAh` as Total Cycle separately from physical capacity and remaining capacity. This is the required cumulative cycle Ah source.
- In a new ANT status frame, physical capacity and remaining capacity are at dynamic offsets `50` and `54` in uAh, and the current firmware correctly projects them as mAh. Total-cycle Ah is a little-endian 32-bit mAh value at dynamic offset `58`; the firmware does not currently decode it.
- In an old ANT status frame, physical capacity, remaining capacity, and total-cycle Ah are respectively at offsets `75`, `79`, and `83`. Total-cycle Ah is a big-endian 32-bit mAh value and is not currently decoded.
- The App and firmware use FFE0/FFE1, new `7E A1 ... AA 55` frames, the same new status request `7E A1 01 00 00 BE 18 55 AA 55`, and the same old status request `DB DB 00 00 00 00`. This feature must not change BLE connection or polling behavior.
- The result must survive a device reboot and be visible in both requested Web settings surfaces.

## Requirements

- R1: Version one estimates capacity only from the ANT new and old total-cycle mAh fields. Other BMS types explicitly show that the estimate is unavailable.
- R2: Persist the confirmed estimate and BMS identity. During a connection, maintain an anchor of total-cycle Ah, SOC, and validation state, then calculate samples from monotonic valid intervals.
- R3: After connection, discard an interval and re-anchor when SOC is corrected without a meaningful matching total-cycle Ah change.
- R4: Do not publish an estimate for a short interval. Require at least a 20-point SOC span and 1 Ah total-cycle increase.
- R5: Persist the estimate and display state across reboots. A changed BMS identity or reduced total-cycle counter must not reuse prior history.
- R6: The local Web and Vercel BMS information use Chinese as the default text with English alternatives, and show either the capacity or an estimating/unavailable state.
- R7: Do not change the TFT localization limits. This feature adds no TFT copy.

## Acceptance Criteria

- [ ] Both ANT status formats decode total-cycle mAh separately from physical and remaining capacity; existing ANT connection and polling behavior is unchanged.
- [ ] Delayed SOC correction after connection does not create a capacity sample or alter an existing credible estimate.
- [ ] A valid non-0%-to-100% charge or discharge interval produces `Ah delta / SOC delta` capacity estimate.
- [ ] Invalid, short, reversing, or counter-reset intervals cannot update the estimate and safely re-anchor.
- [ ] Restarting preserves the valid estimate state; replacing the BMS or resetting its counter cannot reuse old history.
- [ ] The local Web and Vercel BMS information show the same estimate state, in Chinese by default and English when selected.
