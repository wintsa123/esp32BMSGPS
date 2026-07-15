# Implementation Plan

1. Run GitNexus upstream impact analysis for every firmware and frontend symbol
   to be changed; stop and report any HIGH or CRITICAL result.
2. Add `app_update` as a direct runtime component dependency and implement the
   streaming `POST /api/ota` handler with bounded input, ESP-side CRC checking,
   image validation, boot selection, error responses, and delayed restart.
3. Extend common CORS headers for `X-Firmware-Code` without adding a new HTTP
   server or URI registry.
4. Add the embedded local OTA form and remove only its redundant Web password
   prompt/header code. Preserve the eight-digit WPA2 password form and policy.
5. Replace the Vercel update placeholders with HTTP-only file upload and
   four-digit code input, retaining the current direct-to-device PNA fetch path.
6. Add `scripts/build-firmware.py` and its standard-library CRC self-test; emit
   the same-stem `.code.txt` after a successful normal firmware build.
7. Validate:
   - `python3 scripts/build-firmware.py --self-test`
   - `python3 scripts/build-firmware.py`
   - independently recompute the generated `.bin` code and compare the `.txt`
   - `npm run typecheck --prefix vercel`
   - `npm run build --prefix vercel`
   - `git diff --check`
   - `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS --scope compare --base-ref main`
8. Flash once through the fixed RFC2217 bridge, inspect boot logs, then test the
   local page and Vercel page with matching, mismatched, malformed, and
   oversized inputs. Confirm successful slot switching and that the Web page no
   longer asks for the redundant password.

## Rollback Points

- Before boot selection, every failure calls `esp_ota_abort()` and returns an
  error while the current firmware keeps running.
- If the new image boots incorrectly, serial-flash the previous known-good
  firmware or upload it through OTA when the Web service remains available.
