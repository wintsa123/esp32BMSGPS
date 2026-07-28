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

## Controller S3 Contract

- In `create_controller_dashboard()`, use separate exact branches for
  `480x320` and `320x480`; do not stretch either `320x240` or `240x320` layout.
- Keep live speed, gear, power, RPM, controller-temperature and motor-temperature
  labels on their existing `s_ui.controller_*` fields. Static titles and units
  must not enter the snapshot update path.
- The simulator smoke check must recognize both native controller resolutions;
  run the controller dashboard headless at `480x320`, `320x480`, `320x240`, and
  `240x320` after changing this layout.

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

## Startup Gauge Sweep

`speed_dashboard_draw_event_cb()` renders the S1000RR energy band from
`s_ui.last_snapshot`. A boot sweep must store its generated demo snapshot
before invalidating the dashboard; updating labels alone leaves the band at
the real pre-boot speed.

```c
s_ui.last_snapshot = demo;
speed_dashboard_style_apply(&s_ui.last_snapshot);
set_gps_dashboard(&s_ui.last_snapshot);
```

The gauge overlay is transparent and exists only to hold boot state. Do not
add status or percentage labels to it, since they obscure both dashboard
themes. Simulator coverage must check the selected theme page, the demo speed
at each sweep phase, and the absence of those HUD labels.
