# Fix legacy ESP32 main task stack overflow

## Goal

Restore a bootable display on the legacy ESP32-WROOM-32E by preventing the
main FreeRTOS task from overflowing before the first display frame.

## Confirmed Facts

- The remote device is an ESP32-D0WD-V3 with 4 MB Flash and no PSRAM.
- The boot log reports `main_stack_free=0B` and then `A stack overflow in task
  main` immediately after display settings load, causing a reboot loop.
- The legacy default (`config/sdkconfig/sdkconfig.defaults`) leaves
  `CONFIG_ESP_MAIN_TASK_STACK_SIZE` at ESP-IDF's 3584-byte default.
- Profile generation uses the MCU-specific defaults when present and otherwise
  uses `config/sdkconfig/sdkconfig.defaults`; therefore the legacy ESP32 uses
  this base defaults file.
- After the stack fix, the boot log completes display, touch, and LVGL adapter
  initialization but stops before `display path initialized`. The bridge start
  wrapper has no callers, so the display task blocks waiting for an adapter
  lock that has not been made runnable.

## Requirements

- Set the legacy ESP32 main task stack to 8192 bytes in its defaults source.
- Start the initialized LVGL adapter before the display service takes its
  adapter lock and creates the first UI frame.
- Keep the existing ESP32-S3 defaults and display/runtime code unchanged.
- Build a legacy ESP32 profile and flash it through the configured RFC2217
  bridge at 115200 baud.
- Verify a cold boot reaches display initialization without a main-task stack
  overflow or reboot loop.

## Acceptance Criteria

- [ ] The legacy defaults explicitly set `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`.
- [ ] The generated legacy profile/sdkconfig resolves the value to 8192.
- [ ] The display service starts the LVGL adapter before acquiring its startup
      lock.
- [ ] The legacy ESP32 firmware build succeeds.
- [ ] Remote flash succeeds through `rfc2217://192.168.2.10:4000?ign_set_control`.
- [ ] Startup log reports a nonzero main-stack high-water mark and has no
      `A stack overflow in task main` or restart during the observation window.
- [ ] Startup log reaches `display path initialized`.

## Out Of Scope

- Changing the existing ESP32-S3 configuration.
- Refactoring display startup or runtime initialization beyond the missing
  adapter-start call.
- Altering board GPIO, partitions, or NVS contents.
