# BMS BLE stability and protocol completeness

User requested comparison with lswlc33/maibms, then explicitly approved the proposed changes with “行，直接改”. This is execution approval for the prior reviewed scope.

## Requirements
- Recover failed/stalled discovery and CCCD subscription; reject stale callbacks and preserve existing shared BLE scan ownership.
- Choose GATT writes from actual properties, retaining acknowledged EM02 writes.
- Consume ANT new-protocol fragmented/concatenated/compound responses, validate every segment CRC, preserve old ANT support.
- Publish ANT read-only diagnostic fields (cells, MOS/balance state, SOH, power, balance bitmap and optional charge/runtime estimates) through existing status API; map supported safety conditions.
- Merge Bluebabe real-time and identity tables by field presence, preserving zero readings and absent values correctly.
- Provide explicit ANT default/A/B channel selection with a warning about screen/charger ownership; never silently probe occupied external channels.
- Resolve EM02 receive classification from response byte count; change byte order only if source evidence supports it. Retain CRC and model identity gate.

## Acceptance
- Meaningful C host regression tests for parsers, field merge and BLE failure/state behavior; existing host suite stays green.
- Appropriate ESP-IDF build, then one correct-profile RFC2217 flash attempt with actual outcome recorded.
- No parameter writes, calibration, firmware-update protocol, expanded Bluebabe pairing code or unrelated UI redesign.
- Preserve unrelated dirty files. No push. Commit needs a concrete batched plan per workflow.
