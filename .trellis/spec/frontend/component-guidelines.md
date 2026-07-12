# Component Guidelines

> How components are built in this project.

---

## Overview

<!--
Document your project's component conventions here.

Questions to answer:
- What component patterns do you use?
- How are props defined?
- How do you handle composition?
- What accessibility standards apply?
-->

(To be filled by the team)

---

## Component Structure

<!-- Standard structure of a component file -->

(To be filled by the team)

---

## Props Conventions

<!-- How props should be defined and typed -->

(To be filled by the team)

---

## Styling Patterns

### LVGL Settings Detail Rows

- Settings root and secondary list pages use a black page background with one `COLOR_SETTINGS_LIST` card inset by 12px. Rows inside that card have no individual corner radius or gaps; use bottom borders as separators and let the parent card clip the outer corners.
- Secondary settings rows use `settings_zh_13` for the title and `settings_zh_10` for `desc` / subtitle text; do not go below 10px for CJK subtitles because smaller bitmap fonts clip or lose strokes.
- English and Chinese subtitles must render at the same visual size; preview fallback ASCII labels must be scaled down when the preview runtime lacks the exact font size.
- Title + desc must be vertically centered as one text block inside the row; do not use fixed y offsets that only work for one row height.
- Label boxes must be taller than the font line height so CJK and fallback ASCII glyphs do not clip at the bottom edge.
- Right-side affordances must be centered in their slot. Binary actions use a pill switch; navigational or one-shot actions use the arrow.
- Every enabled switch track and border use the shared green `COLOR_SWITCH_ACTIVE` (`#34C759`); disabled switches retain `COLOR_SETTINGS_BORDER`, and switch thumbs remain white when enabled.
- The Bluetooth settings switch and quick-panel Bluetooth tile represent discoverability / advertising, not whether the Bluetooth stack is enabled. Bluetooth is on by default; the UI on/active state must follow `BLUETOOTH_ADVERTISING` only.
- LVGL modal lists on the black settings background must use distinct local colors for modal, list, and row layers plus a visible border; do not stack `COLOR_SETTINGS_CARD` on every level. ASCII-only modal titles, choices, loading text, and BLE candidate metadata should use an ASCII-capable font, and opening the modal should reset settings swipe tracking/consumed state so option clicks are not swallowed by the previous gesture.
- Settings choices that own a scan/result list or a persistent one-of-many selection use full detail subviews, not modal overlays. Store an explicit subview enum in `s_ui`, render into `settings_detail`, and make header-back plus edge-back pop one level. Snapshot refreshes may rebuild the active subview but must not queue its entry action again or jump back to the parent page.
- BLE candidate rows open a confirmation overlay before queuing a bind. Copy the candidate name and MAC into stable UI storage before opening it; never retain a pointer to a snapshot row across refreshes, and never show an anonymous candidate's MAC as its display name.
- Repeated BLE advertisements may update cached RSSI without rebuilding the candidate list. Rebuild only for new candidates, identity/name changes, or scan-state changes; deleting a pressed row before `LV_EVENT_CLICKED` makes the first tap disappear.
- BMS and controller BLE selection use one source-parameterized list and confirmation renderer. The source adapter owns candidate/count/bound-name selection plus scan/bind actions; do not add controller candidates beneath the controller root card or copy a second page implementation.
- A controller settings subview is explicit (`ROOT`, `BLE_LIST`, `TIRE_EDIT`, `RATIO_EDIT`). Header-back and left-edge back pop one level; snapshot refreshes may update root/BLE views but must never rebuild an active roller editor and destroy its draft.
- Controller tire/ratio rows remain visible when the controller page is enabled. Offline rows and controller-synchronized rows use `LV_STATE_DISABLED`; only online rows without controller parameters navigate to an editor.
- Bounded numeric roller editors hold a local draft and queue one absolute-value action only from the green confirmation button. Back/cancel queues no action, and refresh/scan actions must not be treated as settings changes that write NVS.
- A confirmed bind uses the existing toast objects for a persistent rotating `LV_SYMBOL_LOOP` plus `连接...`. Stop the animation on terminal connection states and replace it with `绑定成功` only on the offline-to-online snapshot transition.
- FarDriver controller telemetry subscription and notification parsing stay active after the controller GATT path reaches online, independent of the currently visible dashboard page. The stable page may gate the 2-second active gather request, but must not unsubscribe or discard notifications; otherwise the controller carousel opens with no cached telemetry.
- Regenerate `settings_zh_10/13/16.c` from every Han character actually present in `esp_bms_lvgl_ui.c`; do not maintain an ad hoc glyph tail. Before build, compare the generated `--symbols` list with UI literals and require zero missing glyphs.

