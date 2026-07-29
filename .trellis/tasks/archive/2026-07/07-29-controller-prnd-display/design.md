# Design

## Data Flow

`FarDriver compact frame[4] & 0x03` -> `esp_fardriver_state_t.gear` ->
`snapshot.controller_gear` -> shared UI formatter -> controller page, standard speed dashboard, Fireblade dashboard.

The protocol parser changes only the bit selection. Runtime storage and its
public snapshot field remain `uint8_t`, preserving compatibility with all
existing consumers.

## Display Contract

The APK's live-telemetry contract is authoritative:

| Raw value | Display |
| --- | --- |
| 0 | `N` |
| 1 | `D` |
| 2 | `R` |
| 3 or invalid/offline | `-` |

`D` denotes the APK's forward state. Its optional three-speed submode depends
on an unparsed APK configuration field and is deliberately outside this task.
`P` is not a valid display result because the APK exposes it only as a
configuration capability, not current telemetry.

## UI

One file-local formatter in `esp_bms_lvgl_ui.c` removes three duplicate numeric
formatters. The controller and Fireblade dashboard labels change from their
digit-only fonts to existing `lv_font_montserrat_48`, which already renders the
required ASCII letters. The ordinary speed dashboard already uses Montserrat.

## Validation And Rollback

Extend the existing FarDriver self-test with each low-two-bit value. Add the
formatter's mapping check to the existing UI simulator smoke test. Rollback is
limited to the parser bit extraction and the single UI formatter.
