# LVGL Carousel Drag Diagnostics Contract

## Scenario: Carousel Drag Invalidation

### 1. Scope / Trigger

- Apply this contract when changing dashboard carousel scrolling, the
  `ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE` Kconfig option, its sdkconfig defaults,
  simulator configuration, or `scripts/esp-idf-drag-diag.sh`.
- Full viewport invalidation redraws scaled BMS and Fireblade dashboards on each
  `LV_EVENT_SCROLL`; it is a diagnosis tool, not the normal performance path.

### 2. Signatures

- Kconfig option: `CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE`.
- Diagnostic command:
  `scripts/esp-idf-drag-diag.sh [--full-invalidate|--no-full-invalidate] [--double-buffer] <idf.py args>`.
- Full-redraw overlay:
  `config/sdkconfig/sdkconfig.defaults.dragdiag-full-invalidate`.

### 3. Contracts

- Normal ESP32, ESP32-S3, simulator, and diagnostic images leave full
  invalidation disabled, so LVGL performs native partial invalidation.
- `--full-invalidate` is the only command that adds the full-redraw overlay.
  `--no-full-invalidate` remains accepted and has the same result as default.
- Do not change dashboard scaling, page mapping, gesture thresholds, snapshot
  timing, or double-buffer policy while comparing invalidation paths.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| A normal default enables full invalidation | Reject the change; unset the option in every normal default. |
| Default diagnostic command adds a full-redraw overlay | Reject the change; preserve the local-invalidation default. |
| `--full-invalidate` does not add its overlay | Reject the script change; A/B comparison is unavailable. |
| Legacy `--no-full-invalidate` fails or changes behavior | Reject the script change; it remains a compatibility alias. |
| Hardware shows artifacts with local invalidation | Capture the case and compare an explicit full-invalidate image before changing draw code. |

### 5. Good / Base / Bad Cases

- Good: build a normal image, then use `--full-invalidate` only to compare an
  observed artifact or regression.
- Base: `--no-full-invalidate build` produces the same sdkconfig-default list
  as `build`.
- Bad: make full invalidation the default to hide a localized drawing issue, or
  rewrite scaled dashboards before an A/B comparison proves scaling is causal.

### 6. Tests Required

- Run `git diff --check` and `bash -n scripts/esp-idf-drag-diag.sh`.
- Run `./tests/configurator_selftest.sh`; assert default, explicit full, and
  compatibility command lines select the expected overlays.
- Run horizontal and vertical Fireblade+BMS simulator smoke tests.
- Build the affected ESP-IDF target, then run
  `node .gitnexus/run.cjs detect-changes -r esp32BMSGPS`.
- Flash the affected image and manually check slow drag, fling, snap, and
  programmatic BMS-to-Fireblade switching for artifacts and WDT resets.

### 7. Wrong vs Correct

#### Wrong

```bash
scripts/esp-idf-drag-diag.sh build  # silently forces full viewport redraw
```

#### Correct

```bash
scripts/esp-idf-drag-diag.sh build
scripts/esp-idf-drag-diag.sh --full-invalidate build
```