### LVGL Fixed QR Lifecycle

- Keep both the QR widget pointer and its visual panel pointer. Hiding only the QR child leaves a misleading white card.
- For fixed Setup AP credentials, call `lv_qrcode_update()` only on the first non-empty payload in the cached page object's lifetime. Record separate `encode_attempted` and `qr_ready` flags; routine snapshots and hotspot toggles only change panel visibility.
- Center the QR widget with `lv_obj_center()` after setting size, colors, and quiet-zone policy. Do not rely on a duplicated hard-coded child offset.
- Visibility contract: `panel_visible = SETUP_AP_ENABLED && qr_ready`. An empty payload or encode failure keeps the panel hidden and must not cause repeated encode attempts on every snapshot.

### LVGL BMS Dashboard Visuals

- Keep dashboard styling local to dashboard helpers and objects. Do not change the shared `panel()` palette or geometry to restyle the homepage because settings, quick-panel, and GPS surfaces reuse it.
- The SOC tile uses a static percentage fill. Do not create a wave line or infinite LVGL animation; charging and low-SOC states are expressed by fill color while fill size remains the actual SOC percentage.
- Do not show a separate `SOC` caption in the SOC tile. Stack percentage, a large battery outline, and Ah capacity vertically; the battery's inner width must map to the same validated SOC value and clear when SOC is invalid.
- The portrait SOC capacity label is 100px wide and the firmware only enables Montserrat 14/24. Format it as one-decimal `remaining/totalAh` (for example `59.4/80.0Ah`) so it fits without enabling another font or clipping.
- Keep voltage/current and cell-stat separators at 1px. Temperature columns use fixed small thermometer primitives so all six `T1`-`T4`/`BAL`/`MOS` columns remain stable in 240x320 and 320x240 layouts.
- Render dashboard previews through `preview/lvgl_render_compat.py`; the local LVGL 9 MicroPython binding initializes through module `__init__()` rather than `lv.init()`.

### LVGL Controller Dashboard Visuals

- Use `preview/controller_display_v2_dual_hero.png` as the controller dashboard visual contract. Both orientations require an outer frame, separately bordered speed and gear regions, and a blue vertical-gradient gear region. The auxiliary area is one four-column row in landscape and a 2x2 grid in portrait.
- Keep speed, gear, auxiliary values, headings, and units in separate labels. Do not encode a whole auxiliary column as a multi-line string such as `"CTRL\n52C"`; separate labels are required to preserve the design's font size, color, and vertical-spacing hierarchy.
- Place each auxiliary unit immediately to the right of its value on the same row (`8.4 kW`, `3780 RPM`, `52 C`). Use built-in `lv_font_montserrat_14` for auxiliary ASCII instead of a generated sparse font; the built-in font avoids target-only glyph corruption. Keep `CTRL` and `MOTOR` as white headings at the top-left of their cells, with green temperature values and units on the row below.
- Dynamic controller labels own persistent buffers and use `lv_label_set_text_static()`. Online/offline transitions replace only the numeric text (`"42.6"` -> `"-"`); they must not create/delete widgets or move static headings and units.
- The 320x240 and 240x320 layouts use explicit fixed geometry because they target one physical TFT in two orientations. Landscape uses one horizontal four-column stats row; portrait uses a 2x2 grid with power/RPM above controller/motor temperature.
- Large numeric fonts should contain only the glyphs used by telemetry (`-.0123456789`) to control flash use. Generate component-local LVGL fonts with `--lv-include lvgl.h`; the generator default `lvgl/lvgl.h` does not resolve through this component's ESP-IDF include path.
- The desktop LVGL runtime may lack the custom component font. A preview may scale an available Montserrat font to validate geometry, but the checked-in Pillow reference must use the same Montserrat TTF and native target size to validate crisp glyph metrics.
- The current ST7789 hardware requires `LCD_RGB_ELEMENT_ORDER_RGB` in `esp_bms_lvgl_bridge_init()`. Setting BGR flips red and blue on the physical panel, turning the controller gear panel's intended blue into orange even though desktop previews remain blue.
- `LV_OBJ_FLAG_SCROLL_ONE` is not the complete carousel contract. Use the scroll container's direct `LV_EVENT_SCROLL_BEGIN` / `LV_EVENT_SCROLL` / `LV_EVENT_SCROLL_THROW_BEGIN` / `LV_EVENT_SCROLL_END` chain so a child widget becoming the original touch target cannot hide the gesture. Freeze the actual displacement at `LV_EVENT_SCROLL_THROW_BEGIN`; a displacement of one-fifth page width (64px landscape, 48px portrait) selects the adjacent page, while a shorter drag returns to the stable page. This threshold sits above the observed sub-40px touch-jitter cluster and covers about 82% of historical multi-sample horizontal swipes. Every gesture is limited to `-1/0/+1` page, while programmatic `move_to_page()` remains unrestricted because it has no throw-begin event.
- Validate every fixed-width label against the native font's actual glyph width. In particular, Montserrat 24 `km/h` is about 65px wide; its label needs extra width and right-frame clearance in both orientations.

