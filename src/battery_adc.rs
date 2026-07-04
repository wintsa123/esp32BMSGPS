#![cfg(target_arch = "xtensa")]

use esp_hal::{
    analog::adc::{Adc, AdcConfig, AdcPin, Attenuation},
    peripherals::{ADC1, GPIO34},
};

use esp32_bms_gps::{
    app_state::AppState,
    battery::{BatterySenseConfig, BatterySenseError},
};

pub const DEFAULT_SAMPLE_PERIOD_TICKS: u8 = 4;
pub const DEFAULT_SENSE_CONFIG: BatterySenseConfig =
    BatterySenseConfig::new(4095, 3300, 100_000, 100_000);

pub struct BatteryAdc<'d> {
    adc: Adc<'d, ADC1<'d>, esp_hal::Blocking>,
    pin: AdcPin<GPIO34<'d>, ADC1<'d>>,
}

impl<'d> BatteryAdc<'d> {
    pub fn new(adc1: ADC1<'d>, gpio34: GPIO34<'d>) -> Self {
        let mut config = AdcConfig::new();
        let pin = config.enable_pin(gpio34, Attenuation::_11dB);
        let adc = Adc::new(adc1, config);

        Self { adc, pin }
    }

    pub fn sample_raw(&mut self) -> Option<u16> {
        nb::block!(self.adc.read_oneshot(&mut self.pin)).ok()
    }

    pub fn sample_into_state(
        &mut self,
        app_state: &mut AppState,
        config: BatterySenseConfig,
    ) -> Result<Option<u32>, BatterySenseError> {
        let Some(raw) = self.sample_raw() else {
            return Ok(None);
        };

        let sample = app_state.battery.update_raw(raw, config)?;
        Ok(Some(sample.voltage_mv))
    }
}
