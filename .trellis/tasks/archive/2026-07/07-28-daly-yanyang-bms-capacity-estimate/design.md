# Design

## Data Flow

```text
validated Daly / Yanyang telemetry
    -> local abs(current) integrator
    -> synthetic monotonic total_cycle_mah
    -> existing SOC-span capacity estimator and NVS record
    -> dashboard snapshot / HTTP status
    -> disabled BMS settings row
```

The existing capacity estimator remains the single owner of the SOC-span
math, sample smoothing and persisted estimate. The runtime owns the local
integrator because it also owns BMS identity, NVS and `esp_timer_get_time()`.
The protocol decoders continue to own byte order, scaling and CRC validation.

## Integrator Contract

The small pure-C integrator state has: last accepted telemetry timestamp,
synthetic accumulated mAh, fractional numerator remainder, and a valid flag.
It has no NVS record and allocates no memory.

For every eligible telemetry sample after the first:

```text
delta_mah = (abs(current_deci_amps) * delta_us + remainder) / 36,000,000
remainder = (...) % 36,000,000
```

`current_deci_amps` has a `0.1 A` scale. Values below `0.5 A` are treated as
zero to reject offset noise. The numerator uses `int64_t`; `INT16_MIN` is
widened before calculating the absolute value. The integrator accepts at most
a three-second interval. A longer, non-positive or otherwise invalid gap
updates its timestamp but adds no charge, and the existing SOC estimator
anchor is reset before the current SOC is observed.

This counts throughput, not signed energy. Both charging and discharging can
therefore form a SOC-span sample. The existing estimator independently detects
SOC direction reversals and reanchors.

## Identity And Persistence

For a matching supported BMS type and bound MAC, the first post-restart
integrator sample seeds its synthetic count from the existing
`last_accepted_cycle_mah`. It follows that persisted estimates remain valid
and the next accepted span is monotonic. The in-progress fractional integral
is intentionally not saved, avoiding NVS wear; at most the unaccepted segment
since the prior estimate is discarded after restart.

Before this seed, the runtime validates the capacity identity under the
existing capacity-estimate mutex. A new type or MAC resets both the SOC
estimator and the integrator, clears the snapshot field, and records the new
identity. This preserves existing ANT/JK blob layout and prevents a stale
estimate from appearing while a new BMS is connecting.

## Support And UI Policy

One runtime predicate is extended from ANT/JK to ANT/JK/Daly/彦阳. It governs
NVS validation, observer entry, snapshot/HTTP readiness and the supported
state. JBD remains excluded.

The BMS settings page keeps the JK task's one disabled row. Its label stays
Chinese; its subtitle is `估算中`, formatted Ah, or an explicit supported-brand
list for an unsupported BMS. The row has no action and stays visually dimmed.
No new UI controls, settings, polling routes or protocol fields are added.

## Accuracy Limits

The result estimates usable amp-hour capacity over the BMS's reported SOC
range. It is not a voltage-derived value and is not a battery-health result.
The LFP voltage plateau makes voltage-based SOC particularly unreliable in the
middle range, so this design deliberately relies on the BMS SOC. Hardware
validation must compare one full charge/discharge against a known external
coulomb counter; the `0.5 A` deadband and three-second limit are the only
initial calibration constants.

## Compatibility And Rollback

ANT/JK continue supplying their BMS-native counters unchanged. Deleting the
Daly/彦阳 observer route and integrator support returns them to unsupported;
old NVS data remains structurally compatible. No persisted schema version
bump is needed.
