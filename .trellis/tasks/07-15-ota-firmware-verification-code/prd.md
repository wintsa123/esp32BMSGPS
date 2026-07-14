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
- A local OTA test image is available at
  `ota-images/esp32-bms-gps-ota-demo-20260715.bin` with SHA-256
  `f0f8fdc1ff33031e45c4c9adabddf9a88d9b6200ee662adea0e1ba0d799abff9`.

## Requirements

- Add an OTA section to the local Web UI with firmware file selection, a
  four-digit verification-code input, upload progress/state, and a clear
  activation result.
- Accept only an ESP32 application `.bin` that fits the inactive OTA partition.
- Stream the request body into the inactive OTA partition; do not buffer the
  complete firmware image in RAM.
- Calculate the code deterministically from the exact firmware `.bin` bytes.
- Format the result as `0000` through `9999`; never discard leading zeroes.
- Recalculate the code whenever the selected firmware file changes.
- Require an exact four-digit user input before the OTA image can be activated.
- Reject empty, non-numeric, wrong-length, and mismatched input with a clear
  Chinese message; provide the corresponding English text through the existing
  language mechanism if this is exposed in the local Web UI.
- A failed check must not select the new OTA partition for boot.
- Validate the application image with ESP-IDF OTA APIs before selecting it as
  the boot partition.
- Reboot only after the HTTP success response has been sent.
- Document the algorithm so the same code can be reproduced by a host-side
  release/build tool and by the OTA receiver.
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
- [ ] The same firmware bytes always produce the same four-digit code.
- [ ] Codes below 1000 are displayed and accepted with leading zeroes.
- [ ] Empty, non-numeric, three-digit, five-digit, and mismatched values block
      OTA activation.
- [ ] A matching code permits the normal OTA validation and activation path to
      continue.
- [ ] An oversized, truncated, malformed, or wrong-target image does not become
      the boot partition.
- [ ] A successful upload returns success, then reboots into the new OTA slot.
- [ ] OTA receives the firmware in bounded chunks and does not allocate a
      firmware-sized RAM buffer.
- [ ] Replacing the selected firmware file invalidates the previous input and
      calculates the new file's code.
- [ ] Automated checks cover a known firmware fixture, a leading-zero result,
      malformed input, mismatch rejection, and successful matching input.
- [ ] The implementation never claims this four-digit value provides secure
      firmware authenticity.

## Out Of Scope

- Secure Boot, signed firmware, anti-rollback, or cryptographic release
  authentication unless they are requested as a separate task.
- Changing the OTA partition layout.
- Removing or weakening the Setup AP WPA2 password.

## Open Questions

1. Confirm the algorithm. Recommended minimum: `CRC32(firmware_bytes) % 10000`
   formatted with `%04u`; it is deterministic, small, and easy to reproduce.
2. Decide where the operator obtains the expected code. Recommended: generate
   it in the trusted release/build output and require the user to type it into
   the OTA UI; displaying and typing a code calculated by the same UI is only a
   confirmation gesture.
3. Confirm the byte scope. Recommended: hash the complete app `.bin` exactly as
   uploaded, without excluding headers or metadata.
4. Confirm the gate point. Recommended: the receiver may write the inactive OTA
   slot while calculating the code, but it must not call the boot-partition
   activation step until the entered code matches.

## Notes

- This is a cross-layer task involving the OTA receiver, local Web UI, and a
  host-side release helper. Add `design.md` and `implement.md` after the open
  questions are resolved and before running `task.py start`.
