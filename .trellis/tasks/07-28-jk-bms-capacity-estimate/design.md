# Design

## Data Flow

`JK 0x02 frame` -> `esp_bms_bms_telemetry_t.total_cycle_mah` ->
`bms_apply_telemetry` -> existing capacity estimator and NVS record ->
`esp_bms_dashboard_snapshot_t` -> read-only BMS settings row.

The JK decoder owns protocol validation and unit conversion. The runtime owns
type/MAC identity, persistence, and snapshot projection. LVGL only formats the
already typed snapshot value.

## JK Counter Contract

The raw JK field is a little-endian `uint32` at `154 + dynamic`. `dynamic` is
the decoder's existing `0` or `32` layout offset. The official app reports
`raw / 10 Ah`; therefore the shared telemetry field is `raw * 100 mAh`.
Reject a raw value that cannot be converted into `uint32_t` mAh.

## Runtime Contract

The existing estimator and NVS record are reused. Its type policy expands from
ANT to ANT-or-JK; the record layout is unchanged, so old ANT records remain
readable. The snapshot obtains one nonzero estimate field. A zero field means
that no estimate is ready for the current binding. It is cleared when BMS type
or bound MAC changes and set after the runtime verifies its type/MAC identity.

## UI Contract

The BMS settings page gains a fourth row created with the existing row helper,
then has clickability removed and opacity reduced. It has no action or event
handler. The page rebuild trigger includes the snapshot estimate field.

## Compatibility And Rollback

Only JK gains a protocol counter. ANT behavior and persisted records remain
compatible. Removing the new field and UI row rolls back the feature; no
settings migration or new NVS key is required.
