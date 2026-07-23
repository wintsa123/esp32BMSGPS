# Design: 投屏裁剪与轮播导航

## Boundaries

- `start.sh` owns module selection and generated build-profile feature flags.
- CMake component definitions own propagation of profile feature flags to C compilation units.
- Network, runtime, and LVGL UI own the cast route, cast session behavior, and carousel page availability.
- The simulator and configurator self-test own regression coverage.

## Design

1. Derive `ESP_BMS_FEATURE_CAST` from `CFG[MODULES]` alongside existing module flags, defaulting to enabled only for non-profile builds that preserve legacy behavior.
2. Propagate the flag to every component that conditionally compiles cast behavior. Guard the `/cast` route, cast runtime entry points, cast page construction/updates, and the main-loop cast-driven page transition.
3. Replace the fixed page-index assumption with availability-aware mapping: battery is always available, speed is conditional, cast is conditional. Scroll position, gesture limits, and selected-page decoding use the same mapping.
4. Add configurator coverage for explicit no-cast generation and simulator coverage for carousel navigation with cast disabled and enabled.

## Compatibility

- Profiles which select `cast` retain the existing QR, WebSocket endpoint, and active-session page behavior.
- Profiles that do not select `cast` remove the page and route rather than rendering a disabled placeholder.
- Non-profile/default builds retain cast enabled to avoid silently changing the existing developer build.

## Risks and Rollback

- The carousel helper affects touch navigation. Regression tests must cover speed-page available/unavailable with cast both enabled and disabled.
- The change is compile-time only; rollback is a source revert and a fresh firmware build.
