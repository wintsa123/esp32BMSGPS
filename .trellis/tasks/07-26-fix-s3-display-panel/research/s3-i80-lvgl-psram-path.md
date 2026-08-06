# Research: S3 I80 LVGL PSRAM Path

- Query: Compare the vendor 11_touch sample with the current ESP32-S3 ST7796U I80 path, focusing on timing, DMA, RGB565 byte order, reset/backlight, PSRAM buffers, and a stale LCD after application boot.
- Scope: mixed
- Date: 2026-08-06

## Findings

### Vendor versus current I80 setup

| Concern | Vendor 11_touch | Current project | Assessment |
| --- | --- | --- | --- |
| I80 bus | 16-bit, full-frame `max_transfer_bytes` (307200), 25 MHz, queue depth 10, byte swap disabled | 16-bit, 115200-byte stripe max (480 x 120 x 2), 20 MHz, queue depth 1, byte swap disabled, 32-byte burst | 20 MHz is more conservative, so clock frequency alone is not the leading explanation for no visible updates. Queue depth and stripe sizing affect throughput, not whether the first valid write reaches GRAM. |
| Read strobe | Drives GPIO14 (`RD`) high before creating the bus | Board catalog records `TFT_RD:14`, but generated LVGL config contains no RD field and no source consumer was found | Highest static control-bus discrepancy. A floating or low RD can leave the panel in read mode while the MCU application otherwise boots. |
| RGB565 | BGR, inversion enabled, I80 hardware byte swap disabled | BGR, inversion enabled, big-endian panel data, hardware byte swap disabled; LVGL callback swaps bytes before submit | This can explain colors, not an unchanged old frame. The task's existing color result supports keeping the swap at the submit boundary. |
| Reset/backlight | XL9555: BL off; LCD reset high 10 ms, low 50 ms, high 200 ms; init, display on, clear white, then BL on | XL9555: BL off; same high/low/high reset durations; additional 120 ms delay; init/display on; BL enabled after touch init | The order and reset pulse are materially equivalent. This is lower risk than RD or the pixel transfer source. |
| Render buffer | Draws from `MALLOC_CAP_INTERNAL` buffers | Adapter allocates the 480 x 120 RGB565 LVGL stripe in PSRAM when capacity permits; callback swaps and cache-syncs that buffer before I80 submit | This is the principal data-path difference. ESP-IDF accepts external RAM but rejects unaligned external address/length before queuing DMA. |

### Evidence-backed root-cause ranking for "new app boots but LCD retains an old screen"

1. **RD is not driven high (highest static discrepancy).** The vendor explicitly configures GPIO14 as output and sets it high before I80 initialization (`vendor .../LCD/lcd.c:702-709`; `lcd.h:40-44`). The project lists the same pin in the board record (`firmware/catalog/board/esp32s3-n16r8-st7796u-gt1151.env:18`), but it is absent from the generated bridge configuration (`firmware-builds/.esp32s3-n16r8-st7796u-gt1151.previous.1785161858/generated/esp_bms_profile_hardware.h:24-76`) and no project source consumes it. If read mode is selected, the CPU, NVS, networking, LVGL task, and even panel initialization can succeed while GRAM receives no pixel writes.

