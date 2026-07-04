use crate::{
    app_state::{AppState, WifiLinkState},
    settings::{DeviceSettings, FixedAscii, SetupApState},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DesiredWifiMode {
    Off,
    SetupAp,
    Station,
    SetupApAndStation,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WifiRuntimeConfig {
    pub mode: DesiredWifiMode,
    pub setup_ap_ssid: FixedAscii<32>,
    pub setup_ap_password: FixedAscii<64>,
    pub external_ssid: FixedAscii<32>,
    pub external_password: FixedAscii<64>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WifiEvent {
    StationConnectRequested,
    StationConnected,
    StationDisconnected,
    SetupApStarted,
    SetupApStopped,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WifiEventOutcome {
    pub settings_changed: bool,
}

pub fn desired_runtime_config(settings: &DeviceSettings) -> WifiRuntimeConfig {
    let setup_ap_enabled = settings.wifi.setup_ap_state.enabled();
    let station_enabled =
        settings.wifi.external_wifi_saved && !settings.wifi.external_ssid.is_empty();

    let mode = match (setup_ap_enabled, station_enabled) {
        (false, false) => DesiredWifiMode::Off,
        (true, false) => DesiredWifiMode::SetupAp,
        (false, true) => DesiredWifiMode::Station,
        (true, true) => DesiredWifiMode::SetupApAndStation,
    };

    WifiRuntimeConfig {
        mode,
        setup_ap_ssid: settings.wifi.setup_ap_ssid,
        setup_ap_password: settings.wifi.setup_ap_password,
        external_ssid: settings.wifi.external_ssid,
        external_password: settings.wifi.external_password,
    }
}

pub fn apply_wifi_event(app_state: &mut AppState, event: WifiEvent) -> WifiEventOutcome {
    let mut settings_changed = false;

    match event {
        WifiEvent::StationConnectRequested => {
            app_state.wifi = WifiLinkState::StationConnecting;
        }
        WifiEvent::StationConnected => {
            let old_setup_state = app_state.settings.wifi.setup_ap_state;
            app_state.mark_external_wifi_connected();
            settings_changed = old_setup_state != app_state.settings.wifi.setup_ap_state;
        }
        WifiEvent::StationDisconnected => {
            app_state.wifi = if app_state.settings.wifi.setup_ap_state.enabled() {
                WifiLinkState::SetupApOnly
            } else {
                WifiLinkState::Offline
            };
        }
        WifiEvent::SetupApStarted => {
            if app_state.settings.wifi.setup_ap_state.enabled() {
                app_state.wifi = WifiLinkState::SetupApOnly;
            }
        }
        WifiEvent::SetupApStopped => {
            if app_state.wifi == WifiLinkState::SetupApOnly {
                app_state.wifi = WifiLinkState::Offline;
            }
        }
    }

    WifiEventOutcome { settings_changed }
}

pub fn enable_setup_ap_for_reprovisioning(app_state: &mut AppState) -> WifiEventOutcome {
    let changed = app_state.settings.wifi.setup_ap_state != SetupApState::Reprovisioning;
    app_state.enable_reprovisioning();
    WifiEventOutcome {
        settings_changed: changed,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn configured_settings() -> DeviceSettings {
        let mut settings = DeviceSettings::default();
        settings.wifi.setup_ap_ssid = FixedAscii::try_from_str("ESP32-SETUP").unwrap();
        settings.wifi.set_setup_ap_password("setup1234").unwrap();
        settings
            .wifi
            .set_external_credentials("garage", "secretpass")
            .unwrap();
        settings
    }

    #[test]
    fn chooses_ap_sta_mode_during_first_boot_with_credentials() {
        let settings = configured_settings();

        let config = desired_runtime_config(&settings);

        assert_eq!(config.mode, DesiredWifiMode::SetupApAndStation);
        assert_eq!(config.setup_ap_ssid.as_str(), "ESP32-SETUP");
        assert_eq!(config.external_ssid.as_str(), "garage");
    }

    #[test]
    fn station_connected_disables_setup_ap_and_requires_persist() {
        let mut app_state = AppState::new(configured_settings());

        let outcome = apply_wifi_event(&mut app_state, WifiEvent::StationConnected);

        assert!(outcome.settings_changed);
        assert_eq!(app_state.wifi, WifiLinkState::StationConnected);
        assert_eq!(
            app_state.settings.wifi.setup_ap_state,
            SetupApState::Disabled
        );
        assert_eq!(
            desired_runtime_config(&app_state.settings).mode,
            DesiredWifiMode::Station
        );
    }

    #[test]
    fn disconnected_station_keeps_setup_ap_when_reprovisioning() {
        let mut app_state = AppState::new(configured_settings());
        enable_setup_ap_for_reprovisioning(&mut app_state);

        let outcome = apply_wifi_event(&mut app_state, WifiEvent::StationDisconnected);

        assert!(!outcome.settings_changed);
        assert_eq!(app_state.wifi, WifiLinkState::SetupApOnly);
        assert_eq!(
            app_state.settings.wifi.setup_ap_state,
            SetupApState::Reprovisioning
        );
    }

    #[test]
    fn reprovisioning_is_idempotent_but_updates_wifi_state() {
        let mut app_state = AppState::new(configured_settings());
        app_state.mark_external_wifi_connected();

        let first = enable_setup_ap_for_reprovisioning(&mut app_state);
        let second = enable_setup_ap_for_reprovisioning(&mut app_state);

        assert!(first.settings_changed);
        assert!(!second.settings_changed);
        assert_eq!(app_state.wifi, WifiLinkState::SetupApOnly);
    }
}
