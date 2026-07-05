use crate::{
    ant_bms::BmsTelemetry,
    battery::{BatterySenseConfig, BatteryState},
    bms_scan::BmsScanCandidates,
    gps_nmea::{GpsFix, SpeedUnit},
    settings::{DeviceSettings, SetupApState},
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GpsState {
    pub fix_valid: bool,
    pub speed_knots: f32,
    pub sentences_seen: u32,
}

impl GpsState {
    pub const fn new() -> Self {
        Self {
            fix_valid: false,
            speed_knots: 0.0,
            sentences_seen: 0,
        }
    }

    pub fn update_fix(&mut self, fix: GpsFix) {
        self.fix_valid = fix.valid;
        self.speed_knots = fix.speed_knots;
        self.sentences_seen = self.sentences_seen.saturating_add(1);
    }

    pub fn speed_deci_units(self, unit: SpeedUnit) -> Option<u16> {
        if !self.fix_valid {
            return None;
        }
        let value = GpsFix {
            valid: true,
            speed_knots: self.speed_knots,
        }
        .speed(unit);
        Some((value * 10.0).clamp(0.0, u16::MAX as f32) as u16)
    }
}

impl Default for GpsState {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WifiLinkState {
    SetupApOnly,
    StationConnecting,
    StationConnected,
    Offline,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OtaState {
    Idle,
    Checking,
    UpdateAvailable,
    Downloading,
    Verifying,
    ReadyToReboot,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct BmsState {
    pub online: bool,
    pub telemetry: Option<BmsTelemetry>,
}

impl BmsState {
    pub fn update_telemetry(&mut self, telemetry: BmsTelemetry) {
        self.online = true;
        self.telemetry = Some(telemetry);
    }

    pub fn mark_offline(&mut self) {
        self.online = false;
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AppState {
    pub settings: DeviceSettings,
    pub gps: GpsState,
    pub bms: BmsState,
    pub bms_scan_candidates: BmsScanCandidates,
    pub battery: BatteryState,
    pub wifi: WifiLinkState,
    pub ota: OtaState,
}

impl AppState {
    pub fn new(settings: DeviceSettings) -> Self {
        let wifi = if settings.wifi.setup_ap_state.enabled() {
            WifiLinkState::SetupApOnly
        } else {
            WifiLinkState::Offline
        };

        Self {
            settings,
            gps: GpsState::new(),
            bms: BmsState::default(),
            bms_scan_candidates: BmsScanCandidates::new(),
            battery: BatteryState::new(),
            wifi,
            ota: OtaState::Idle,
        }
    }

    pub fn mark_external_wifi_connecting(&mut self) {
        self.wifi = WifiLinkState::StationConnecting;
    }

    pub fn mark_external_wifi_connected(&mut self) {
        self.settings.wifi.mark_external_wifi_connected();
        self.wifi = WifiLinkState::StationConnected;
    }

    pub fn enable_reprovisioning(&mut self) {
        self.settings.wifi.setup_ap_state = SetupApState::Reprovisioning;
        self.wifi = WifiLinkState::SetupApOnly;
    }

    pub fn update_battery_raw(&mut self, raw_adc: u16, config: BatterySenseConfig) {
        let _ = self.battery.update_raw(raw_adc, config);
    }

    pub fn clear_bms_scan_candidates(&mut self) {
        self.bms_scan_candidates.clear();
    }

    pub fn dashboard_snapshot(&self) -> DashboardSnapshot {
        let telemetry = self.bms.telemetry;
        let mut temperature_celsius = [None; 6];
        if let Some(value) = telemetry {
            let count = (value.temperature_count as usize).min(temperature_celsius.len());
            let mut index = 0;
            while index < count {
                temperature_celsius[index] = Some(value.temperature_celsius[index]);
                index += 1;
            }
        }
        DashboardSnapshot {
            speed_deci_units: self.gps.speed_deci_units(self.settings.speed_unit),
            speed_unit: self.settings.speed_unit,
            gps_fix_valid: self.gps.fix_valid,
            bms_online: self.bms.online,
            pack_voltage_mv: telemetry.map(|value| value.pack_voltage_mv),
            current_deci_amps: telemetry.map(|value| value.current_deci_amps),
            soc_percent: telemetry.map(|value| value.soc_percent),
            min_cell_voltage_mv: telemetry.map(|value| value.min_cell_voltage_mv),
            max_cell_voltage_mv: telemetry.map(|value| value.max_cell_voltage_mv),
            delta_cell_voltage_mv: telemetry.map(|value| value.delta_cell_voltage_mv),
            average_cell_voltage_mv: telemetry.map(|value| value.average_cell_voltage_mv),
            total_capacity_mah: telemetry.map(|value| value.total_capacity_mah),
            capacity_remaining_mah: telemetry.map(|value| value.capacity_remaining_mah),
            temperature_celsius,
            local_battery_voltage_mv: self.battery.latest.map(|sample| sample.voltage_mv),
            setup_ap_enabled: self.settings.wifi.setup_ap_state.enabled(),
            wifi: self.wifi,
            ota: self.ota,
        }
    }
}

impl Default for AppState {
    fn default() -> Self {
        Self::new(DeviceSettings::default())
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DashboardSnapshot {
    pub speed_deci_units: Option<u16>,
    pub speed_unit: SpeedUnit,
    pub gps_fix_valid: bool,
    pub bms_online: bool,
    pub pack_voltage_mv: Option<u32>,
    pub current_deci_amps: Option<i16>,
    pub soc_percent: Option<u16>,
    pub min_cell_voltage_mv: Option<u16>,
    pub max_cell_voltage_mv: Option<u16>,
    pub delta_cell_voltage_mv: Option<u16>,
    pub average_cell_voltage_mv: Option<u16>,
    pub total_capacity_mah: Option<u32>,
    pub capacity_remaining_mah: Option<u32>,
    pub temperature_celsius: [Option<i16>; 6],
    pub local_battery_voltage_mv: Option<u32>,
    pub setup_ap_enabled: bool,
    pub wifi: WifiLinkState,
    pub ota: OtaState,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ant_bms::{
        BalancerStatus, BatteryStatus, MAX_CELLS, MAX_TEMPERATURES, MosfetStatus,
    };

    fn telemetry() -> BmsTelemetry {
        BmsTelemetry {
            permissions: 5,
            battery_status: BatteryStatus::Idle,
            temperature_sensor_count: 2,
            cell_count: 2,
            cell_voltage_mv: {
                let mut cells = [0_u16; MAX_CELLS];
                cells[0] = 3300;
                cells[1] = 3302;
                cells
            },
            temperature_celsius: [0_i16; MAX_TEMPERATURES],
            temperature_count: 6,
            pack_voltage_mv: 6602,
            current_deci_amps: -12,
            soc_percent: 88,
            state_of_health_percent: 99,
            charge_mosfet: MosfetStatus::On,
            discharge_mosfet: MosfetStatus::On,
            balancer: BalancerStatus::Off,
            total_capacity_mah: 100_000,
            capacity_remaining_mah: 88_000,
            cycle_capacity_mah: 123_000,
            power_watts: -79,
            total_runtime_seconds: 1,
            balanced_cell_bitmask: 0,
            max_cell_voltage_mv: 3302,
            max_voltage_cell: 2,
            min_cell_voltage_mv: 3300,
            min_voltage_cell: 1,
            delta_cell_voltage_mv: 2,
            average_cell_voltage_mv: 3301,
        }
    }

    #[test]
    fn default_state_starts_in_setup_ap() {
        let state = AppState::default();
        let snapshot = state.dashboard_snapshot();

        assert_eq!(snapshot.wifi, WifiLinkState::SetupApOnly);
        assert!(snapshot.setup_ap_enabled);
        assert_eq!(snapshot.speed_deci_units, None);
    }

    #[test]
    fn snapshot_combines_gps_bms_and_settings() {
        let mut state = AppState::default();
        state.settings.speed_unit = SpeedUnit::Kmh;
        state.gps.update_fix(GpsFix {
            valid: true,
            speed_knots: 10.0,
        });
        state.update_battery_raw(2048, BatterySenseConfig::new(4095, 3300, 100_000, 100_000));
        state.bms.update_telemetry(telemetry());
        state.mark_external_wifi_connected();

        let snapshot = state.dashboard_snapshot();

        assert_eq!(snapshot.speed_deci_units, Some(185));
        assert!(snapshot.gps_fix_valid);
        assert!(snapshot.bms_online);
        assert_eq!(snapshot.pack_voltage_mv, Some(6602));
        assert_eq!(snapshot.current_deci_amps, Some(-12));
        assert_eq!(snapshot.soc_percent, Some(88));
        assert_eq!(snapshot.total_capacity_mah, Some(100_000));
        assert_eq!(snapshot.capacity_remaining_mah, Some(88_000));
        assert_eq!(snapshot.delta_cell_voltage_mv, Some(2));
        assert_eq!(snapshot.average_cell_voltage_mv, Some(3301));
        assert_eq!(snapshot.temperature_celsius, [Some(0); 6]);
        assert_eq!(snapshot.local_battery_voltage_mv, Some(3300));
        assert_eq!(snapshot.wifi, WifiLinkState::StationConnected);
    }

    #[test]
    fn reprovisioning_reopens_setup_ap() {
        let mut state = AppState::default();
        state.mark_external_wifi_connected();
        state.enable_reprovisioning();

        assert_eq!(state.wifi, WifiLinkState::SetupApOnly);
        assert_eq!(
            state.settings.wifi.setup_ap_state,
            SetupApState::Reprovisioning
        );
    }
}
