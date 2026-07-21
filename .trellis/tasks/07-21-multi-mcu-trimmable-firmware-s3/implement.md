# Implementation Plan

## Ordered Child Delivery

- [ ] 1. `07-21-legacy-esp32-compat`: snapshot current source identity, normalized defaults, partitions, build/map/size, simulator UI state, and RFC2217 boot/hardware baseline.
- [ ] 2. `07-21-firmware-configurator`: define catalog schema and fixtures; implement Bash/PowerShell/CMD commands, canonicalization, dependency/GPIO validation, profile generation, and cross-shell tests.
- [ ] 3. `07-21-modular-firmware`: introduce core registry and generated CMake reachability; extract optional runtime components one by one; generate TFT/Web/API contributions and prove absence.
- [ ] 4. `07-21-multi-mcu-s3-board-support`: add ten-target capability records, legacy and S3 boards, target lock isolation, generated sdkconfig and partition selection.
- [ ] 5. `07-21-display-input-drivers`: generalize display buses/panels and input backends; add OLED UI boundary and capability-driven settings.
- [ ] 6. `07-21-bms-ble-protocol-suite`: implement licensed ANT/JK/JBD/Daly protocol/transport matrix and pure-C stream/parser tests.
- [ ] 7. `07-21-build-cloud-verification`: complete doctor/install support, local/cloud packaging, Actions matrix, module exclusion audits, and hardware/protocol verification reports.

## Integration Gates

- [ ] After each child: focused host tests, applicable ESP-IDF profile builds, `git diff --check`, and GitNexus `detect-changes`.
- [ ] Before editing any indexed function/class/method: GitNexus upstream impact analysis; warn before HIGH/CRITICAL changes.
- [ ] Before each component extraction: enabled legacy build is the control; after extraction compare size/map/symbol/UI/API evidence.
- [ ] Preserve user-owned dirty files and record overlaps before edits.
- [ ] Firmware-impacting checkpoints use the project RFC2217 flash/monitor skill; pin changes involving strapping pins include cold boot.
- [ ] Final integration runs all golden configurations, ten target baselines, seven module on/off pairs, BMS test matrix, and artifacts manifest verification.

## Validation Commands

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh build
git diff --check
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS
```

Additional generated-profile and cloud commands are added by their owning child and then become part of this final gate.

## Risk And Rollback Points

- `main/CMakeLists.txt`, component `CMakeLists.txt`, root sdkconfig/partition behavior, and `app_main` are compatibility-critical.
- The monolithic runtime and UI files currently contain user modifications; extraction must patch around them and never replace whole files from another revision.
- BLE/Wi-Fi coexistence, 4 MB image fit, S3 PSRAM allocation, I80 bandwidth, USB/JTAG pin reservations, and OTA boundaries require explicit reports.
- If a child breaks the legacy control build, stop later extractions, restore the last passing component/profile boundary without reverting unrelated user changes, and rerun the baseline comparison.
