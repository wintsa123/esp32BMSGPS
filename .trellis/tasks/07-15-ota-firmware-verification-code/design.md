# Design: Local Web OTA with 4-digit firmware code

## Boundaries

- Firmware receiver: `components/esp_bms_idf_runtime/esp_bms_idf_runtime.c`.
- Embedded local UI: `main/web/index.html`.
- Public control UI: `vercel/src/App.tsx` and existing styles.
- Release helper: one new Python script under `scripts/` that calls the existing
  ESP-IDF environment wrapper and uses only the Python standard library.
- OTA uses the existing `ota_0` / `ota_1` partition table. No partition or
  `sdkconfig` changes are needed.

## HTTP Contract

Add `POST /api/ota` to the existing `/api/*` wildcard handler.

- Request body: raw ESP32 application `.bin` bytes.
- `Content-Type`: `application/octet-stream`.
- `X-Firmware-Code`: exactly four ASCII decimal digits.
- Response: small JSON success or a clear HTTP error. CORS permits the custom
  verification header for the Vercel page.

The handler rejects missing/invalid code, empty input, and images larger than
the next OTA partition before starting the write. It allocates one bounded
receive buffer, streams `httpd_req_recv()` chunks to `esp_ota_write()`, and
updates `esp_rom_crc32_le()` with the same chunks.

After the complete body arrives:

1. Compare `crc32 % 10000`, formatted as four digits, with the request header.
2. On mismatch, call `esp_ota_abort()` and leave the boot partition unchanged.
3. On match, call `esp_ota_end()` to validate the app image.
4. Only then call `esp_ota_set_boot_partition()`.
5. Send the success response, wait briefly for delivery, then restart.

Any receive, flash, CRC, or image validation failure aborts the OTA handle and
does not change the boot partition. The four-digit code is confirmation only;
ESP-IDF image validation remains authoritative for image structure.

## CRC Contract

Use the standard reflected CRC-32 used by Python `zlib.crc32` and ESP-IDF
`esp_rom_crc32_le`, initialized with zero and updated incrementally over every
byte of the app `.bin`. The verification code is `crc32 % 10000`, formatted as
`%04u`.

The known vector `123456789` must produce CRC `0xCBF43926`.

## Web UI

Both pages use native file inputs and send the selected `File` directly as the
raw request body. They require a four-digit input before upload, disable the
submit button while uploading, and show Chinese-first success/failure state.

The Vercel update tab uploads only over the existing hotspot HTTP transport. If
BLE is selected, it asks the user to connect through the hotspot API instead of
attempting a large BLE transfer.

The embedded page deletes its `sessionStorage` password state, prompt helpers,
Basic Auth header, and `X-Setup-Password` header. The Setup AP password-change
form and WPA2 configuration are unchanged.

## Build Output

Add `scripts/build-firmware.py`:

1. Run `scripts/esp-idf-env.sh build` from the repository root.
2. Read `build/esp32_bms_gps_idf.bin` in chunks with `zlib.crc32`.
3. Write `build/esp32_bms_gps_idf.code.txt` containing only four digits and a
   trailing newline.
4. Print both output paths and the code.

The script includes a small self-test for the known CRC vector and four-digit
formatting. No new dependency is introduced.

## Compatibility And Rollback

- Target remains ESP32-WROOM-32E, 4 MB Flash, no PSRAM, ESP-IDF 5.5.4.
- Automatic rollback and anti-rollback remain disabled and out of scope.
- A failed upload leaves the running partition unchanged. A successful upload
  can be reversed by uploading a previous valid app image with its own code.
- Firmware upload is hardware-validated through the fixed RFC2217 bridge after
  build checks, then exercised from a browser joined to the Setup AP.

