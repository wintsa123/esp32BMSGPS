# LAN RFC2217 Flash Bridge

This project has a fixed remote ESP32 flashing/monitoring path through the Windows-side Espressif `esp_rfc2217_server` bridge.

- Always use ESP-IDF port URL: `rfc2217://192.168.2.10:4000?ign_set_control`.
- Use baud `115200` unless the user updates the bridge baud.
- This bridge is RFC2217. Do not use `socket://192.168.2.10:4000` for this setup.
- Windows-side serial port: `COM3`.
- TCP listen address: `0.0.0.0:4000`.
- Windows host IP: `192.168.2.10`.
- Allowed remote CIDR: `192.168.2.108/32`.
- Firewall rule: `ESP_COM3_TCP_Bridge_4000`.
- Recorded listener PID: `43276`.
- Known-good local validation: `esptool read_mac` through RFC2217 succeeds; MAC is `20:e7:c8:5f:ab:a4`.

Workflow preference from the user: after completing each code task, run one hardware flash attempt through this LAN RFC2217 bridge and report the result. Do not ask for the serial port again when this bridge information is available.

Default commands:

```bash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 monitor
./scripts/esp-idf-env.sh -p "rfc2217://192.168.2.10:4000?ign_set_control" -b 115200 flash monitor
```

Important: RFC2217 is required because ESP32 flashing needs remote DTR/RTS control to enter the ROM download mode. The previous raw TCP/socket bridge could connect but could not control these lines reliably.