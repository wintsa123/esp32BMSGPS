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
- A confirmed bind uses the existing toast objects for a persistent rotating `LV_SYMBOL_LOOP` plus `连接...`. Stop the animation on terminal connection states and replace it with `绑定成功` only on the offline-to-online snapshot transition.

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

### LVGL Settings Navigation

- The settings root and all settings detail pages reuse one persistent top navigation object. The root title is `设置` and its back action returns to the dashboard; detail titles come from `SETTINGS_OPTIONS` and return to the settings root.
- LVGL event `user_data` must not retain pointers to function-local row/config structs. Pack small action/view values into `uintptr_t`, or store them in UI state whose lifetime covers the widget.
- Vertical navigation collapse uses one offset animation. Apply the same offset to the header `y`, the root/detail scroll-container top padding, and the left-edge capture zone so content fills the released space without a blank strip.
- Browsing down past `SETTINGS_NAV_SCROLL_THRESHOLD` hides the bar; browsing back up or reaching scroll position `0` shows it. Direct vertical drags use the same threshold so pages whose content fits the viewport still behave consistently.
- A recognized vertical navigation drag must set `SETTINGS_SWIPE_CONSUMED` to prevent the release from triggering the row under the finger. Horizontal left-edge back tracking remains dominant when horizontal movement is larger.

---

## Accessibility

<!-- A11y requirements and patterns -->

(To be filled by the team)

---

## Common Mistakes

<!-- Component-related mistakes your team has made -->

(To be filled by the team)
