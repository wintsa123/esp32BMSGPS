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

## Scenario: Blackout Carousel Transition

### 1. Scope / Trigger

- Apply when changing `page_scroll_event_cb()`, `page_transition_*()`, or the
  carousel's drag-time rendering strategy in `esp_bms_lvgl_ui.c`.

### 2. Signatures

- `page_transition_show(void)`
- `page_transition_hide(void)`
- `page_transition_page_create(lv_obj_t *parent, esp_bms_lvgl_page_t page)`

### 3. Contracts

- Create one hidden, opaque `COLOR_DASHBOARD_BG` placeholder per physical
  carousel slot (`BMS`, `仪表`, and, when enabled, `投屏`) as a normal
  snappable child of `s_ui.pages`. Position every placeholder with
  `page_target_scroll_x()`; it must never be a floating screen overlay.
- At drag start, reveal those placeholders and hide `battery_page`,
  `gps_page`, and `cast_page`. The placeholders preserve the parent scroll
  extent while the real pages are hidden, so LVGL can still drag and snap to
  every existing slot.
- During the drag, do not manually move labels. The parent carousel scroll
  moves each centered title naturally; a first/last-page drag cannot reveal a
  nonexistent or empty target slot.
- While the layer is visible, do not invalidate the dashboard viewport. Restore
  every hidden page, hide every placeholder, and flush the deferred snapshot
  after the normal snap path or any early `finish_page_scroll_state()` cleanup.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| A complex page remains visible during drag | Reject the change; it defeats the draw-cost reduction. |
| Hiding real pages shrinks `s_ui.pages` below its last target x | Reject the change; placeholders must retain the complete scroll range. |
| An edge drag displays an empty target title | Reject the change; use only the physical carousel slots. |
| Settings, lock, or an aborted drag leaves a placeholder visible | Reject the change; route cleanup through `page_transition_hide()`. |

### 5. Good / Base / Bad Cases

- Good: BMS-to-Speed drag moves the black `BMS` and `仪表` pages with the
  finger; on snap, the complete Speed page returns.
- Base: a below-threshold drag returns the original complete page.
- Bad: using a screen-level layer, because hiding the real children then
  removes the carousel's scroll geometry; caching a full page bitmap during a
  gesture is also forbidden.

### 6. Tests Required

- Run both headless LVGL orientations and assert the opaque placeholders,
  titles, retained `lv_obj_get_scroll_right(s_ui.pages)`, and real-page
  restoration in the native gesture smoke test.
- Build the affected profile. Before hardware sign-off, inspect slow drag,
  cancel, snap, and first/last-page drags on the TFT.

### 7. Wrong vs Correct

#### Wrong

```c
page_transition_create(screen);  // screen overlay loses carousel geometry
```

#### Correct

```c
page_transition_create(s_ui.pages);
if (!page_transition_active()) {
    invalidate_dashboard_viewport();
}
```
