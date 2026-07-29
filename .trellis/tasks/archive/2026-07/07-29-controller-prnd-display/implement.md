# Implementation

1. Run GitNexus upstream impact analysis for the parser and UI update symbols;
   halt for HIGH or CRITICAL risk.
2. Update `tests/fardriver_protocol_selftest.c` to lock APK-compatible low-bit
   extraction for raw values 0 through 3.
3. Change `parse_compact()` to preserve `data[2] & 0x03` without remapping.
4. Add one UI formatter for the `N/D/R/-` contract, route all three dashboard
   paths through it, and replace digit-only fonts where letters are displayed.
5. Add the formatter mapping to the existing simulator smoke check.
6. Run the protocol self-test, simulator build and smoke, `git diff --check`,
   targeted ESP-IDF build, and GitNexus change detection.

## Rollback

Revert the parser mask and shared UI formatter together. Do not add a
controller command, persistent setting, or guessed `P` mapping.
