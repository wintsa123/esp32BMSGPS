# FlashDB 2.2.0 Research

Source: `armink/FlashDB` tag `2.2.0`, commit `65a87640bf816effc84ca161f5254878677232b8`.

- TSDB exposes `fdb_tsl_append_with_ts`, forward/reverse iteration, time-range iteration, bounded count, status updates, `fdb_tsl_to_blob`, and `fdb_tsl_clean`.
- TSDB rollover is sector based. When the current sector is full and rollover is enabled, the next sector is formatted and the oldest sector advances. `fdb_tsl_clean` formats the entire database; there is no public API to delete an arbitrary session range.
- `FDB_USING_TIMESTAMP_64BIT` changes `fdb_time_t` to `int64_t`; this is required for a persisted session generation plus elapsed seconds.
- `FDB_TSDB_CTRL_SET_SEC_SIZE`, `FDB_TSDB_CTRL_SET_MAX_SIZE`, and `FDB_TSDB_CTRL_SET_NOT_FORMAT` must be applied before initialization; rollover is configured after initialization.
- The ESP32 example uses an exclusive custom `esp_partition` and a 4 KiB erase block. The upstream repository has no ESP-IDF component manifest, so the project must vendor or pin the upstream source rather than assume an IDF Registry package.

Design consequence: use one logical FAL partition per session slot and clean only the selected whole slot before opening a new session. Keep the fault-event partition separate so sample retention cannot erase faults.
