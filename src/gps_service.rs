use crate::{
    app_state::AppState,
    gps_nmea::{NmeaError, NmeaLineBuffer},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GpsFeedReport {
    pub bytes_seen: u32,
    pub fixes_applied: u32,
    pub parse_errors: u32,
    pub last_error: Option<NmeaError>,
}

impl GpsFeedReport {
    pub const fn new() -> Self {
        Self {
            bytes_seen: 0,
            fixes_applied: 0,
            parse_errors: 0,
            last_error: None,
        }
    }
}

impl Default for GpsFeedReport {
    fn default() -> Self {
        Self::new()
    }
}

pub struct GpsService {
    buffer: NmeaLineBuffer,
    report: GpsFeedReport,
}

impl GpsService {
    pub const fn new() -> Self {
        Self {
            buffer: NmeaLineBuffer::new(),
            report: GpsFeedReport::new(),
        }
    }

    pub fn feed(&mut self, bytes: &[u8], app_state: &mut AppState) -> GpsFeedReport {
        for &byte in bytes {
            self.report.bytes_seen = self.report.bytes_seen.saturating_add(1);
            if let Some(result) = self.buffer.push_byte(byte) {
                match result {
                    Ok(fix) => {
                        app_state.gps.update_fix(fix);
                        self.report.fixes_applied = self.report.fixes_applied.saturating_add(1);
                        self.report.last_error = None;
                    }
                    Err(error) => {
                        if error != NmeaError::NotRmc && error != NmeaError::Empty {
                            self.report.parse_errors = self.report.parse_errors.saturating_add(1);
                            self.report.last_error = Some(error);
                        }
                    }
                }
            }
        }

        self.report
    }

    pub const fn report(&self) -> GpsFeedReport {
        self.report
    }
}

impl Default for GpsService {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{app_state::AppState, gps_nmea::SpeedUnit};

    #[test]
    fn applies_rmc_fix_from_stream() {
        let mut service = GpsService::new();
        let mut state = AppState::default();

        let report = service.feed(
            b"$GPRMC,092751.000,A,5321.6802,N,00630.3372,W,12.4,84.4,230394,,,A*7D\r\n",
            &mut state,
        );

        assert_eq!(report.fixes_applied, 1);
        assert_eq!(report.parse_errors, 0);
        assert!(state.gps.fix_valid);
        assert_eq!(state.gps.sentences_seen, 1);
        assert_eq!(state.gps.speed_deci_units(SpeedUnit::Kmh), Some(229));
    }

    #[test]
    fn ignores_non_rmc_sentences_but_counts_real_parse_errors() {
        let mut service = GpsService::new();
        let mut state = AppState::default();

        let report = service.feed(
            b"$GPGGA,092751.000,5321.6802,N,00630.3372,W,1,08,0.9,545.4,M,46.9,M,,*45\r\n\
              $GPRMC,092751.000,A,5321.6802,N,00630.3372,W,12.4,84.4,230394,,,A*00\r\n",
            &mut state,
        );

        assert_eq!(report.fixes_applied, 0);
        assert_eq!(report.parse_errors, 1);
        assert_eq!(report.last_error, Some(NmeaError::Checksum));
        assert_eq!(state.gps.sentences_seen, 0);
    }

    #[test]
    fn recovers_after_line_too_long() {
        let mut service = GpsService::new();
        let mut state = AppState::default();
        let mut long = [b'A'; 128];
        long[127] = b'\n';

        let report = service.feed(&long, &mut state);

        assert_eq!(report.fixes_applied, 0);
        assert_eq!(report.parse_errors, 1);
        assert_eq!(report.last_error, Some(NmeaError::LineTooLong));
    }
}
