# Quality Guidelines

> Code quality standards for the embedded Web UI.

## Scope

The device-hosted Web UI lives at `main/web/index.html` and is embedded into the
ESP-IDF app image.

## Required Patterns

- Default UI language is Chinese; English is selectable from device settings.
- Keep the UI framework-free: plain HTML, CSS, and vanilla JavaScript.
- API calls must tolerate missing optional numeric fields by displaying `--`.
- Battery display prefers `local_battery_mv` and falls back to `pack_voltage_mv`.
- BMS candidate rows consume `mac`, optional `name`, and `rssi`.
- Clicking a BMS candidate may fill the MAC input; binding still posts the
  selected MAC explicitly.

## Forbidden Patterns

- Do not add framework dependencies, charting libraries, CDN calls, or remote
  assets to the embedded UI.
- Do not show fake BMS candidates when the API returns an empty list.
- Do not add a separate language prompt, login page, or modal for language
  selection; language stays in device settings.

## Validation Matrix

- Missing optional battery values -> display `--`.
- Fetch failure during setup AP startup -> keep the page usable and show Wi-Fi
  as setup.
- Empty BMS candidates -> render an empty candidate list, not stale devices.
- New JSON fields consumed by the UI -> update the C runtime response contract
  and validation plan.
