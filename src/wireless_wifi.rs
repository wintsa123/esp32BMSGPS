#![cfg(all(target_arch = "xtensa", feature = "wireless"))]

use esp_hal::peripherals::WIFI;
use esp_radio::wifi::{
    self, AuthenticationMethod, Config, ControllerConfig, Interfaces, WifiController, WifiError,
    ap::AccessPointConfig, sta::StationConfig,
};

use esp32_bms_gps::wifi_control::{DesiredWifiMode, WifiRuntimeConfig};

pub struct WifiRuntime<'d> {
    controller: WifiController<'d>,
    _interfaces: Interfaces<'d>,
}

impl WifiRuntime<'static> {
    pub fn start(
        wifi: WIFI<'static>,
        config: WifiRuntimeConfig,
    ) -> Result<Option<Self>, WifiError> {
        log_runtime_config("start requested", config);
        let Some(initial_config) = esp_radio_config(config) else {
            esp_println::println!("[wifi] start skipped: desired mode is off");
            return Ok(None);
        };
        let controller_config = ControllerConfig::default().with_initial_config(initial_config);
        let (controller, interfaces) = match wifi::new(wifi, controller_config) {
            Ok(parts) => {
                esp_println::println!("[wifi] controller initialized and initial config accepted");
                parts
            }
            Err(error) => {
                esp_println::println!("[wifi] controller init/config failed: {:?}", error);
                return Err(error);
            }
        };
        log_station_connect_limit(config);

        Ok(Some(Self {
            controller,
            _interfaces: interfaces,
        }))
    }

    pub fn apply_config(&mut self, config: WifiRuntimeConfig) -> Result<bool, WifiError> {
        log_runtime_config("apply requested", config);
        let Some(radio_config) = esp_radio_config(config) else {
            esp_println::println!("[wifi] apply skipped: desired mode is off");
            return Ok(false);
        };
        if let Err(error) = self.controller.set_config(&radio_config) {
            esp_println::println!("[wifi] set_config failed: {:?}", error);
            return Err(error);
        }
        esp_println::println!("[wifi] set_config ok");
        log_station_connect_limit(config);
        Ok(true)
    }
}

pub fn esp_radio_config(config: WifiRuntimeConfig) -> Option<Config> {
    match config.mode {
        DesiredWifiMode::Off => None,
        DesiredWifiMode::SetupAp => Some(Config::AccessPoint(setup_ap_config(config))),
        DesiredWifiMode::Station => Some(Config::Station(station_config(config))),
        DesiredWifiMode::SetupApAndStation => Some(Config::AccessPointStation(
            station_config(config),
            setup_ap_config(config),
        )),
    }
}

fn setup_ap_config(config: WifiRuntimeConfig) -> AccessPointConfig {
    AccessPointConfig::default()
        .with_ssid(config.setup_ap_ssid.as_str())
        .with_auth_method(AuthenticationMethod::Wpa2Personal)
        .with_password(config.setup_ap_password.as_str().into())
        .with_max_connections(2)
}

fn station_config(config: WifiRuntimeConfig) -> StationConfig {
    StationConfig::default()
        .with_ssid(config.external_ssid.as_str())
        .with_password(config.external_password.as_str().into())
}

fn log_runtime_config(action: &str, config: WifiRuntimeConfig) {
    esp_println::println!(
        "[wifi] {}: mode={} ap_ssid='{}' ap_pw_len={} sta_ssid='{}' sta_pw_len={}",
        action,
        mode_label(config.mode),
        config.setup_ap_ssid.as_str(),
        config.setup_ap_password.len(),
        config.external_ssid.as_str(),
        config.external_password.len()
    );
}

fn log_station_connect_limit(config: WifiRuntimeConfig) {
    if matches!(
        config.mode,
        DesiredWifiMode::Station | DesiredWifiMode::SetupApAndStation
    ) {
        esp_println::println!(
            "[wifi] station credentials configured; async connect task is not running yet"
        );
    }
}

fn mode_label(mode: DesiredWifiMode) -> &'static str {
    match mode {
        DesiredWifiMode::Off => "off",
        DesiredWifiMode::SetupAp => "setup-ap",
        DesiredWifiMode::Station => "station",
        DesiredWifiMode::SetupApAndStation => "setup-ap+station",
    }
}