### LVGL Settings Navigation

- The settings root and all settings detail pages reuse one persistent top navigation object. The root title is `设置` and its back action returns to the dashboard; detail titles come from `SETTINGS_OPTIONS` and return to the settings root.
- LVGL event `user_data` must not retain pointers to function-local row/config structs. Pack small action/view values into `uintptr_t`, or store them in UI state whose lifetime covers the widget.
- Vertical navigation collapse uses one offset animation. Apply the same offset to the header `y`, the root/detail scroll-container top padding, and the left-edge capture zone so content fills the released space without a blank strip.
- Changing a scroll container's padding during that animation can make LVGL recalculate `scroll_y` and emit scroll events. Keep the navigation layout guard active for the whole animation, force the affected layout while guarded, refresh the scroll anchor from the active container, and release the guard only from the animation completion callback. Reset the guard when replacing or cancelling the animation; otherwise layout-generated events can reopen the header and cover the list bottom.
- Browsing down past `SETTINGS_NAV_SCROLL_THRESHOLD` hides the bar; browsing back up or reaching scroll position `0` shows it. Direct vertical drags use the same threshold so pages whose content fits the viewport still behave consistently.
- A recognized vertical navigation drag must set `SETTINGS_SWIPE_CONSUMED` to prevent the release from triggering the row under the finger. Horizontal left-edge back tracking remains dominant when horizontal movement is larger.
- Dynamic settings cards must calculate their visible row count before calling `settings_list_card()`. Create the card at its final height and place rows with a compact `visible_index`; do not create a maximum-height card and then hide rows or shrink it after child creation, because LVGL can retain stale scroll geometry or visible blank slots.

### LVGL Full-Screen Interaction Guards

- A locked or modal dashboard state that must block every underlying control uses one transparent, full-screen clickable object above `pages`, `quick_pull_zone`, and the quick panel. Do not try to add lock checks independently to every dashboard child; missing one event path reintroduces touch-through.
- The guard owns the complete pointer contract. If locked browsing still permits page changes, recognize that horizontal gesture on the guard and call the existing page-navigation helper; keep taps, unlock drags, and dashboard swipes mutually exclusive by distance thresholds.
- A temporary unlock control uses an LVGL one-shot timer. Cancel and null the timer on unlock and before deleting/rebuilding the root object; in the timeout callback, null the stored pointer before hiding/resetting widgets because LVGL deletes a one-repeat timer after the callback returns.
- Dashboard creation or rebuild code can move `quick_pull_zone` back to the foreground. After restoring a locked state, explicitly hide the pull zone and move the guard to the foreground again.
- Touch-friendly slider targets use a visible track of at least 52px, a knob of at least 44px, and `lv_obj_set_ext_click_area()` where space allows. Require the initial press to land on or near the starting knob and require release beyond a defined completion threshold.

```c
static void interaction_guard_reapply(void)
{
    set_obj_hidden(s_ui.quick_pull_zone, true);
    set_obj_hidden(s_ui.interaction_guard, false);
    lv_obj_move_foreground(s_ui.interaction_guard);
}

static void interaction_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    s_ui.interaction_timer = NULL;
    interaction_prompt_hide();
}
```

Validation must cover: tap versus horizontal swipe classification, below-threshold release, timeout while idle and while dragging, unlock cleanup, root rebuild while locked, and restoration of the quick-pull gesture after unlock.

---

## Accessibility

<!-- A11y requirements and patterns -->

(To be filled by the team)

---

## Common Mistakes

<!-- Component-related mistakes your team has made -->

(To be filled by the team)
