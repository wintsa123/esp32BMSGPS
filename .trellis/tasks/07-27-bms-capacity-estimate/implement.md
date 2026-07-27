# BMS Real Capacity Estimate Implementation Plan

1. Extend ANT telemetry decoding.
   - Add the optional total-cycle mAh contract to the shared telemetry type.
   - Decode the verified new and old ANT offsets without changing polling or frame validation.
   - Extend the ANT protocol self-test with expected cycle values for both fixtures.

2. Add the estimator and persistence.
   - Create the smallest testable capacity-estimate state helper in `esp_bms_idf_runtime`.
   - Apply it from accepted ANT telemetry only, keyed to normalized bound MAC.
   - Load/save a versioned NVS blob only after estimate state changes, with retry behavior matching ride records.
   - Add an assert self-test covering delayed SOC correction, partial charge/discharge, direction reversal, and counter reset.

3. Project the result through the runtime HTTP status contract.
   - Emit the nullable mAh value and the three-state status.
   - Clear transient in-memory anchors on reconnect without discarding a valid persisted estimate for the same BMS.

4. Render one read-only value in the requested settings surfaces.
   - Add Chinese and English strings plus status formatting to `main/web/index.html`.
   - Extend the Vercel `Status` type and render the same value in its BMS settings section.

5. Verify.
   - Run the targeted protocol and estimator self-tests.
   - Run the relevant firmware build/analyze commands from the project hardware guidelines.
   - Run Vercel type/lint checks available in `vercel/package.json`.
   - Confirm `/api/status` state/value combinations and both language render paths.
   - Before committing, run GitNexus change detection as required by project instructions.

## Risk Points

- `esp_bms_idf_runtime.c` already has user changes. Keep the integration narrow and preserve those edits.
- A total-cycle counter reset must clear the previous estimate instead of silently mixing two BMS histories.
- Do not store a raw BLE frame or any credential in NVS.
