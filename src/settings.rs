use crate::gps_nmea::SpeedUnit;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FixedTextError {
    TooLong,
    NonAscii,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FixedAscii<const N: usize> {
    bytes: [u8; N],
    len: u8,
}

impl<const N: usize> FixedAscii<N> {
    pub const fn empty() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }

    pub fn try_from_str(value: &str) -> Result<Self, FixedTextError> {
        Self::from_ascii_bytes(value.as_bytes())
    }

    pub fn from_ascii_bytes(value: &[u8]) -> Result<Self, FixedTextError> {
        if value.len() > N || value.len() > u8::MAX as usize {
            return Err(FixedTextError::TooLong);
        }
        if !value.is_ascii() {
            return Err(FixedTextError::NonAscii);
        }

        let mut out = Self::empty();
        out.bytes[..value.len()].copy_from_slice(value);
        out.len = value.len() as u8;
        Ok(out)
    }

    pub const fn len(self) -> usize {
        self.len as usize
    }

    pub const fn is_empty(self) -> bool {
        self.len == 0
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len()]
    }

    pub fn as_str(&self) -> &str {
        core::str::from_utf8(self.as_bytes()).unwrap_or("")
    }

    pub const fn raw_bytes(self) -> [u8; N] {
        self.bytes
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MacAddressError {
    InvalidLength,
    InvalidSeparator,
    InvalidHex,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MacAddress {
    pub octets: [u8; 6],
}

impl MacAddress {
    pub const fn new(octets: [u8; 6]) -> Self {
        Self { octets }
    }

    pub fn parse_colon_hex(value: &str) -> Result<Self, MacAddressError> {
        let bytes = value.as_bytes();
        if bytes.len() != 17 {
            return Err(MacAddressError::InvalidLength);
        }

        let mut octets = [0_u8; 6];
        let mut index = 0;
        while index < 6 {
            let base = index * 3;
            octets[index] = parse_hex_pair(bytes[base], bytes[base + 1])?;
            if index < 5 && bytes[base + 2] != b':' {
                return Err(MacAddressError::InvalidSeparator);
            }
            index += 1;
        }

        Ok(Self { octets })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DisplayRotation {
    Portrait,
    Landscape,
    InvertedPortrait,
    InvertedLandscape,
}

impl DisplayRotation {
    pub const fn logical_size(self) -> (u16, u16) {
        match self {
            Self::Portrait | Self::InvertedPortrait => (240, 320),
            Self::Landscape | Self::InvertedLandscape => (320, 240),
        }
    }

    pub const fn as_config_value(self) -> &'static str {
        match self {
            Self::Portrait => "portrait",
            Self::Landscape => "landscape",
            Self::InvertedPortrait => "inverted_portrait",
            Self::InvertedLandscape => "inverted_landscape",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Language {
    Chinese,
    English,
}

impl Language {
    pub const fn as_config_value(self) -> &'static str {
        match self {
            Self::Chinese => "zh",
            Self::English => "en",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SetupApState {
    FirstBoot,
    Disabled,
    Reprovisioning,
}

impl SetupApState {
    pub const fn enabled(self) -> bool {
        matches!(self, Self::FirstBoot | Self::Reprovisioning)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RawTouchPoint {
    pub x: u16,
    pub y: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ScreenPoint {
    pub x: u16,
    pub y: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TouchCalibration {
    pub raw_x_min: u16,
    pub raw_x_max: u16,
    pub raw_y_min: u16,
    pub raw_y_max: u16,
    pub swap_xy: bool,
    pub invert_x: bool,
    pub invert_y: bool,
    pub width: u16,
    pub height: u16,
}

impl TouchCalibration {
    pub const ENCODED_LEN: usize = 16;

    pub fn from_four_points(
        top_left: RawTouchPoint,
        top_right: RawTouchPoint,
        bottom_right: RawTouchPoint,
        bottom_left: RawTouchPoint,
        width: u16,
        height: u16,
    ) -> Self {
        let horizontal_delta_x = abs_diff(top_left.x, top_right.x)
            .saturating_add(abs_diff(bottom_left.x, bottom_right.x));
        let horizontal_delta_y = abs_diff(top_left.y, top_right.y)
            .saturating_add(abs_diff(bottom_left.y, bottom_right.y));
        let swap_xy = horizontal_delta_y > horizontal_delta_x;

        let left_a = axis_value(top_left, swap_xy);
        let left_b = axis_value(bottom_left, swap_xy);
        let right_a = axis_value(top_right, swap_xy);
        let right_b = axis_value(bottom_right, swap_xy);
        let top_a = cross_axis_value(top_left, swap_xy);
        let top_b = cross_axis_value(top_right, swap_xy);
        let bottom_a = cross_axis_value(bottom_left, swap_xy);
        let bottom_b = cross_axis_value(bottom_right, swap_xy);

        let left_avg = avg_u16(left_a, left_b);
        let right_avg = avg_u16(right_a, right_b);
        let top_avg = avg_u16(top_a, top_b);
        let bottom_avg = avg_u16(bottom_a, bottom_b);

        Self {
            raw_x_min: left_avg.min(right_avg),
            raw_x_max: left_avg.max(right_avg),
            raw_y_min: top_avg.min(bottom_avg),
            raw_y_max: top_avg.max(bottom_avg),
            swap_xy,
            invert_x: right_avg < left_avg,
            invert_y: bottom_avg < top_avg,
            width,
            height,
        }
    }

    pub fn map(self, raw: RawTouchPoint) -> ScreenPoint {
        let raw_x = axis_value(raw, self.swap_xy);
        let raw_y = cross_axis_value(raw, self.swap_xy);
        let max_x = self.width.saturating_sub(1);
        let max_y = self.height.saturating_sub(1);
        let mut x = scale_axis(raw_x, self.raw_x_min, self.raw_x_max, max_x);
        let mut y = scale_axis(raw_y, self.raw_y_min, self.raw_y_max, max_y);

        if self.invert_x {
            x = max_x.saturating_sub(x);
        }
        if self.invert_y {
            y = max_y.saturating_sub(y);
        }

        ScreenPoint { x, y }
    }

    pub fn map_for_rotation(self, raw: RawTouchPoint, rotation: DisplayRotation) -> ScreenPoint {
        let point = self.map(raw);
        let source_rotation = calibration_source_rotation(self.width, self.height);
        let portrait = point_to_portrait(point, self.width, self.height, source_rotation);
        clamp_screen_point(
            point_from_portrait(portrait, self.width, self.height, rotation),
            rotation,
        )
    }

    pub fn encode(self) -> [u8; Self::ENCODED_LEN] {
        let mut bytes = [0_u8; Self::ENCODED_LEN];
        bytes[0..2].copy_from_slice(&self.raw_x_min.to_le_bytes());
        bytes[2..4].copy_from_slice(&self.raw_x_max.to_le_bytes());
        bytes[4..6].copy_from_slice(&self.raw_y_min.to_le_bytes());
        bytes[6..8].copy_from_slice(&self.raw_y_max.to_le_bytes());
        bytes[8..10].copy_from_slice(&self.width.to_le_bytes());
        bytes[10..12].copy_from_slice(&self.height.to_le_bytes());
        bytes[12] = u8::from(self.swap_xy)
            | (u8::from(self.invert_x) << 1)
            | (u8::from(self.invert_y) << 2);
        bytes
    }

    pub fn decode(bytes: &[u8; Self::ENCODED_LEN]) -> Option<Self> {
        let raw_x_min = u16::from_le_bytes([bytes[0], bytes[1]]);
        let raw_x_max = u16::from_le_bytes([bytes[2], bytes[3]]);
        let raw_y_min = u16::from_le_bytes([bytes[4], bytes[5]]);
        let raw_y_max = u16::from_le_bytes([bytes[6], bytes[7]]);
        let width = u16::from_le_bytes([bytes[8], bytes[9]]);
        let height = u16::from_le_bytes([bytes[10], bytes[11]]);
        let flags = bytes[12];

        if raw_x_min >= raw_x_max || raw_y_min >= raw_y_max || width == 0 || height == 0 {
            return None;
        }

        Some(Self {
            raw_x_min,
            raw_x_max,
            raw_y_min,
            raw_y_max,
            swap_xy: flags & 0x01 != 0,
            invert_x: flags & 0x02 != 0,
            invert_y: flags & 0x04 != 0,
            width,
            height,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WifiSettings {
    pub setup_ap_state: SetupApState,
    pub external_wifi_saved: bool,
    pub setup_ap_password_saved: bool,
    pub setup_ap_ssid: FixedAscii<32>,
    pub setup_ap_password: FixedAscii<64>,
    pub external_ssid: FixedAscii<32>,
    pub external_password: FixedAscii<64>,
}

impl WifiSettings {
    pub const fn first_boot() -> Self {
        Self {
            setup_ap_state: SetupApState::FirstBoot,
            external_wifi_saved: false,
            setup_ap_password_saved: false,
            setup_ap_ssid: FixedAscii::empty(),
            setup_ap_password: FixedAscii::empty(),
            external_ssid: FixedAscii::empty(),
            external_password: FixedAscii::empty(),
        }
    }

    pub fn set_external_credentials(
        &mut self,
        ssid: &str,
        password: &str,
    ) -> Result<(), FixedTextError> {
        self.external_ssid = FixedAscii::try_from_str(ssid)?;
        self.external_password = FixedAscii::try_from_str(password)?;
        self.external_wifi_saved = true;
        Ok(())
    }

    pub fn set_setup_ap_password(&mut self, password: &str) -> Result<(), FixedTextError> {
        self.setup_ap_password = FixedAscii::try_from_str(password)?;
        self.setup_ap_password_saved = true;
        Ok(())
    }

    pub fn mark_external_wifi_saved(&mut self) {
        self.external_wifi_saved = true;
    }

    pub fn mark_external_wifi_connected(&mut self) {
        if self.external_wifi_saved {
            self.setup_ap_state = SetupApState::Disabled;
        }
    }

    pub fn enable_reprovisioning(&mut self) {
        self.setup_ap_state = SetupApState::Reprovisioning;
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct BmsSettings {
    pub bound_mac: Option<MacAddress>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceSettings {
    pub display_rotation: DisplayRotation,
    pub brightness_percent: u8,
    pub speed_unit: SpeedUnit,
    pub language: Language,
    pub wifi: WifiSettings,
    pub bms: BmsSettings,
    pub touch_calibration: Option<TouchCalibration>,
}

impl Default for DeviceSettings {
    fn default() -> Self {
        Self {
            display_rotation: DisplayRotation::Landscape,
            brightness_percent: 80,
            speed_unit: SpeedUnit::Kmh,
            language: Language::Chinese,
            wifi: WifiSettings::first_boot(),
            bms: BmsSettings::default(),
            touch_calibration: None,
        }
    }
}

impl DeviceSettings {
    pub fn set_brightness_percent(&mut self, brightness_percent: u8) {
        self.brightness_percent = brightness_percent.clamp(10, 100);
    }

    pub fn restore_defaults(&mut self) {
        *self = Self::default();
    }
}

fn axis_value(point: RawTouchPoint, swap_xy: bool) -> u16 {
    if swap_xy { point.y } else { point.x }
}

fn cross_axis_value(point: RawTouchPoint, swap_xy: bool) -> u16 {
    if swap_xy { point.x } else { point.y }
}

fn abs_diff(a: u16, b: u16) -> u16 {
    a.max(b) - a.min(b)
}

fn avg_u16(a: u16, b: u16) -> u16 {
    ((a as u32 + b as u32) / 2) as u16
}

fn scale_axis(raw: u16, raw_min: u16, raw_max: u16, screen_max: u16) -> u16 {
    if raw_max <= raw_min || screen_max == 0 {
        return 0;
    }

    let clamped = raw.clamp(raw_min, raw_max);
    let numerator = (clamped - raw_min) as u32 * screen_max as u32;
    let denominator = (raw_max - raw_min) as u32;
    (numerator / denominator) as u16
}

fn calibration_source_rotation(width: u16, height: u16) -> DisplayRotation {
    if width > height {
        DisplayRotation::Landscape
    } else {
        DisplayRotation::Portrait
    }
}

fn point_to_portrait(
    point: ScreenPoint,
    width: u16,
    height: u16,
    rotation: DisplayRotation,
) -> ScreenPoint {
    match rotation {
        DisplayRotation::Portrait => point,
        DisplayRotation::Landscape => ScreenPoint {
            x: height.saturating_sub(1).saturating_sub(point.y),
            y: point.x,
        },
        DisplayRotation::InvertedPortrait => ScreenPoint {
            x: width.saturating_sub(1).saturating_sub(point.x),
            y: height.saturating_sub(1).saturating_sub(point.y),
        },
        DisplayRotation::InvertedLandscape => ScreenPoint {
            x: point.y,
            y: width.saturating_sub(1).saturating_sub(point.x),
        },
    }
}

fn point_from_portrait(
    point: ScreenPoint,
    source_width: u16,
    source_height: u16,
    rotation: DisplayRotation,
) -> ScreenPoint {
    let (portrait_width, portrait_height) = if source_width > source_height {
        (source_height, source_width)
    } else {
        (source_width, source_height)
    };

    match rotation {
        DisplayRotation::Portrait => point,
        DisplayRotation::Landscape => ScreenPoint {
            x: point.y,
            y: portrait_width.saturating_sub(1).saturating_sub(point.x),
        },
        DisplayRotation::InvertedPortrait => ScreenPoint {
            x: portrait_width.saturating_sub(1).saturating_sub(point.x),
            y: portrait_height.saturating_sub(1).saturating_sub(point.y),
        },
        DisplayRotation::InvertedLandscape => ScreenPoint {
            x: portrait_height.saturating_sub(1).saturating_sub(point.y),
            y: point.x,
        },
    }
}

fn clamp_screen_point(point: ScreenPoint, rotation: DisplayRotation) -> ScreenPoint {
    let (width, height) = rotation.logical_size();
    ScreenPoint {
        x: point.x.min(width.saturating_sub(1)),
        y: point.y.min(height.saturating_sub(1)),
    }
}

fn parse_hex_pair(high: u8, low: u8) -> Result<u8, MacAddressError> {
    Ok((hex_nibble(high)? << 4) | hex_nibble(low)?)
}

fn hex_nibble(value: u8) -> Result<u8, MacAddressError> {
    match value {
        b'0'..=b'9' => Ok(value - b'0'),
        b'a'..=b'f' => Ok(value - b'a' + 10),
        b'A'..=b'F' => Ok(value - b'A' + 10),
        _ => Err(MacAddressError::InvalidHex),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_settings_start_setup_ap() {
        let settings = DeviceSettings::default();

        assert_eq!(settings.display_rotation, DisplayRotation::Landscape);
        assert_eq!(settings.speed_unit, SpeedUnit::Kmh);
        assert_eq!(settings.language, Language::Chinese);
        assert!(settings.wifi.setup_ap_state.enabled());
        assert!(!settings.wifi.external_wifi_saved);
    }

    #[test]
    fn external_wifi_connection_disables_setup_ap() {
        let mut settings = DeviceSettings::default();

        settings
            .wifi
            .set_external_credentials("garage", "secretpass")
            .unwrap();
        settings.wifi.mark_external_wifi_connected();

        assert_eq!(settings.wifi.setup_ap_state, SetupApState::Disabled);
        assert!(!settings.wifi.setup_ap_state.enabled());
        assert_eq!(settings.wifi.external_ssid.as_str(), "garage");
    }

    #[test]
    fn touchscreen_reprovisioning_reopens_setup_ap() {
        let mut settings = DeviceSettings::default();

        settings.wifi.mark_external_wifi_saved();
        settings.wifi.mark_external_wifi_connected();
        settings.wifi.enable_reprovisioning();

        assert_eq!(settings.wifi.setup_ap_state, SetupApState::Reprovisioning);
        assert!(settings.wifi.setup_ap_state.enabled());
    }

    #[test]
    fn brightness_is_clamped_to_safe_ui_range() {
        let mut settings = DeviceSettings::default();

        settings.set_brightness_percent(0);
        assert_eq!(settings.brightness_percent, 10);

        settings.set_brightness_percent(101);
        assert_eq!(settings.brightness_percent, 100);
    }

    #[test]
    fn touch_calibration_maps_normal_axes() {
        let calibration = TouchCalibration::from_four_points(
            RawTouchPoint { x: 400, y: 300 },
            RawTouchPoint { x: 3700, y: 320 },
            RawTouchPoint { x: 3680, y: 3800 },
            RawTouchPoint { x: 420, y: 3780 },
            320,
            240,
        );

        assert_eq!(
            calibration.map(RawTouchPoint { x: 400, y: 300 }),
            ScreenPoint { x: 0, y: 0 }
        );
        assert_eq!(
            calibration.map(RawTouchPoint { x: 3700, y: 3800 }),
            ScreenPoint { x: 319, y: 239 }
        );
    }

    #[test]
    fn touch_calibration_handles_swapped_and_inverted_axes() {
        let calibration = TouchCalibration::from_four_points(
            RawTouchPoint { x: 3800, y: 3700 },
            RawTouchPoint { x: 3780, y: 400 },
            RawTouchPoint { x: 300, y: 420 },
            RawTouchPoint { x: 320, y: 3680 },
            320,
            240,
        );

        assert_eq!(
            calibration.map(RawTouchPoint { x: 3800, y: 3700 }),
            ScreenPoint { x: 0, y: 0 }
        );
        assert_eq!(
            calibration.map(RawTouchPoint { x: 300, y: 420 }),
            ScreenPoint { x: 319, y: 239 }
        );
    }

    #[test]
    fn touch_calibration_maps_landscape_baseline_to_display_rotation() {
        let calibration = TouchCalibration {
            raw_x_min: 0,
            raw_x_max: 319,
            raw_y_min: 0,
            raw_y_max: 239,
            swap_xy: false,
            invert_x: false,
            invert_y: false,
            width: 320,
            height: 240,
        };
        let top_left = RawTouchPoint { x: 0, y: 0 };
        let bottom_right = RawTouchPoint { x: 319, y: 239 };

        assert_eq!(
            calibration.map_for_rotation(top_left, DisplayRotation::Landscape),
            ScreenPoint { x: 0, y: 0 }
        );
        assert_eq!(
            calibration.map_for_rotation(top_left, DisplayRotation::Portrait),
            ScreenPoint { x: 239, y: 0 }
        );
        assert_eq!(
            calibration.map_for_rotation(top_left, DisplayRotation::InvertedPortrait),
            ScreenPoint { x: 0, y: 319 }
        );
        assert_eq!(
            calibration.map_for_rotation(top_left, DisplayRotation::InvertedLandscape),
            ScreenPoint { x: 319, y: 239 }
        );
        assert_eq!(
            calibration.map_for_rotation(bottom_right, DisplayRotation::Portrait),
            ScreenPoint { x: 0, y: 319 }
        );
    }

    #[test]
    fn touch_calibration_maps_portrait_baseline_to_display_rotation() {
        let calibration = TouchCalibration {
            raw_x_min: 0,
            raw_x_max: 239,
            raw_y_min: 0,
            raw_y_max: 319,
            swap_xy: false,
            invert_x: false,
            invert_y: false,
            width: 240,
            height: 320,
        };

        assert_eq!(
            calibration.map_for_rotation(RawTouchPoint { x: 0, y: 0 }, DisplayRotation::Portrait),
            ScreenPoint { x: 0, y: 0 }
        );
        assert_eq!(
            calibration.map_for_rotation(RawTouchPoint { x: 0, y: 0 }, DisplayRotation::Landscape),
            ScreenPoint { x: 0, y: 239 }
        );
        assert_eq!(
            calibration.map_for_rotation(
                RawTouchPoint { x: 239, y: 319 },
                DisplayRotation::InvertedLandscape
            ),
            ScreenPoint { x: 0, y: 239 }
        );
    }

    #[test]
    fn touch_calibration_round_trips_through_bytes() {
        let calibration = TouchCalibration {
            raw_x_min: 321,
            raw_x_max: 3720,
            raw_y_min: 288,
            raw_y_max: 3810,
            swap_xy: true,
            invert_x: false,
            invert_y: true,
            width: 320,
            height: 240,
        };

        let encoded = calibration.encode();
        let decoded = TouchCalibration::decode(&encoded).unwrap();

        assert_eq!(decoded, calibration);
    }

    #[test]
    fn fixed_ascii_rejects_non_ascii_and_oversize_values() {
        assert_eq!(
            FixedAscii::<4>::try_from_str("abc").unwrap().as_str(),
            "abc"
        );
        assert_eq!(
            FixedAscii::<4>::try_from_str("abcde"),
            Err(FixedTextError::TooLong)
        );
        assert_eq!(
            FixedAscii::<8>::try_from_str("中文"),
            Err(FixedTextError::NonAscii)
        );
    }

    #[test]
    fn mac_address_parses_colon_hex() {
        let mac = MacAddress::parse_colon_hex("AA:bb:01:23:45:ff").unwrap();

        assert_eq!(mac.octets, [0xAA, 0xBB, 0x01, 0x23, 0x45, 0xFF]);
        assert_eq!(
            MacAddress::parse_colon_hex("AA-bb-01-23-45-ff"),
            Err(MacAddressError::InvalidSeparator)
        );
    }
}
