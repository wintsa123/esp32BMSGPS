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
- `page_transition_expand(esp_bms_lvgl_page_t page)`
- `page_transition_page_create(lv_obj_t *parent, esp_bms_lvgl_page_t page)`
- `move_to_page(esp_bms_lvgl_page_t page, bool animated)`

### 3. Contracts

- Create one hidden, transparent full-size placeholder per physical carousel
  slot (`BMS`, `仪表`, and, when enabled, `投屏`) as a normal snappable child
  of `s_ui.pages`. Its opaque `COLOR_DASHBOARD_BG` child card owns the title,
  1px `COLOR_SETTINGS_BORDER`, and 8px radius. Position every placeholder with
  `page_target_scroll_x()`; it must never be a floating screen overlay.
- At drag start, reveal those placeholders and hide `battery_page`,
  `gps_page`, and `cast_page`. The active card starts full size and shrinks to
  an 8px inset in 100ms; the other slot cards are already inset. The full-size
  wrappers preserve the parent scroll extent while the real pages are hidden,
  so LVGL can still drag and snap to every existing slot.
- During the drag, do not manually move labels. The parent carousel scroll
  moves each centered title naturally; a first/last-page drag cannot reveal a
  nonexistent or empty target slot.
- `LV_EVENT_SCROLL_END` expands the selected card for 100ms before restoring
  the real pages and flushing the deferred snapshot. A below-threshold return
  expands the original card. If a release arrives before the shrink has drawn,
  compact that active card before starting the expansion.
- LVGL stages position and size in style properties. Call
  `lv_obj_update_layout(card)` before reading animated-card geometry; otherwise
  a fast release observes stale full-screen coordinates and skips expansion.
- `move_to_page(..., false)` is synchronous programmatic navigation, not a
  drag. Keep `page_scroll_programmatic` set while it dispatches scroll events
  so `page_scroll_event_cb()` does not show placeholders or defer stable data.
- While the layer is visible, do not invalidate the dashboard viewport. Restore
  every hidden page, hide every placeholder, cancel card animation, and flush
  the deferred snapshot after the normal snap path or any early
  `finish_page_scroll_state()` cleanup.

### 4. Validation & Error Matrix

| Condition | Required response |
| --- | --- |
| A complex page remains visible during drag | Reject the change; it defeats the draw-cost reduction. |
| Hiding real pages shrinks `s_ui.pages` below its last target x | Reject the change; placeholders must retain the complete scroll range. |
| An edge drag displays an empty target title | Reject the change; use only the physical carousel slots. |
| A fast release restores the page without an expansion | Refresh the target card layout and expand from its inset geometry. |
| `set_page(..., false)` leaves stable data unavailable | Ignore its synchronous scroll events with `page_scroll_programmatic`. |
| Settings, lock, or an aborted drag leaves a placeholder visible | Reject the change; route cleanup through `page_transition_hide()`. |

### 5. Good / Base / Bad Cases

- Good: BMS-to-Speed drag changes the full black `BMS` page into an inset card,
  moves the `BMS` and `仪表` cards with the finger, then expands the selected
  card before the complete Speed page returns.
- Base: a below-threshold drag expands the original inset card before returning
  the original complete page.
- Bad: using a screen-level layer, because hiding the real children then
  removes the carousel's scroll geometry; reading `lv_obj_get_width()` before a
  layout refresh or caching a full page bitmap during a gesture is also
  forbidden.

### 6. Tests Required

- Run both headless LVGL orientations and assert full-card, inset-card, and
  expansion geometry after an explicit layout refresh; also assert titles,
  retained `lv_obj_get_scroll_right(s_ui.pages)`, real-page restoration, and
  the native-gesture/programmatic-page contract.
- Build the affected profile. Before hardware sign-off, inspect slow drag,
  fast release, cancel, snap, and first/last-page drags on the TFT.

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

#### Wrong

```c
page_transition_card_set_compact(card);
if (lv_obj_get_width(card) == s_ui.width) {
    page_transition_hide();
}
```

#### Correct

```c
page_transition_card_set_compact(card);
lv_obj_update_layout(card);
page_transition_card_animate(card, true);
```
