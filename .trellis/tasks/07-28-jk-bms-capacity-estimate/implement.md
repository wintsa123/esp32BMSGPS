# Implementation Plan

1. Update the JK decoder and its host self-test for the 24/32-slot cumulative
   capacity field and official scaling.
2. Extend the existing runtime capacity identity policy from ANT to ANT/JK,
   project the result into the dashboard snapshot, and invalidate projection
   on type or binding changes.
3. Route valid JK telemetry through the existing estimate observer.
4. Render the disabled BMS settings row and rebuild it when the estimate
   changes.
5. Run formatter, JK and capacity self-tests, the full host self-test script,
   simulator checks, targeted firmware build, GitNexus change detection, and
   the project quality gate.

## Risk Controls

- `decode_jk02_cell_info` has HIGH GitNexus impact: retain all existing frame
  validation and cover both layouts in its self-test.
- Do not substitute JK's configured total capacity for its cumulative counter.
- Do not enable the estimate for Daly, Yanyang, or JBD.
