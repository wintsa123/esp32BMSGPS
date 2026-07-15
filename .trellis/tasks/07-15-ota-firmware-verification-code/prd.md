# Local Web OTA with 4-digit firmware code

## Goal

Add firmware upload and activation to the local `http://192.168.4.1` Web UI.
Prevent accidental activation of the wrong image by requiring a deterministic
four-digit code derived from the selected firmware file. Remove the redundant
Web API password prompt while retaining the required WPA2 Setup AP password.

## Background

- The verification code must be calculated from the firmware image contents.
- The code is exactly four decimal digits; leading zeroes are significant.
- The current partition table already provides `ota_0` and `ota_1`, each sized
  `0x1E0000`.
- As of 2026-07-15, the current `main` checkout contains the OTA partition
  layout but no repository implementation using `esp_ota_*` to receive and
  activate an update image.
- The local Web UI currently prompts for the Setup AP password and sends it in
  `X-Setup-Password` and Basic Auth headers for every API request.
- The firmware HTTP handler does not inspect or validate either authentication
  header. The prompt is therefore a client-only barrier, not effective server
  authentication.
- The Setup AP itself is correctly protected by an eight-digit random WPA2-PSK
  and must remain protected under the project constraints.
- Confirmed product decision: remove only the local Web page's additional
  password prompt and unused API authentication headers. Keep the eight-digit
  WPA2 hotspot password and its password-change flow.
- Confirmed algorithm: calculate standard CRC-32 over the complete app `.bin`,
  reduce it modulo 10000, and format the result as four decimal digits.
- Confirmed trust boundary: both the local Web UI and the Vercel UI collect and
  forward the user-entered code, but the ESP32 independently calculates the
  code from the bytes it actually receives and makes the final decision.
- The Vercel React UI already has an update tab with placeholder check/start
  actions, but it has no file input and its referenced OTA API routes are not
  implemented by the firmware.
- The Vercel page communicates directly from the browser to `192.168.4.1`; no
  Vercel backend proxies or stores the firmware image.
- The repository currently has no release helper that emits the four-digit code
  alongside a built firmware image.
- Confirmed delivery decision: the firmware build helper writes a text file
  beside the generated app `.bin`; the text file contains the matching
  four-digit code.
- Confirmed OTA transport boundary: both Web pages upload directly to the
  ESP32 over the Setup AP HTTP connection. Vercel does not proxy or retain the
  image, and OTA over the existing BLE control transport is out of scope.
- Confirmed activation boundary: receiving and writing the inactive slot may
  happen before the code is known, but the ESP32 must not select that partition
  for boot unless the complete image is valid and its computed code matches.
- A local OTA test image is available at
  `ota-images/esp32-bms-gps-ota-demo-20260715.bin` with SHA-256
  `f0f8fdc1ff33031e45c4c9adabddf9a88d9b6200ee662adea0e1ba0d799abff9`.

## Requirements

- Add an OTA section to the local Web UI with firmware file selection, a
  four-digit verification-code input, upload state, and a clear activation
  result.
- Add the equivalent firmware selection, four-digit code input, upload state, and
  result flow to the existing Vercel control UI when using the hotspot HTTP
  transport.
- Accept only an ESP32 application `.bin` that fits the inactive OTA partition.
- Stream the request body into the inactive OTA partition; do not buffer the
  complete firmware image in RAM.
- Calculate the code deterministically from the exact firmware `.bin` bytes.
- Use standard CRC-32 over the complete firmware bytes, then `% 10000` and
  `%04u` formatting.
- Format the result as `0000` through `9999`; never discard leading zeroes.
- Clear any previously entered code whenever the selected firmware file
  changes.
- Require an exact four-digit user input before the OTA image can be activated.
- Reject empty, non-numeric, wrong-length, and mismatched input with a clear
  Chinese message; provide the corresponding English text through the existing
  language mechanism if this is exposed in the local Web UI.
- A failed check must not select the new OTA partition for boot.
- The ESP32 must calculate the code from the received bytes and must not trust a
  browser-side calculated value as the final verification result.
- Validate the application image with ESP-IDF OTA APIs before selecting it as
  the boot partition.
- Reboot only after the HTTP success response has been sent.
- Document the algorithm so the same code can be reproduced by a host-side
  release/build tool and by the OTA receiver.
- Add a firmware build helper that reuses the existing ESP-IDF environment
  wrapper, builds the app image, and writes `<firmware-stem>.code.txt` beside
  the generated `.bin`. The file contains exactly four digits and a trailing
  newline.
- Treat the four-digit code as an operator confirmation check, not as firmware
  authentication or a cryptographic signature.
- Remove the local Web page's password prompt, session-stored password, Basic
  Auth header, and `X-Setup-Password` header from normal API calls.
- Keep the Setup AP SSID/password policy, WPA2 authentication, QR contents, and
  eight-digit password-change form unchanged.
- Use Chinese as the default OTA UI language and provide the matching English
  strings through the existing page language switch.

## Acceptance Criteria

- [ ] `http://192.168.4.1` loads status and configuration without an additional
      browser password prompt after the phone has joined the WPA2 hotspot.
- [ ] The hotspot still requires the current eight-digit WPA2 password.
- [ ] The existing password-change form still accepts only eight digits and
      updates the Setup AP credentials.
- [ ] Selecting a valid app `.bin` enables the OTA verification and upload flow.
- [ ] Both the local page and Vercel page require a four-digit code before
      starting upload.
- [ ] The same firmware bytes always produce the same four-digit code.
- [ ] Codes below 1000 are displayed and accepted with leading zeroes.
- [ ] Empty, non-numeric, three-digit, five-digit, and mismatched values block
      OTA activation.
- [ ] A matching code permits the normal OTA validation and activation path to
      continue.
- [ ] Supplying a forged browser-side result or bypassing browser validation
      cannot activate an image whose ESP-side CRC-derived code does not match.
- [ ] An oversized, truncated, malformed, or wrong-target image does not become
      the boot partition.
- [ ] A successful upload returns success, then reboots into the new OTA slot.
- [ ] OTA receives the firmware in bounded chunks and does not allocate a
      firmware-sized RAM buffer.
- [ ] Replacing the selected firmware file clears the previous input, and the
      ESP32 calculates the code from the newly uploaded bytes.
- [ ] Automated checks cover a known firmware fixture, a leading-zero result,
      malformed input, mismatch rejection, and successful matching input.
- [ ] The implementation never claims this four-digit value provides secure
      firmware authenticity.
- [ ] The firmware build helper produces the app `.bin` and a same-stem
      `.code.txt`; recomputing the code from the `.bin` matches the text file.

## Out Of Scope

- Secure Boot, signed firmware, anti-rollback, or cryptographic release
  authentication unless they are requested as a separate task.
- Changing the OTA partition layout.
- Removing or weakening the Setup AP WPA2 password.
- OTA transfer over the existing BLE control channel.
