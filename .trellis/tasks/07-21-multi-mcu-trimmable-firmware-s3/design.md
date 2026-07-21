# Technical Design

## 1. Architecture

The migration has four explicit boundaries:

1. **Catalog**: versioned, non-executable `KEY=VALUE` records describe MCU capabilities, boards, displays, inputs, modules, settings contributions, dependencies, conflicts, GPIO requirements, partitions, and verification status.
2. **Configurator**: Bash and PowerShell independently parse the same catalog into one canonical, sorted configuration. They validate before any build/install/cloud side effect.
3. **Generator**: a validated configuration creates a self-contained profile directory with sdkconfig defaults, target-specific component selection, generated C/H registry, assembled Web asset, partition CSV, dependency-lock selection, and build report.
4. **Firmware**: `main` orchestrates only generated core/module lifecycle hooks. Optional ESP-IDF components own their runtime, settings contributions, routes, and assets so component reachability is the trimming mechanism.

Default generated profiles live under `firmware-builds/<profile>/`; the directory contains `generated/` inputs and an isolated `idf-build/`. The root `sdkconfig` and existing default build entry remain compatibility inputs, not generator scratch space.

## 2. Catalog Contract

- Record encoding is UTF-8 text with LF output, ASCII keys, one `KEY=VALUE` field per line, blank lines and `#` comments allowed.
- Parsers accept a documented character set per field and reject duplicate keys, unknown required schema versions, malformed records, traversal, shell metacharacters in identifiers, and dependency cycles.
- No catalog file is sourced, dot-included, evaluated, imported as PowerShell code, or expanded by a shell.
- Component-local module descriptors are discovery units. Adding a custom module means adding an ESP-IDF component and descriptor; menu scripts contain no module-specific branches.
- Canonical output uses a fixed schema version, fixed key ordering, normalized booleans/integers/lists, escaped values, and LF newlines. Golden fixtures compare SHA-256 and bytes across shells.

## 3. Dependency And Capability Resolution

- Resolution is deterministic: requested user modules -> transitive internal dependencies -> conflict/capability checks -> stable topological ordering.
- Hardware resources are typed claims rather than names only: input, output, ADC, DAC, USB/JTAG, strapping, flash/PSRAM, display bus signals, and optional shared-bus group.
- Reusing a GPIO is illegal unless every claimant declares the same shareable bus identity and compatible electrical role.
- Dangerous but legal pins produce a confirmation token stored in the normalized config and a warning in the report; non-existent or electrically incapable pins are fatal.
- Required setting fields with no provider are fatal. Optional zero-provider fields are omitted; one provider is fixed and disabled; multiple providers generate a selectable list.

## 4. Generated Firmware Boundary

- Root CMake keeps `COMPONENTS main`. Generated profile selection changes `main` requirements through a generated component list, so unselected components never become reachable.
- Generated registry exposes lifecycle entries (`init`, `start`, `tick`, `stop`) and capability/settings tables for selected modules only. The core does not reference optional symbols directly.
- Web assembly concatenates a core shell with selected module fragments and emits a route registry consumed by the HTTP component. A disabled Web/network module embeds neither the shell nor module fragments.
- TFT settings consume the same generated capability table as Web generation. Shared option IDs remain stable across both frontends.
- Existing runtime is split incrementally behind the current public snapshot/action contract. Each extraction first preserves behavior, then removes the old implementation from the monolith.

## 5. Firmware Component Boundaries

- `esp_bms_core`: state, NVS schema, lifecycle, capability registry, action routing.
- `esp_bms_board`: generated board/GPIO/partition-facing compile-time configuration.
- `esp_bms_display` and input backends: bus/panel/indev construction; color and OLED presentation chosen by display capability.
- Optional components: `esp_bms_bms_ble`, `esp_bms_gps`, `esp_bms_controller_ble`, `esp_bms_audio_feedback`, `esp_bms_network`, `esp_bms_http_ota`, `esp_bms_cast`.
- Protocol parsers are host-testable pure C under the BMS component. BLE transport and parsers are separated so fragmented/coalesced notification tests do not need NimBLE.

## 6. Board And Target Strategy

- `esp32-wroom-32e-legacy` preserves current GPIO, no-PSRAM sdkconfig, and `partitions.csv` byte-for-byte unless an explicit compatibility migration is approved.
- `esp32s3-wroom-1-n16r8-i80` declares 16 MB flash, 8 MB PSRAM, I80 ILI9488, FT6336U, GNSS 39/40/41, I2S 42/47/48, and AMP_SHDN 2.
- Ten target records describe BLE/Wi-Fi, USB/JTAG/reserved pins, GPIO ranges/capabilities, supported display buses, flash/PSRAM limits, and baseline profiles.
- Managed-component resolution selects and preserves `dependencies.lock.${IDF_TARGET}`. A build never reuses another target's lock.

## 7. Build And Cloud Flow

- `doctor` is read-only until it has printed all missing prerequisites and received an explicit install confirmation.
- `build-local` validates, generates, invokes ESP-IDF 5.5.4 with the profile target/build/sdkconfig paths, then packages artifacts and hashes.
- `build-cloud` validates locally, verifies the ref exists remotely, encodes only canonical non-secret configuration, and calls `workflow_dispatch`. It never commits or pushes.
- GitHub Actions decodes and revalidates the configuration, builds in a pinned ESP-IDF 5.5.4 environment, and publishes the same artifact manifest as local builds.
- The four-digit firmware code is deterministic from the final OTA image hash and recorded with the full SHA-256.

## 8. Migration And Rollback

- Capture the legacy build before changing CMake reachability or runtime ownership.
- Land the configurator first with a legacy profile whose generated values equal current authorities.
- Extract one module at a time and prove both enabled behavior and disabled absence before deleting its old code.
- Keep the original command mapped to the legacy profile until final integration acceptance.
- Rollback is component/profile selection plus source-control revert of the current extraction; generated directories are disposable and never authoritative.

## 9. Trust And Verification

- Catalog/profile input is untrusted data even when stored in-repo; validation owns all path, identifier, integer, enum, graph and GPIO normalization.
- Cloud tokens are read from process/session only and redacted from logs.
- Reports distinguish `hardware-verified`, `protocol-compatible-unverified`, and `build-only` evidence.
- Completion evidence is requirement-scoped: build success alone cannot prove binary trimming, protocol correctness, UI contribution behavior, or hardware verification.
