# Quality Guidelines

> Code quality standards for frontend development.

---

## Overview

<!--
Document your project's quality standards here.

Questions to answer:
- What patterns are forbidden?
- What linting rules do you enforce?
- What are your testing requirements?
- What code review standards apply?
-->

(To be filled by the team)

---

## Forbidden Patterns

<!-- Patterns that should never be used and why -->

(To be filled by the team)

---

## Required Patterns

<!-- Patterns that must always be used -->

(To be filled by the team)

---

## Testing Requirements

### Scenario: Embedded Web UI API Field Consumption

#### 1. Scope / Trigger

- Trigger: the device-hosted web UI is embedded in firmware and consumes JSON
  produced by no-heap Rust API writers.
- Applies to `src/web/index.html` and status/config JSON emitted from
  `src/local_api.rs`.

#### 2. Signatures

- `GET /api/status` is the only dashboard refresh endpoint.
- `GET /api/bms/candidates` returns the BMS scan list consumed by the BMS bind
  UI.
- `POST /api/bms/scan` requests BLE scanning; it does not bind a MAC by itself.
- `POST /api/bms/bind` stores the selected MAC.
- The web refresh function must tolerate missing optional numeric fields by
  displaying `--`.

#### 3. Contracts

- The battery metric displays `local_battery_mv` when present.
- If `local_battery_mv` is absent, the battery metric falls back to
  `pack_voltage_mv`.
- Millivolt values are displayed as volts with two decimals.
- Do not add framework dependencies, remote assets, charting libraries, or CDN
  calls to the embedded UI.
- BMS candidate rows consume `mac`, optional `name`, and `rssi`. Clicking a row
  may fill the BMS MAC input; binding still posts the selected MAC explicitly.

#### 4. Validation & Error Matrix

- Missing optional battery values -> display `--`.
- Fetch failure during setup AP startup -> keep the page usable and show Wi-Fi
  as `setup`.
- New JSON field consumed by the web UI -> add or update a Rust API test that
  asserts the exact field name.
- Missing or empty BMS candidates -> render an empty candidate list, not stale
  fake devices.

#### 5. Good/Base/Bad Cases

- Good: `{ "local_battery_mv": 3300, "pack_voltage_mv": 52840 }` displays
  `3.30 V`.
- Base: `{ "local_battery_mv": null, "pack_voltage_mv": 52840 }` displays
  `52.84 V`.
- Bad: reading `data.battery` displays `--` because the API does not emit that
  field.

#### 6. Tests Required

- Keep host Rust tests for JSON writers as the authoritative field-name tests.
- For any nontrivial web parsing logic, prefer a small pure JS helper function
  that can be visually inspected and kept framework-free.
- BMS scan/bind UI must remain vanilla JS and must not introduce any framework
  bundle.

#### 7. Wrong vs Correct

##### Wrong

```javascript
const battery = data.battery;
```

##### Correct

```javascript
const battery = data.local_battery_mv ?? data.pack_voltage_mv;
```

---

## Code Review Checklist

<!-- What reviewers should check -->

(To be filled by the team)
