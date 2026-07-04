#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BatterySenseError {
    ZeroAdcMax,
    InvalidDivider,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BatterySenseConfig {
    pub adc_max: u16,
    pub reference_mv: u16,
    pub divider_top_ohms: u32,
    pub divider_bottom_ohms: u32,
}

impl BatterySenseConfig {
    pub const fn new(
        adc_max: u16,
        reference_mv: u16,
        divider_top_ohms: u32,
        divider_bottom_ohms: u32,
    ) -> Self {
        Self {
            adc_max,
            reference_mv,
            divider_top_ohms,
            divider_bottom_ohms,
        }
    }

    pub fn adc_to_battery_mv(self, raw: u16) -> Result<u32, BatterySenseError> {
        if self.adc_max == 0 {
            return Err(BatterySenseError::ZeroAdcMax);
        }
        if self.divider_bottom_ohms == 0 {
            return Err(BatterySenseError::InvalidDivider);
        }

        let pin_mv = raw as u64 * self.reference_mv as u64 / self.adc_max as u64;
        let divider_total = self.divider_top_ohms as u64 + self.divider_bottom_ohms as u64;
        let battery_mv = pin_mv * divider_total / self.divider_bottom_ohms as u64;
        Ok(battery_mv.min(u32::MAX as u64) as u32)
    }
}

impl Default for BatterySenseConfig {
    fn default() -> Self {
        Self::new(4095, 3300, 100_000, 100_000)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BatterySample {
    pub raw_adc: u16,
    pub voltage_mv: u32,
}

impl BatterySample {
    pub fn from_raw(raw_adc: u16, config: BatterySenseConfig) -> Result<Self, BatterySenseError> {
        Ok(Self {
            raw_adc,
            voltage_mv: config.adc_to_battery_mv(raw_adc)?,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct BatteryState {
    pub latest: Option<BatterySample>,
    pub samples_seen: u32,
}

impl BatteryState {
    pub const fn new() -> Self {
        Self {
            latest: None,
            samples_seen: 0,
        }
    }

    pub fn update_raw(
        &mut self,
        raw_adc: u16,
        config: BatterySenseConfig,
    ) -> Result<BatterySample, BatterySenseError> {
        let sample = BatterySample::from_raw(raw_adc, config)?;
        self.latest = Some(sample);
        self.samples_seen = self.samples_seen.saturating_add(1);
        Ok(sample)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn converts_adc_sample_through_voltage_divider() {
        let config = BatterySenseConfig::new(4095, 3300, 100_000, 100_000);
        let sample = BatterySample::from_raw(2048, config).unwrap();

        assert_eq!(sample.raw_adc, 2048);
        assert_eq!(sample.voltage_mv, 3300);
    }

    #[test]
    fn rejects_invalid_config() {
        assert_eq!(
            BatterySenseConfig::new(0, 3300, 100_000, 100_000).adc_to_battery_mv(1),
            Err(BatterySenseError::ZeroAdcMax)
        );
        assert_eq!(
            BatterySenseConfig::new(4095, 3300, 100_000, 0).adc_to_battery_mv(1),
            Err(BatterySenseError::InvalidDivider)
        );
    }

    #[test]
    fn state_tracks_latest_sample() {
        let mut state = BatteryState::new();

        let sample = state
            .update_raw(4095, BatterySenseConfig::new(4095, 3300, 2, 1))
            .unwrap();

        assert_eq!(sample.voltage_mv, 9900);
        assert_eq!(state.latest, Some(sample));
        assert_eq!(state.samples_seen, 1);
    }
}