2. **PSRAM-to-I80 DMA submission can be rejected or corrupted if source alignment/lifetime is violated (high, needs hardware/log proof).** The adapter chooses PSRAM for the project display buffer (`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1410-1424`, `1465-1471`) and allocates it with generic PSRAM capabilities (`managed_components/espressif__esp_lvgl_adapter/src/display/display_manager.c:1146-1181`, `1422-1468`). ESP-IDF v6.0.2 validates external source address and byte length against GDMA alignment before it queues the transfer (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_lcd/i80/esp_lcd_panel_io_i80.c:541-589`). A returned `ESP_ERR_INVALID_ARG` leaves the panel's old GRAM visible. The current callback does cache-sync but cannot make an invalid alignment valid (`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1243-1268`).

3. **No vendor-style guaranteed first clear (medium).** The vendor writes white to every pixel before enabling BL (`vendor .../LCD/lcd.c:783-786`). The project enables the panel, registers LVGL, then waits for a normal LVGL flush (`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1443-1496`). If LVGL never produces a successful first flush, retained GRAM remains visible. This is a consequence of a failed write path, not proof of an initialization error by itself.

4. **I80 timing / queue / burst mismatch (low).** Vendor uses 25 MHz and queue depth 10 (`vendor .../LCD/lcd.c:741-759`); project uses 20 MHz, queue depth 1, and 32-byte DMA bursts (`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1318-1356`; `firmware/catalog/display/st7796u-i80.env:17-25`). The project clock is slower, and ESP-IDF v6 defaults to 32 bytes when unset (`/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_lcd/i80/esp_lcd_panel_io_i80.c:648-686`), so these differences are better treated as performance knobs after a successful write is proven.

5. **RGB565 byte order (low for stale-frame symptom).** Vendor and current profile both use BGR/inversion with I80 hardware swap disabled (`vendor .../LCD/lcd.c:769-785`; `firmware/catalog/display/st7796u-i80.env:21-25`). The project's LVGL-only swap occurs immediately before `esp_lcd_panel_draw_bitmap()` (`components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1243-1268`). Incorrect byte order changes colors; it does not normally preserve a prior image unchanged.

### Correct internal DMA staging-buffer rule

- For a diagnostic or permanent bounce path, allocate each staging slot through `esp_lcd_i80_alloc_draw_buffer(panel_io, slot_bytes, MALLOC_CAP_INTERNAL)`. ESP-IDF obtains the bus-specific internal alignment and adds `MALLOC_CAP_DMA` (`.../esp_lcd_panel_io_i80.c:689-701`). Do not use an arbitrary stack buffer or generic `malloc` as the DMA source.
- `esp_lcd_panel_draw_bitmap()` only queues an asynchronous transfer (`.../esp_lcd_panel_io_i80.c:575-589`). A staging slot must remain allocated and unmodified until the corresponding color-transfer-done callback releases it. Returning from `custom_draw_bitmap` is **not** the ownership boundary.
- With the current queue depth of one, use one slot only when submissions are serialized behind the adapter's color-transfer completion. Any direct `WRITE_RGB565` path is an additional producer (`components/esp_bms_display_service/esp_bms_display_service.c:317-323`; `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c:1522-1545`) and must share the same slot lock/completion protocol. Otherwise use at least one slot per allowed in-flight transfer and release slots only from completion.
- Copy and RGB565-swap while copying into the staging slot. Do not first mutate LVGL's PSRAM `color_map` and then reuse it as a source for another asynchronous path; that risks double swap and data reuse before DMA completes.

### Smallest discriminating A/B

1. **First A/B: explicitly drive GPIO14 (`TFT_RD`) high before I80 initialization.** This is the smallest hardware-equivalent change because it tests the highest-confidence vendor discrepancy without changing timing, color, or LVGL memory behavior. A changed screen immediately isolates the control-bus issue.
2. **If RD changes nothing, use the smallest PSRAM isolation A/B: run the I80 LVGL buffer from a small internal buffer (for example 20 rows) rather than a one-slot copied bounce buffer.** It preserves the adapter's existing flush-completion lifecycle and avoids adding a second asynchronous owner. A working result implicates PSRAM source/alignment/cache behavior; an unchanged result shifts attention back to panel signaling and first-flush telemetry.

## Files Found

- `firmware/catalog/display/st7796u-i80.env` - current S3 panel timing, color, orientation, and power settings.
- `firmware/catalog/board/esp32s3-n16r8-st7796u-gt1151.env` - current board pin map, including `TFT_RD:14`.
- `components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c` - I80 initialization, XL9555 sequencing, PSRAM LVGL setup, and submit callback.
- `managed_components/espressif__esp_lvgl_adapter/src/display/display_manager.c` - LVGL draw-buffer allocation behavior.
- `/vol1/1000/toolchains/esp-idf-v6.0.2/components/esp_lcd/i80/esp_lcd_panel_io_i80.c` - actual IDF 6.0.2 DMA alignment, cache, queue, and allocator behavior.
- `/vol1/1000/project/慧勤智远 ESP32-S3 N16R8 V1.0-3.5寸电容屏开发套件/3. 程序源码/1，基础例程/basic_routines/basic_routines/11_touch/components/BSP/LCD/lcd.c` - known-good vendor I80 initialization and internal buffer writes.
- `/vol1/1000/project/慧勤智远 ESP32-S3 N16R8 V1.0-3.5寸电容屏开发套件/3. 程序源码/1，基础例程/basic_routines/basic_routines/11_touch/components/BSP/LCD/lcd.h` - vendor physical I80 pin declarations.

## External References

- ESP-IDF v6.0.2 local I80 API documentation: `/vol1/1000/toolchains/esp-idf-v6.0.2/docs/en/api-reference/peripherals/lcd/i80_lcd.rst`.
- Current project dependency lock: ESP-IDF 6.0.2 and `espressif/esp_lvgl_adapter` 0.6.2 (`dependencies.lock:106-128`, `225-241`).
- Vendor sample SDK configuration: ESP-IDF 5.4.0 (`vendor .../11_touch/sdkconfig:370-374`).

## Related Specs

- `.trellis/spec/backend/hardware-build-flash.md:940-1004` - panel-only mirror and board-specific RGB565 contracts.

## Caveats / Not Found

- This is static-source analysis; it does not prove GPIO14's boot state or capture `color address/size not aligned` logs on hardware.
- No researcher-readable JSONL context existed. `implement.jsonl` and `check.jsonl` were deliberately not read because this Trellis research role is isolated from those manifests.
- GitNexus MCP was not exposed in this dispatched environment, so source-level tracing was used instead.
