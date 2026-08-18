# FlashDB History Storage Contract

## Scenario: GPS and BMS history

### 1. Scope / Trigger

Use this contract when changing the FlashDB component, one-second history sampling,
history HTTP APIs, or the Android history reader. Settings, pairing, Wi-Fi and
NimBLE state remain in NVS.

### 2. Signatures

- `esp_bms_flashdb_start_session(uint64_t *session_id)` starts one board-flash session.
- `esp_bms_flashdb_append_sample(uint64_t key, const esp_bms_flashdb_sample_t *)` appends one 32-byte row.
- `GET /api/history/sessions`, `/api/history/samples`, `/api/history/faults` are the Android contract.

### 3. Contracts

- S3 has `history0..2` and `faults` logical FAL partitions; classic ESP32 has one history slot and one fault slot.
- A new session uses an empty slot, otherwise cleans the oldest slot. Fault storage is never cleaned by sample rollover.
- Sample keys are monotonic `session_id << 32 | elapsed_seconds`; query responses are bounded by `limit <= 500`.
- A row carries version, validity flags, GPS E7 coordinates, BMS values, and six compressed temperatures.
- TF is intentionally not probed or mounted in these board profiles.

### 4. Validation & Error Matrix

- Missing FlashDB partition -> runtime logs a warning and real-time features continue.
- Missing session -> HTTP 404; invalid range or zero limit -> argument error.
- Full classic slot -> append stops and reports `capacity_reached`; it never overwrites the session head.
- Unavailable GPS/BMS fields -> clear validity bits, never fabricated zero-as-valid data.

### 5. Good / Base / Bad Cases

- Good: three S3 sessions coexist and a fourth removes only the oldest sample slot.
- Base: classic ESP32 retains the prefix that fits after reserving the fault slot.
- Bad: using `fdb_tsl_clean()` on the fault database during sample rollover.

### 6. Tests Required

- Build the FlashDB component for ESP32 and S3 partition tables.
- Assert the 32-byte payload size, bounded query count, independent fault writes, and oldest-slot selection.
- Android unit tests must parse empty, truncated, and paged history responses.

### 7. Wrong vs Correct

```c
/* Wrong: one global TSDB clean loses faults and all sessions. */
fdb_tsl_clean(&faults);

/* Correct: clean only the selected history slot before opening a session. */
fdb_tsl_clean(&history[oldest_slot]);
```
