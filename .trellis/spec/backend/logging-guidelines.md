# Logging Guidelines

> How logging is done in this project.

---

## Overview

The ESP32 target uses `esp-println` for direct serial diagnostics and initializes
the `log` facade through `esp_println::logger::init_logger_from_env()`. The
build-time `ESP_LOG` environment variable controls dependency logs such as
`esp-radio`.

---

## Log Levels

<!-- When to use each level: debug, info, warn, error -->

- Use direct `[module] ...` `esp_println::println!` lines for bring-up signals
  that must appear even when dependency log filters change.
- Use `log`-facade output for dependency/internal stack diagnostics.

---

## Structured Logging

<!-- Log format, required fields -->

Firmware bring-up logs should use a short module prefix such as `[boot]` or
`[wifi]`, followed by a stable action and redacted fields.

---

## What to Log

<!-- Important events to log -->

### Scenario: ESP32 Wi-Fi Bring-Up Diagnostics

#### 1. Scope / Trigger

- Trigger: target Wi-Fi failures can look identical from the TFT/web UI
  (`connecting`, `offline`, or phone stuck joining the setup AP) unless serial
  logs identify the radio boundary.
- Applies to `src/main.rs`, `src/wireless_wifi.rs`, `.cargo/config.toml`, and
  `Cargo.toml`.

#### 2. Signatures

- Logger initialization: `esp_println::logger::init_logger_from_env()`.
- Direct target diagnostics: `esp_println::println!("[wifi] ...")`.
- Runtime projection:
  `wifi_control::desired_runtime_config(settings) -> WifiRuntimeConfig`.
- Target radio start:
  `wireless_wifi::WifiRuntime::start(wifi, config) -> Result<Option<_>, WifiError>`.
- Runtime reconfiguration:
  `WifiRuntime::apply_config(config) -> Result<bool, WifiError>`.
- Runtime network polling:
  `WifiRuntime::poll()`.

#### 3. Contracts

- Log Wi-Fi desired mode, setup AP SSID, external station SSID, and password
  lengths only.
- Never log setup AP password or external Wi-Fi password plaintext.
- Log `esp-radio` controller initialization/config errors with `Debug` error
  output.
- Log when desired mode is `Off`, when the `WIFI` peripheral is unavailable,
  and when app state is forced to `Offline`.
- Log the actual AP/IP boundary separately from config acceptance:
  - `[wifi] AP started: ip=192.168.4.1 dhcp=on`
  - `[wifi] AP stopped`
- Log SoftAP client association separately from DHCP:
  - `[wifi] AP client connected: clients=<n> first_mac=<mac> rssi=<rssi>`
  - `[wifi] AP client disconnected: clients=0`
- Log DHCP requests/replies after a client is associated, without logging any
  Wi-Fi password.
- Log local HTTP request outcomes as status code and body length only. Do not
  log setup passwords, external Wi-Fi passwords, or request bodies.
- Until the async station connect task exists, target logs must explicitly say
  that station credentials are configured but `connect_async()` is not running.

#### 4. Validation & Error Matrix

- `wifi::new` or initial config fails -> `[wifi] controller init/config failed: ...`.
- `set_config` fails -> `[wifi] set_config failed: ...`.
- Desired mode is off -> `[wifi] desired mode is off; no Wi-Fi runtime started`.
- Runtime exists and config changes -> `[wifi] applying config to existing runtime`.
- Config accepted but no `AP started` log -> inspect whether the AP interface
  link state is being polled and whether `esp_wifi_start` reached
  `AccessPointStart`.
- `AP client connected` appears but no `DHCP` log -> client associated, but the
  phone is likely stuck before receiving an IPv4 lease.
- `DHCP ...` appears but phone still cannot open `192.168.4.1` -> AP IPv4 lease
  likely works; inspect TCP/HTTP server integration next.
- `HTTP 401` from a browser-hosted control page -> the page is reaching the
  device, but the `X-Setup-Password` value does not match the current setup AP
  password.
- Station mode requested before async connect is implemented ->
  `[wifi] station credentials configured; async connect task is not running yet`.

#### 5. Good/Base/Bad Cases

- Good: setup AP log shows `mode=setup-ap`, AP SSID, password length, and
  `controller initialized and initial config accepted`, then `AP started`,
  `AP client connected`, and a DHCP Offer/Ack log when a phone joins.
- Base: AP+STA log shows `mode=setup-ap+station`, both SSIDs, password lengths,
  and the explicit async-connect limitation.
- Bad: printing `"password='secretpass'"` or reporting only `connecting` without
  a radio/config, AP started, client association, or DHCP boundary.

#### 6. Tests Required

- Run `cargo +esp check --bin esp32-bms-gps -j1` for target-only logging code.
- Run `cargo +esp clippy --bin esp32-bms-gps -j1 -- -D warnings` after changing
  log formatting or target imports.
- Run host library tests when logging changes are coupled to shared Wi-Fi state
  contracts.

#### 7. Wrong vs Correct

##### Wrong

```rust
esp_println::println!("wifi password={}", config.external_password.as_str());
```

##### Correct

```rust
esp_println::println!(
    "[wifi] mode={} sta_ssid='{}' sta_pw_len={}",
    mode,
    config.external_ssid.as_str(),
    config.external_password.len()
);
```

---

## What NOT to Log

<!-- Sensitive data, PII, secrets -->

- Do not log Wi-Fi passwords, setup AP passwords, OTA credentials, private URLs
  carrying tokens, or raw payloads that may contain secrets.
