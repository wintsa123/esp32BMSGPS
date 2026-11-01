# Execution

1. Sync the existing remote EM02 commit by fast-forward; keep pre-existing dirty files.
2. Refresh GitNexus and run upstream impact before edits, report HIGH/CRITICAL; verify UNKNOWN using callers/registrations.
3. Implement ANT parser/diagnostic/header contracts and tests; independently implement transport recovery/properties and tests, plus Bluebabe partial projection/diagnostic API and tests. Coordinate shared-file function ownership.
4. Integrate explicit ANT channel configuration and evidence-backed EM02 corrections.
5. Run host selftests and firmware build for the saved current hardware profile, then RFC2217 flash attempt. Do not flash stale or wrong-profile images.
6. Full-scope trellis-check, fix findings, update spec, complete GitNexus change analysis.
7. Present reviewable task-only commit plan, preserving unrelated changes. No push. Archive only if required validation succeeds.
