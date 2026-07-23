# Implementation Plan

1. Add and propagate `ESP_BMS_FEATURE_CAST` from the generated profile through CMake definitions.
2. Conditionalize cast UI, route registration, and main-loop cast page transitions.
3. Make carousel page position and gesture resolution derive from enabled page availability.
4. Add no-cast configurator assertions and simulator coverage for both page-availability matrices.
5. Run focused self-tests, simulator tests, a no-cast profile build, the full project check, then flash the affected firmware once through RFC2217.

## Verification

- `tests/configurator_selftest.sh`
- Simulator capability-matrix tests for cast disabled/enabled
- `./start.sh build-local` with a legacy profile that omits `cast`
- Project quality checks and remote flash validation
