# BLE peripheral notification security

## Scope
ESP-IDF 6.0.2 NimBLE central links to unencrypted BMS/controller telemetry devices.

## Configuration and APIs
`CONFIG_BT_NIMBLE_SM_LVL=0`; retain SECURITY_ENABLE, SC, LEGACY, bonding and encryption support.
`ble_att_svr_rx_notify()` receives the remote ATT notification before `BLE_GAP_EVENT_NOTIFY_RX`.

## Contract
NimBLE's global `sm_sec_lvl >= 2` silently discards unencrypted notifications before the application callback. Use IDF's default 0 for mixed central/peripheral roles. Do not use 1: IDF's `ble_sm.c` rejects incoming pairing requests at exactly 1.
Protect local BLE API requests with WRITE_ENC and responses with NOTIFY_INDICATE_ENC. Local HID READ_ENC, WRITE_ENC and NOTIFY_INDICATE_ENC must not depend on this global level. Never remove encryption support to receive telemetry.

## Validation matrix
- SM2 + unencrypted notification: freed, no callback (regression reproducer).
- SM0 + unencrypted notification: callback receives the unchanged payload.
- SM2 + encrypted notification: callback receives payload.
- Local API/HID at SM0: per-characteristic encryption flags remain set.

## Examples
Good: FFE0/FFEC properties 0x14 uses Write Without Response and receives plaintext notifications, while local control services require encryption.
Bad: treating no application NOTIFY_RX logs as proof the remote sent no packets; the host may silently filter them.

## Tests
Run `python -m unittest discover -s tests -p 'test_controller_ble_*.py'` with ESP-IDF 6.0.2 and GCC. The notification test compiles the actual IDF receive function. Validate on hardware using first-frame/online logs, not just successful CCCD writes or command submission.

## Wrong vs correct
Wrong: global SM_LVL=2 to protect local services, or HID encryption flags conditional on that value.
Correct: global SM_LVL=0 with independent encrypted characteristic permissions on local services.
