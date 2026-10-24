# Controller binding persistence

## Contract

- Factory defaults contain no controller MAC. Runtime initialization leaves the binding empty; no profile or flash artifact should inject a test controller address.
- User selection persists the address in NVS `ctl_mac` and enables controller connection in the same settings commit. Boot restores that selection; an empty address must not start an automatic connection scan.
- Preserve existing user bindings across normal firmware updates. Do not erase NVS to remove a presumed compiled default.
- HTTP pending binding must set `controller_connection_enabled` before saving. If saving fails, restore both the previous address and previous enabled state, and do not start the new connection.
- An explicit user disable remains effective after reboot.

## Call paths and verification

- Boot: `esp_bms_module_registry_start` → `esp_bms_controller_ble_start` → `controller_start_if_enabled`.
- Screen: committed `START_CONTROLLER_BIND` with `CONTROLLER_MAC_VALID` → runtime selection → main settings save.
- Web: `runtime_apply_pending_http_controller_bind` → settings commit → connection start.
- `tests/test_controller_binding_persistence.py` compiles real C function bodies and tests save inputs, rollback, empty binding and restored-binding startup gates. The reboot scenario uses saved fields in a host harness; it does not emulate NVS storage itself.
