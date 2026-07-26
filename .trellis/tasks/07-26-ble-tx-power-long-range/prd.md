# BLE transmit power and long range

## Goal

Improve BLE link budget for all firmware targets that have an integrated BLE controller. On BLE 5 capable targets, request LE Coded PHY when the connected peer supports it.

## Confirmed Facts

- The reported symptom is a successful BLE connection followed by a disconnect at about one metre.
- The firmware uses one NimBLE host for BMS central, controller central, and local peripheral connections. The common initialization and local GAP callback are in `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`; the central GAP callbacks are in `components/esp_bms_bms_ble/esp_bms_bms_ble.c` and `components/esp_bms_controller_ble/esp_bms_controller_ble.c`.
- Current firmware targets are `esp32`, `esp32c3`, `esp32s3`, and `esp32p4`. The P4 catalog entry has no integrated BLE controller and declares an ESP32-C6 communication coprocessor; this repository does not currently build firmware for that coprocessor.
- ESP-IDF 6.0.2's NimBLE configuration exposes `CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY` only for BLE 5 capable targets. The legacy ESP32 cannot use LE Coded PHY.
- Scope decision: this task covers only the existing ESP32, ESP32-C3, and ESP32-S3 firmware builds. ESP32-P4 plus its declared ESP32-C6 coprocessor is deferred to a separate task.

## Requirements

1. Set BLE transmit power to the highest level supported by each integrated BLE controller without applying Wi-Fi transmit-power APIs to BLE.
2. Enable the NimBLE LE Coded PHY feature only in BLE 5 target defaults supported by ESP-IDF.
3. For BMS, controller, and local peripheral connections, request LE Coded PHY only after a connection is established and preserve normal 1M PHY operation when the remote peer does not support Coded PHY.
4. Emit one concise log for transmit-power setup failures and for PHY negotiation failures; a non-supporting peer must not be disconnected solely due to that negotiation.
5. Do not add scan, advertising, connection-interval, or Wi-Fi coexistence changes in this task.
6. Do not claim that a software change fixes an RF hardware fault; hardware range testing remains required.

## Acceptance Criteria

- [ ] ESP32, ESP32-C3, and ESP32-S3 builds initialize BLE transmit power at their supported maximum without build warnings or runtime failures.
- [ ] Coded PHY is compiled only for targets where ESP-IDF exposes the BLE 5 feature; the legacy ESP32 build remains supported.
- [ ] Each of the three established connection paths requests Coded PHY when available and stays connected if the peer rejects or lacks it.
- [ ] Targeted builds cover legacy ESP32 plus the BLE 5 configuration targets, and host self-tests continue to pass.
- [ ] Hardware testing records link stability and disconnect reason at 1 m and beyond for every actual board and paired peripheral.

## Out Of Scope

- Antenna, enclosure, PCB layout, power integrity, and external-radio redesign.
- Firmware for the ESP32-C6 coprocessor declared by the P4 catalog entry.
