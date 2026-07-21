# Technical Design

## Baseline Artifact

Store versioned metadata and human-readable summaries under this task directory; store large/generated build logs, maps, binaries and preview images under an ignored `artifacts/` or root `preview/` path. The manifest records paths plus SHA-256 so evidence is traceable without committing large binaries.

The baseline has three evidence levels:

1. **Host/build**: source identity, dependency/tool versions, normalized sdkconfig and partitions, component graph, host tests, app size, map and image hashes.
2. **Simulator**: reproducible commands and screenshots for existing supported views; screenshots remain under root `preview/`.
3. **Hardware**: RFC2217 flash/monitor transcript with per-capability pass/unverified status. A missing BMS/GPS peripheral is not converted into a pass.

## Compatibility Comparison

The final legacy comparison checks structured authorities first (GPIO catalog, partition CSV, generated sdkconfig, route/settings capability lists), then build artifacts (component graph, symbols, resources, size), and finally simulator/hardware behavior. Exact binary equality is not required after modularization; contract equality is.

## Dirty Worktree Handling

The baseline identifies both `HEAD` and a sorted hash list of tracked/untracked inputs. Existing user modifications are treated as the product state to preserve. No cleanup, reset, checkout, or broad formatting is allowed.
