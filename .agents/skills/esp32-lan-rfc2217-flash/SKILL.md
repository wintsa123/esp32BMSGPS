---
name: esp32-lan-rfc2217-flash
description: Flash and monitor this project's ESP32 hardware through the fixed LAN RFC2217 bridge. Use when finishing a firmware-impacting code task that changes ESP-IDF sources, configuration, build inputs, or device runtime behavior; when the user asks to burn/flash/monitor logs; or when ESP-IDF hardware validation is needed from the Linux host through the Windows COM3 bridge. Do not trigger for host-only tools, documentation, generated previews, or Trellis/agent files that do not affect the firmware image.
---

# ESP32 LAN RFC2217 Flash

Use the project's fixed Espressif `esp_rfc2217_server` bridge. Do not ask for a
serial port when this bridge information is available.

## Current Endpoint

- ESP-IDF port: `rfc2217://192.168.2.10:4000?ign_set_control`
- Baud: `115200`
- Current Windows host: `192.168.2.10`
- Windows serial port: `COM3`
- TCP listen address: `0.0.0.0:4000`
- Previous allowed remote: `192.168.2.108/32`
- Firewall rule: `ESP_COM3_TCP_Bridge_4000`
- Recorded listener PID: `43276`
- Known-good validation on 2026-07-11: flashing through the current endpoint
  succeeded with image hash verification; MAC is `20:e7:c8:5f:ab:a4`.

## Completion Workflow

After completing a firmware-impacting code task, run one flash attempt before
reporting final completion. Firmware-impacting tasks include changes to ESP-IDF
sources, components, `sdkconfig*`, partition tables, firmware build files, or
other inputs that alter the device image or runtime behavior.

Do not flash for host-only utility scripts, documentation, generated images or
previews, Trellis/spec files, or agent/skill files unless the user explicitly
asks for hardware validation or those changes also affect the firmware image.

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
```

If boot logs are required, monitor separately:

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor
```

For flash plus monitor in one command:

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash monitor
```

## Rules

- Use RFC2217 for this bridge; do not use `socket://100.118.146.11:4000`.
- Keep `?ign_set_control` in the URL.
- Keep `-b 115200` unless the user updates the bridge baud.
- If flashing fails, report the exact esptool error and whether TCP/RFC2217
  connection was established.
- Only one client should use the bridge at a time; close monitor sessions before
  starting another flash.
