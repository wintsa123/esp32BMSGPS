# GPS Diagnostic Telemetry

## Scenario: GPS Settings Diagnostics

### 1. Scope / Trigger

- Apply when changing NMEA GSA/GSV parsing, `esp_bms_idf_runtime_publish_gps_satellites()`,
  or the GPS settings page.

### 2. Signatures

```c
bool esp_bms_idf_runtime_publish_gps_satellites(
    esp_bms_idf_runtime_t *runtime,
    uint8_t satellites_visible,
    uint8_t satellites_used,
    uint8_t max_cn0,
    uint8_t average_cn0,
    uint8_t constellation_mask,
    uint8_t fix_dimension,
    uint16_t hdop_centi,
    bool hdop_valid,
    bool valid);
```

### 3. Contracts

- GSA owns fix dimension, used satellites, and HDOP in centi-units.
- GSV owns visible satellites, C/N0 sum/count, and the observed GPS/BDS/GLONASS mask.
- The UI consumes only the snapshot; do not query or reconfigure the receiver from a settings page.
- `valid=false` makes dynamic values unavailable. A constellation is observation data, not a claim
  that every receiver capability is enabled.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| Invalid NMEA checksum or malformed field | Reject sentence; do not publish new telemetry. |
| GSV PRN exceeds 255 | Parse it as `uint16_t`; unsupported constellations must not invalidate GSV. |
| GSA/GSV times out | Publish invalid satellite information and show placeholders. |

### 5. Good / Base / Bad Cases

- Good: fresh GSA plus GSV, 3D fix, valid HDOP, and a GPS/BDS/GLONASS mask.
- Base: only GPS is observed; render the single-GPS status.
- Bad: a 301 Galileo PRN causes a parser failure and hides all satellite information.

### 6. Tests Required

- `tests/gps_stream_selftest.c` must assert GSA HDOP, GSV C/N0 average inputs, GPS/BDS/GLONASS
  identification, and acceptance of a 301 PRN.
- Run `./scripts/run-host-selftests.sh`, the LVGL simulator smoke test, and a profile build.

### 7. Wrong vs Correct

```c
/* Wrong: a static capability label can claim a constellation with no live evidence. */
const char *system = "GPS+BDS+GLONASS 三模";

/* Correct: render the current mask obtained from validated GSV sentences. */
switch (snapshot->gps_constellation_mask) {
case 0x07U: system = "GPS+BDS+GLONASS 三模"; break;
default: system = "等待搜星"; break;
}
```
