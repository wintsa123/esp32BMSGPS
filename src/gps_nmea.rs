#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SpeedUnit {
    Kmh,
    Mph,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GpsFix {
    pub valid: bool,
    pub speed_knots: f32,
}

impl GpsFix {
    pub fn speed(self, unit: SpeedUnit) -> f32 {
        match unit {
            SpeedUnit::Kmh => self.speed_knots * 1.852,
            SpeedUnit::Mph => self.speed_knots * 1.150_779_5,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NmeaError {
    Empty,
    LineTooLong,
    InvalidUtf8,
    Checksum,
    NotRmc,
    MissingField,
    InvalidSpeed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NmeaLineBuffer {
    bytes: [u8; 96],
    len: usize,
}

impl NmeaLineBuffer {
    pub const fn new() -> Self {
        Self {
            bytes: [0; 96],
            len: 0,
        }
    }

    pub fn push_byte(&mut self, byte: u8) -> Option<Result<GpsFix, NmeaError>> {
        match byte {
            b'\n' => {
                let result = self.parse_current_line();
                self.clear();
                Some(result)
            }
            b'\r' => None,
            b'$' => {
                self.clear();
                self.bytes[0] = b'$';
                self.len = 1;
                None
            }
            byte => {
                if self.len >= self.bytes.len() {
                    self.clear();
                    return Some(Err(NmeaError::LineTooLong));
                }
                self.bytes[self.len] = byte;
                self.len += 1;
                None
            }
        }
    }

    pub fn clear(&mut self) {
        self.len = 0;
    }

    fn parse_current_line(&self) -> Result<GpsFix, NmeaError> {
        if self.len == 0 {
            return Err(NmeaError::Empty);
        }
        let line =
            core::str::from_utf8(&self.bytes[..self.len]).map_err(|_| NmeaError::InvalidUtf8)?;
        parse_rmc(line)
    }
}

impl Default for NmeaLineBuffer {
    fn default() -> Self {
        Self::new()
    }
}

pub fn parse_rmc(sentence: &str) -> Result<GpsFix, NmeaError> {
    let sentence = sentence.trim();
    if sentence.is_empty() {
        return Err(NmeaError::Empty);
    }

    let payload = strip_and_validate_checksum(sentence)?;
    let mut fields = payload.split(',');

    let kind = fields.next().ok_or(NmeaError::MissingField)?;
    if !matches!(kind, "GPRMC" | "GNRMC" | "GARMC" | "GLRMC" | "BDRMC") {
        return Err(NmeaError::NotRmc);
    }

    let _time = fields.next().ok_or(NmeaError::MissingField)?;
    let status = fields.next().ok_or(NmeaError::MissingField)?;
    let _latitude = fields.next().ok_or(NmeaError::MissingField)?;
    let _latitude_dir = fields.next().ok_or(NmeaError::MissingField)?;
    let _longitude = fields.next().ok_or(NmeaError::MissingField)?;
    let _longitude_dir = fields.next().ok_or(NmeaError::MissingField)?;
    let speed_knots = fields
        .next()
        .ok_or(NmeaError::MissingField)?
        .parse::<f32>()
        .map_err(|_| NmeaError::InvalidSpeed)?;

    Ok(GpsFix {
        valid: status == "A",
        speed_knots,
    })
}

fn strip_and_validate_checksum(sentence: &str) -> Result<&str, NmeaError> {
    let body = sentence.strip_prefix('$').unwrap_or(sentence);

    let Some((payload, checksum)) = body.split_once('*') else {
        return Ok(body);
    };

    let expected = u8::from_str_radix(checksum, 16).map_err(|_| NmeaError::Checksum)?;
    let actual = payload.bytes().fold(0_u8, |crc, byte| crc ^ byte);
    if actual != expected {
        return Err(NmeaError::Checksum);
    }

    Ok(payload)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_valid_rmc_speed() {
        let fix = parse_rmc("$GPRMC,092751.000,A,5321.6802,N,00630.3372,W,12.4,84.4,230394,,,A*7D")
            .unwrap();

        assert!(fix.valid);
        assert!((fix.speed(SpeedUnit::Kmh) - 22.9648).abs() < 0.001);
        assert!((fix.speed(SpeedUnit::Mph) - 14.2697).abs() < 0.001);
    }

    #[test]
    fn parses_void_fix_as_invalid_but_keeps_speed_field() {
        let fix = parse_rmc("$GNRMC,092751.000,V,5321.6802,N,00630.3372,W,0.0,84.4,230394,,,A*43")
            .unwrap();

        assert!(!fix.valid);
        assert_eq!(fix.speed_knots, 0.0);
    }

    #[test]
    fn rejects_bad_checksum() {
        assert_eq!(
            parse_rmc("$GPRMC,092751.000,A,5321.6802,N,00630.3372,W,12.4,84.4,230394,,,A*00"),
            Err(NmeaError::Checksum)
        );
    }

    #[test]
    fn rejects_non_rmc_sentence() {
        assert_eq!(
            parse_rmc("$GPGGA,092751.000,5321.6802,N,00630.3372,W,1,08,0.9,545.4,M,46.9,M,,*45"),
            Err(NmeaError::NotRmc)
        );
    }

    #[test]
    fn line_buffer_parses_rmc_from_byte_stream() {
        let mut buffer = NmeaLineBuffer::new();
        let mut parsed = None;

        for byte in b"noise$GPRMC,092751.000,A,5321.6802,N,00630.3372,W,12.4,84.4,230394,,,A*7D\r\n"
        {
            if let Some(result) = buffer.push_byte(*byte) {
                parsed = Some(result);
            }
        }

        let fix = parsed.unwrap().unwrap();
        assert!(fix.valid);
        assert_eq!(fix.speed_knots, 12.4);
    }

    #[test]
    fn line_buffer_reports_overlong_lines_and_recovers() {
        let mut buffer = NmeaLineBuffer::new();
        let mut error = None;

        for _ in 0..100 {
            if let Some(result) = buffer.push_byte(b'A') {
                error = Some(result);
                break;
            }
        }

        assert_eq!(error, Some(Err(NmeaError::LineTooLong)));
        assert_eq!(buffer.push_byte(b'\n'), Some(Err(NmeaError::Empty)));
    }
}
