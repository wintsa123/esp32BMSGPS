# Design

## Scope And Ownership

- `components/esp_bms_lvgl_ui/` owns system rows, subpage widgets, calibration targets, and user navigation.
- `components/esp_bms_lvgl_bridge/` owns XPT2046 coordinate correction, calibration session state, and calibration NVS data.
- `main/idf_main.c` remains the coordinator between queued UI actions and bridge operations.
- The managed XPT2046 component is not modified.

## System Settings Navigation

Keep the existing settings root and detail chrome. Extend the System detail with four subviews:

- Brightness: one native LVGL slider, current percentage, live apply, commit on release.
- Volume: the same native slider page, including existing volume feedback semantics.
- Level position: three direct choices using the existing UI-local position state.
- Screen calibration: full-screen four-target wizard with cancel and result states.

Rotation and language continue to execute from the System list. Restore defaults opens a confirmation overlay and queues the existing restore action only after confirmation.

The System subviews reuse the existing detail header and one-level back behavior. No generic settings framework or new component is introduced.

## Text Layout

Fix the shared setting-row text policy instead of patching individual strings:

- reserve a concrete text width after the trailing control/arrow slot;
- use vertical centering based on font line height;
- set a long mode for constrained title and subtitle labels;
- keep row height responsive to portrait/landscape and allow the detail container to scroll.

This fixes System text and sibling settings rows through one shared path.

## Touch Calibration Model

The XPT2046 driver already converts its 12-bit ADC values to a canonical pre-rotation coordinate space, then `esp_lcd_touch` applies mirror/swap flags. The bridge will keep this pipeline and add a small correction step in its existing custom touch read callback.

Calibration uses four targets near the display corners. Each UI sample contains:

- observed display-space point;
- expected target display-space point;
- target index / completion marker.

The bridge converts observed and expected points back through the current mirror/swap transform into the canonical pre-rotation space. From the left/right and top/bottom point pairs it derives linear X/Y bounds. It rejects samples when ordering is inverted, spans are too small, coordinates are outside plausible ranges, or the four-point set is incomplete.

Runtime mapping clamps the corrected coordinates to the canonical display range, then lets the existing rotation transform produce final display coordinates. One saved calibration therefore remains valid across all four display rotations.

## Calibration Session

1. UI opens the calibration wizard and queues `START_TOUCH_CALIBRATION`.
2. Main calls bridge begin; the bridge saves the active calibration and temporarily uses factory mapping.
3. On each target release, UI queues one sample action.
4. Main passes the sample to the bridge while holding the existing LVGL lock.
5. The final valid sample computes, applies, and saves the new calibration; invalid completion restores the previous calibration.
6. Main reports success/failure to the UI through a small public UI result function.
7. Cancel restores the previous calibration without writing NVS.

## Persistence And Reset

Store a versioned calibration blob in namespace `esp_bms` under one new key. The blob contains canonical X/Y bounds and a version field. On boot, the bridge validates the blob before applying it; missing or invalid data falls back to factory mapping.

Confirming Restore Defaults erases the calibration key and restores factory mapping immediately, in addition to the existing runtime reset.

## Compatibility

- Existing Setup AP, BMS, Bluetooth, quick panel, and Web flows are unchanged.
- Existing action values retain their numeric values; new actions append to the enum.
- The action-event ABI assertion is updated deliberately for the added calibration sample payload.
- Calibration remains opt-in and never blocks boot.
- Current display rotation, touch swap, and mirror defaults remain unchanged.

## Preview And Hardware Validation

- Extend the repository preview path to render System root, slider page, position page, and calibration wizard into `preview/`.
- Validate 320x240 and 240x320 layouts visually.
- Build, flash through the fixed RFC2217 bridge, complete calibration on hardware, reboot, and verify retained alignment plus all four rotations.

## Rollback

Reverting the UI, bridge, main coordinator, preview, and task files restores the old fixed coordinate mapping. Invalid or absent NVS calibration data is ignored, so firmware rollback does not require an NVS migration.
