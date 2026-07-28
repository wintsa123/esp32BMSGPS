# LVGL Dashboard Layout

## Scope

Applies to native dashboard builders in
`components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` when an LVGL dashboard must
support an exact display size outside the 240x320 portrait fallback canvas.

## Native 320x480 Contract

- Use an explicit size predicate: `s_ui.width == 320 && s_ui.height == 480`.
- Build the native branch directly on the full page. Do not route it through
  `dashboard_viewport()`, whose portrait canvas is 240x320.
- Keep other resolutions on their existing fallback or native-landscape paths unless the request
  explicitly calls for a compact-layout adjustment; validate every adjusted resolution.
- Assign every live snapshot label and dynamic drawing object to its existing
  `s_ui` field so `set_controller_dashboard()` and `set_fireblade_dashboard()`
  continue to update it.

## Forbidden Layout Fix

Do not use `lv_obj_set_style_transform_scale`, transform zoom, or scale
animation to enlarge a portrait fallback. Set the target branch's coordinates,
sizes, and fonts directly instead.

## Validation

| Resolution | Expected result |
| --- | --- |
| 320x480 | Native portrait branch fills the page without clipping or large unused bands. |
| 240x320 | Existing portrait fallback remains unchanged unless explicitly adjusted and validated. |
| 480x320 | Existing native landscape or landscape fallback remains unchanged. |

Required checks:

- Build `simulator/build` and run the three headless resolutions above.
- Generate and inspect a 320x480 screenshot for each changed dashboard.
- Extend `esp_bms_lvgl_ui_simulator_snapshot_matches()` with object-presence
  assertions for new native widgets.
