# Implementation Plan

- [ ] Capture `HEAD`, branch, `git status`, per-file hashes for relevant tracked/untracked firmware inputs, GitNexus index status, ESP-IDF/tool versions and dependency-lock hashes.
- [ ] Snapshot the current GPIO and partition authorities into a normalized baseline manifest without editing source symbols.
- [ ] Run existing host self-tests and record results.
- [ ] Build the current default ESP32 firmware in an isolated baseline build directory; save app/total size, component graph, map and binary hashes.
- [ ] Run existing simulator smoke/preview path and save reproducible visual evidence under `preview/` when supported.
- [ ] Use the LAN RFC2217 flash/monitor workflow to record boot and available peripheral checks.
- [ ] Add a baseline summary mapping every acceptance item to evidence and mark unavailable hardware honestly.
- [ ] Run `git diff --check` and GitNexus `detect-changes`; confirm only task evidence files were newly changed by this child.

## Validation Commands

```bash
./scripts/run-host-selftests.sh
./scripts/esp-idf-env.sh -B build/legacy-baseline build
./scripts/esp-idf-env.sh -B build/legacy-baseline size-components
git diff --check
node .gitnexus/run.cjs detect-changes --repo esp32BMSGPS
```

The RFC2217 command and monitor checks follow `.trellis/spec/backend/hardware-build-flash.md` and the project flash skill.
