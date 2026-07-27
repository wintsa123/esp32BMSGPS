# BMS Real Capacity Estimate Design

## Scope

Version one supports only ANT new and old protocols. It preserves existing BLE UUIDs, polling, frame validation, and other BMS telemetry. It adds neither a setting nor TFT UI.

## Data Flow

```text
ANT status frame
  -> telemetry.total_cycle_mah (valid only for ANT)
  -> runtime capacity estimator
  -> NVS blob keyed by bound ANT MAC
  -> /api/status
  -> local Web BMS section and Vercel BMS settings section
```

The decoder owns byte order and unit conversion. The estimator never reads raw frames, and neither Web client derives capacity from SOC or Ah values.

## Protocol Contract

`esp_bms_bms_telemetry_t` gains an optional `total_cycle_mah` value and validity bit.

- New ANT status frame: read a little-endian `uint32_t` at `58 + 2 * cell_count + 2 * temperature_sensor_count`. The field is mAh.
- Old ANT status frame: read a big-endian `uint32_t` at offset `83`. The field is mAh.
- Other BMS decoders leave the field invalid.

The runtime projects the ready estimate through `GET /api/status`:

```json
{
  "bms_capacity_estimate_mah": 52300,
  "bms_capacity_estimate_state": "ready"
}
```

`bms_capacity_estimate_mah` is `null` until ready. State is `unsupported` for non-ANT, `estimating` for ANT with insufficient valid data, and `ready` once at least one valid interval is accepted.

## Estimator

The state contains the persisted estimate, capped smoothing weight, sample count, format version, BMS type, and normalized bound MAC. The in-memory connection anchor contains only the last accepted total-cycle mAh, SOC, and observed SOC direction.

1. Require full ANT telemetry, a bound MAC, valid SOC, and valid total-cycle mAh.
2. The first valid observation becomes the anchor.
3. A reduced total-cycle value invalidates the stored estimate and starts a new anchor; this handles BMS counter reset or a different BMS.
4. A changed SOC with no meaningful total-cycle increase is treated as delayed SOC recalculation and replaces the anchor without producing a sample.
5. When SOC reverses direction, replace the anchor. This prevents a charge and discharge segment from being combined.
6. Accept a sample only after at least a 20-point SOC span and 1 Ah total-cycle increase. Calculate `sample_mAh = delta_cycle_mAh * 100 / delta_soc_percent`.
7. The first sample becomes the estimate. Later samples use a capped weight of four (`estimate * weight + sample`) so newer valid intervals continue to affect the result without retaining a history array; the current point then becomes the next anchor.

The thresholds deliberately favor fewer, credible updates over immediate output. A user need not reach 0% or 100%.

## Persistence And Reset

Persist only when an estimate is accepted or invalidated, using one versioned blob in the existing `esp_bms` NVS namespace. This follows the existing ride-record pattern and avoids flash writes for every BLE packet.

At load time, reject an unknown format or a saved BMS type/MAC different from the currently bound BMS. A BMS rebind also clears the active result immediately. In both cases present `estimating` and wait for new ANT telemetry. A missing or invalid blob is not an error.

## Presentation

Both user-facing strings are Chinese by default with English alternatives:

- `电池真实容量估算`: `<n.n> Ah` when ready.
- `电池真实容量估算`: `估算中` while ANT data is being collected.
- `电池真实容量估算`: `仅支持 ANT` for other BMS types.

The local HTML BMS settings section is refreshed from `/api/status`. Vercel renders the same value beside the dynamic `bms` settings section from the already-loaded status object. No new settings-manifest control or separate endpoint is required.

## Failure And Rollback

Malformed, partial, or unsupported telemetry cannot alter the estimate. NVS save failures leave the in-memory result active and retry through the runtime's existing deferred persistence style. Removing the new NVS key and fields returns behavior to the pre-feature state; no configuration migration is required.
