# Fix WiFi SocketSet startup panic

## Goal

Fix the target firmware startup panic where Wi-Fi setup AP initialization adds
more embassy-net sockets than the configured `StackResources` capacity, so the
main loop can continue into TFT/touch polling after Wi-Fi starts.

## Requirements

- Keep the fix scoped to the Wi-Fi target runtime socket capacity/startup path.
- Preserve the current setup AP radio, DHCP server, and HTTP server behavior.
- Do not change touch handling, TFT orientation, or TFT language rendering in
  this task.
- Keep target memory use bounded and explicit; avoid broad refactors.

## Acceptance Criteria

- [ ] Booting with setup AP enabled no longer panics with
      `adding a socket to a full SocketSet`.
- [ ] Serial logs can proceed past Wi-Fi startup into the main loop where touch
      diagnostics are able to run.
- [ ] Host library checks still pass.
- [ ] ESP target check/clippy/build still pass.

## Notes

- User-provided backtrace points to `wireless_wifi.rs` socket allocation during
  `apply_target_wifi_config` before touch polling begins.
