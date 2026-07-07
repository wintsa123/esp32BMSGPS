# Firmware Feature Roadmap Design

## Architecture

The roadmap stays on the pure ESP-IDF architecture:

- `main/idf_main.c` remains boot and loop orchestration only.
- `esp_bms_idf_runtime` owns runtime state, NVS persistence, GPS, BMS, Wi-Fi,
  HTTP API, OTA orchestration, and snapshot production.
- `esp_bms_lvgl_ui` owns TFT layout, pages, actions, and rendering from the
  runtime snapshot.
- `esp_bms_lvgl_bridge` owns display, touch, rotation, and backlight hardware.
- `main/web/index.html` remains the single local Web UI asset for detailed
  settings, lists, and Chinese explanations.

If BMS or OTA logic grows further, split focused components later, but keep the
first roadmap implementation small enough to preserve boot stability.

## UI Ownership

### TFT

TFT is for glanceable, in-vehicle information and compact actions:

- BMS homepage.
- GPS speed and 0-100 timing summary.
- Setup AP QR/status.
- Compact settings page with icon-like rows and dropdown-style controls where
  practical.
- Short ASCII status/fault codes only.

TFT should not host long candidate lists, long Chinese descriptions, token/URL
entry, OTA manifest details, or a detailed map.

### Web UI

Web UI owns complex workflows:

- BMS scan candidate list, bind/rebind/unbind details.
- Full BMS protection and warning list with Chinese explanations.
- Wi-Fi setup and password entry.
- OTA check/start details.
- Detailed map view or route/location configuration if map support is added.

The Web UI remains Chinese-first, with English selectable from device settings.

## BMS Homepage

The BMS homepage is the first implementation priority. It should follow the
user's reference image while fitting the actual 240 x 320 TFT.

Required layout:

- Large SOC tile:
  - Percent as the primary visual value.
  - Ah capacity below the percent, e.g. `59.44/80.00Ah`.
- Right-side voltage/current tile:
  - Pack voltage.
  - Pack current.
- Cell voltage stats tile:
  - `MAX`, `MIN`, `DIFF`, `AVG`.
- Lower information area:
  - Shows highest-priority BMS protection/warning/info state.
  - Uses fixed ASCII codes.
  - No free-form Chinese text on TFT in the first version.
- Temperature area:
  - `T1`, `T2`, `T3`, `T4`, `BAL`, `MOS` where data exists.

The previous generated concept
`.trellis/tasks/07-07-firmware-feature-roadmap/assets/image_1783359124_0.png`
is directionally useful, but it is still a large-screen style reference. The
actual LVGL layout should be denser and avoid text overflow at 240 x 320.

The homepage must not include `ROTATE`, `BMS CONNECT`, or dropdown settings
controls. Those belong on the settings page.

## BMS State Model

Current `bms_error_text` is too generic. The next BMS child task should separate
three concepts:

- `protection`: active protection states reported by ANT BMS.
- `warning`: active warning states reported by ANT BMS.
- `info`: connection/runtime state, such as scan, connecting, online, no
  service, no characteristic, no CCCD, RX error, poll error.

Display priority:

1. Protection.
2. Warning.
3. Info.
4. `OK`.

TFT should show only the highest-priority compact code, optionally with one
secondary line if space allows:

```text
PROT OVP
WARN CELL
INFO BMS OK
```

Web UI should expose the full active protection and warning lists and map them
to Chinese explanations.

The exact ANT BMS protection/warning bit layout must be verified against real
frames, existing known-good behavior, or protocol documentation before parsing
is treated as complete. Unknown bits should be preserved/logged as hex codes
instead of discarded.

## Settings UI

The settings page should move away from plain stacked action labels where space
allows:

- Use icon-like ASCII or LVGL symbols for categories.
- Use dropdown-style rows for display rotation and similar option sets.
- Keep language as `ZH` / `EN` on TFT until Chinese fonts are approved.
- Include a compact protection-board connection entry:
  - Current BMS state.
  - Trigger scan/reconnect/rebind.
  - Refer the user to Web UI for full candidate selection if needed.

Web UI remains the authoritative place for candidate selection, long forms, and
Chinese copy.

## GPS, 0-100, And Map

GPS-dependent features must wait until the UART0 console/GPS conflict is
resolved. Options include:

- Move GPS to a non-console UART/pin pair if hardware allows.
- Keep UART0 GPS for product firmware but use a separate debug build/transport
  for logs.
- Add a build-time switch for GPS-vs-console, with field firmware favoring GPS
  and debug firmware favoring logs.

0-100 timing needs explicit rules:

- Requires fresh valid GPS fix.
- Starts when speed rises above a small threshold.
- Stops when speed reaches 100 km/h or configured equivalent.
- Cancels or invalidates when GPS fix is stale/lost.
- Stores last/best result only when data is valid.

Map functionality should not be assumed to be a full offline TFT map. Given
4 MB flash, no PSRAM, and current font constraints, the safer default is:

- Web/phone-hosted map for detailed view.
- TFT summary for speed/timing/heading/waypoint-like data.

## Password And OTA

Password and OTA are intentionally last.

Password work should preserve:

- Setup AP SSID policy.
- Eight-digit random setup AP password policy.
- No secret logging.
- QR and displayed password matching active SoftAP config.

OTA work should use ESP-IDF OTA APIs and verify:

- Manifest validity.
- Image size against OTA slot.
- Download integrity.
- Partition switch.
- First-boot validity marking and rollback behavior.

## Rollout

Each child task must preserve:

- Boot to display.
- Backlight enabled.
- Setup AP recoverability.
- `./scripts/esp-idf-env.sh build` success.
- No fabricated BMS/GPS/OTA data.

If a child destabilizes boot, disable that subsystem behind a narrow runtime or
build-time guard rather than weakening the display/setup AP baseline.
