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

- Secondary settings rows use `settings_zh_13` for the title and `settings_zh_10` for `desc` / subtitle text; do not go below 10px for CJK subtitles because smaller bitmap fonts clip or lose strokes.
- English and Chinese subtitles must render at the same visual size; preview fallback ASCII labels must be scaled down when the preview runtime lacks the exact font size.
- Title + desc must be vertically centered as one text block inside the row; do not use fixed y offsets that only work for one row height.
- Label boxes must be taller than the font line height so CJK and fallback ASCII glyphs do not clip at the bottom edge.
- Right-side affordances must be centered in their slot. Binary actions use a pill switch; navigational or one-shot actions use the arrow.
- Every enabled switch track and border use the shared green `COLOR_SWITCH_ACTIVE` (`#34C759`); disabled switches retain `COLOR_SETTINGS_BORDER`, and switch thumbs remain white when enabled.
- The Bluetooth settings switch and quick-panel Bluetooth tile represent discoverability / advertising, not whether the Bluetooth stack is enabled. Bluetooth is on by default; the UI on/active state must follow `BLUETOOTH_ADVERTISING` only.
- LVGL modal lists on the black settings background must use distinct local colors for modal, list, and row layers plus a visible border; do not stack `COLOR_SETTINGS_CARD` on every level. ASCII-only modal titles, choices, loading text, and BLE candidate metadata should use an ASCII-capable font, and opening the modal should reset settings swipe tracking/consumed state so option clicks are not swallowed by the previous gesture.

### LVGL Settings Navigation

- The settings root and all settings detail pages reuse one persistent top navigation object. The root title is `设置` and its back action returns to the dashboard; detail titles come from `SETTINGS_OPTIONS` and return to the settings root.
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
