use core::str;

use crate::{
    app_state::{AppState, OtaState, WifiLinkState},
    gps_nmea::SpeedUnit,
    settings::{
        DeviceSettings, DisplayRotation, FixedTextError, Language, MacAddress, MacAddressError,
    },
};

pub const INDEX_PATH: &str = "/";
pub const STATUS_PATH: &str = "/api/status";
pub const CONFIG_PATH: &str = "/api/config";
pub const WIFI_PATH: &str = "/api/wifi";
pub const AP_PASSWORD_PATH: &str = "/api/ap-password";
pub const BMS_BIND_PATH: &str = "/api/bms/bind";
pub const BMS_CANDIDATES_PATH: &str = "/api/bms/candidates";
pub const BMS_SCAN_PATH: &str = "/api/bms/scan";
pub const OTA_CHECK_PATH: &str = "/api/ota/check";
pub const OTA_START_PATH: &str = "/api/ota/start";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HttpMethod {
    Get,
    Post,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApiRoute {
    Index,
    Status,
    ConfigRead,
    ConfigWrite,
    WifiUpdate,
    ApPasswordUpdate,
    BmsBind,
    BmsCandidates,
    BmsScan,
    OtaCheck,
    OtaStart,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApiError {
    NotFound,
    MethodNotAllowed,
    InvalidSsid,
    InvalidPassword,
    InvalidBrightness,
    InvalidLanguage,
    InvalidRotation,
    InvalidSpeedUnit,
    InvalidMac,
    InvalidJson,
    ResponseTooLarge,
    Text(FixedTextError),
}

impl From<FixedTextError> for ApiError {
    fn from(value: FixedTextError) -> Self {
        Self::Text(value)
    }
}

impl From<MacAddressError> for ApiError {
    fn from(_: MacAddressError) -> Self {
        Self::InvalidMac
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WifiUpdate<'a> {
    pub ssid: &'a str,
    pub password: &'a str,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceConfigUpdate<'a> {
    pub brightness_percent: Option<u8>,
    pub display_rotation: Option<&'a str>,
    pub speed_unit: Option<&'a str>,
    pub language: Option<&'a str>,
    pub bms_mac: Option<&'a str>,
}

pub struct JsonWriter<'a> {
    bytes: &'a mut [u8],
    len: usize,
}

impl<'a> JsonWriter<'a> {
    pub fn new(bytes: &'a mut [u8]) -> Self {
        Self { bytes, len: 0 }
    }

    pub fn as_str(&self) -> &str {
        str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }

    fn len(&self) -> usize {
        self.len
    }

    fn push(&mut self, value: &str) -> Result<(), ApiError> {
        if self.len + value.len() > self.bytes.len() {
            return Err(ApiError::ResponseTooLarge);
        }
        self.bytes[self.len..self.len + value.len()].copy_from_slice(value.as_bytes());
        self.len += value.len();
        Ok(())
    }

    fn push_byte(&mut self, value: u8) -> Result<(), ApiError> {
        if self.len + 1 > self.bytes.len() {
            return Err(ApiError::ResponseTooLarge);
        }
        self.bytes[self.len] = value;
        self.len += 1;
        Ok(())
    }

    fn string_field(&mut self, name: &str, value: &str, comma: bool) -> Result<(), ApiError> {
        if comma {
            self.push(",")?;
        }
        self.push("\"")?;
        self.push(name)?;
        self.push("\":\"")?;
        self.push_escaped(value)?;
        self.push("\"")
    }

    fn bool_field(&mut self, name: &str, value: bool, comma: bool) -> Result<(), ApiError> {
        if comma {
            self.push(",")?;
        }
        self.push("\"")?;
        self.push(name)?;
        self.push("\":")?;
        self.push(if value { "true" } else { "false" })
    }

    fn u32_field(&mut self, name: &str, value: u32, comma: bool) -> Result<(), ApiError> {
        if comma {
            self.push(",")?;
        }
        self.push("\"")?;
        self.push(name)?;
        self.push("\":")?;
        self.push_u32(value)
    }

    fn i32_field(&mut self, name: &str, value: i32, comma: bool) -> Result<(), ApiError> {
        if comma {
            self.push(",")?;
        }
        self.push("\"")?;
        self.push(name)?;
        self.push("\":")?;
        if value < 0 {
            self.push_byte(b'-')?;
            self.push_u32(value.saturating_abs() as u32)
        } else {
            self.push_u32(value as u32)
        }
    }

    fn null_field(&mut self, name: &str, comma: bool) -> Result<(), ApiError> {
        if comma {
            self.push(",")?;
        }
        self.push("\"")?;
        self.push(name)?;
        self.push("\":null")
    }

    fn push_escaped(&mut self, value: &str) -> Result<(), ApiError> {
        for byte in value.bytes() {
            match byte {
                b'"' => self.push("\\\"")?,
                b'\\' => self.push("\\\\")?,
                0x20..=0x7E => self.push_byte(byte)?,
                _ => return Err(ApiError::InvalidJson),
            }
        }
        Ok(())
    }

    fn push_u32(&mut self, mut value: u32) -> Result<(), ApiError> {
        if value == 0 {
            return self.push_byte(b'0');
        }
        let mut scratch = [0_u8; 10];
        let mut len = 0;
        while value > 0 {
            scratch[len] = b'0' + (value % 10) as u8;
            value /= 10;
            len += 1;
        }
        while len > 0 {
            len -= 1;
            self.push_byte(scratch[len])?;
        }
        Ok(())
    }
}

pub fn route(method: HttpMethod, path: &str) -> Result<ApiRoute, ApiError> {
    match path {
        INDEX_PATH => method_route(method, HttpMethod::Get, ApiRoute::Index),
        STATUS_PATH => method_route(method, HttpMethod::Get, ApiRoute::Status),
        CONFIG_PATH => match method {
            HttpMethod::Get => Ok(ApiRoute::ConfigRead),
            HttpMethod::Post => Ok(ApiRoute::ConfigWrite),
        },
        WIFI_PATH => method_route(method, HttpMethod::Post, ApiRoute::WifiUpdate),
        AP_PASSWORD_PATH => method_route(method, HttpMethod::Post, ApiRoute::ApPasswordUpdate),
        BMS_BIND_PATH => method_route(method, HttpMethod::Post, ApiRoute::BmsBind),
        BMS_CANDIDATES_PATH => method_route(method, HttpMethod::Get, ApiRoute::BmsCandidates),
        BMS_SCAN_PATH => method_route(method, HttpMethod::Post, ApiRoute::BmsScan),
        OTA_CHECK_PATH => method_route(method, HttpMethod::Post, ApiRoute::OtaCheck),
        OTA_START_PATH => method_route(method, HttpMethod::Post, ApiRoute::OtaStart),
        _ => Err(ApiError::NotFound),
    }
}

pub fn write_status_json<'a>(
    output: &'a mut [u8],
    app_state: &AppState,
    firmware_version: &str,
) -> Result<&'a str, ApiError> {
    let snapshot = app_state.dashboard_snapshot();
    let mut writer = JsonWriter::new(output);

    writer.push("{")?;
    writer.string_field("version", firmware_version, false)?;
    match snapshot.speed_deci_units {
        Some(speed) => {
            let speed = DeciText::from_u16(speed);
            writer.string_field("speed", speed.as_str(), true)?;
        }
        None => writer.string_field("speed", "--", true)?,
    }
    writer.string_field("speed_unit", speed_unit_text(snapshot.speed_unit), true)?;
    writer.bool_field("gps_fix", snapshot.gps_fix_valid, true)?;
    writer.string_field(
        "bms",
        if snapshot.bms_online {
            "online"
        } else {
            "offline"
        },
        true,
    )?;
    write_optional_u32(
        &mut writer,
        "pack_voltage_mv",
        snapshot.pack_voltage_mv,
        true,
    )?;
    write_optional_i32(
        &mut writer,
        "current_deci_amps",
        snapshot.current_deci_amps.map(|value| value as i32),
        true,
    )?;
    write_optional_u32(
        &mut writer,
        "soc_percent",
        snapshot.soc_percent.map(|value| value as u32),
        true,
    )?;
    write_optional_u32(
        &mut writer,
        "local_battery_mv",
        snapshot.local_battery_voltage_mv,
        true,
    )?;
    writer.string_field("wifi", wifi_state_text(snapshot.wifi), true)?;
    writer.bool_field("setup_ap_enabled", snapshot.setup_ap_enabled, true)?;
    writer.string_field("ota", ota_state_text(snapshot.ota), true)?;
    writer.push("}")?;

    let len = writer.len();
    Ok(str::from_utf8(&output[..len]).unwrap_or(""))
}

pub fn write_bms_candidates_json<'a>(
    output: &'a mut [u8],
    app_state: &AppState,
) -> Result<&'a str, ApiError> {
    let mut writer = JsonWriter::new(output);

    writer.push("{\"candidates\":[")?;
    for (index, candidate) in app_state.bms_scan_candidates.as_slice().iter().enumerate() {
        if index > 0 {
            writer.push(",")?;
        }
        let mac = mac_to_text(candidate.mac);
        writer.push("{")?;
        writer.string_field("mac", mac.as_str(), false)?;
        match candidate.name {
            Some(name) => writer.string_field("name", name.as_str(), true)?,
            None => writer.null_field("name", true)?,
        }
        writer.i32_field("rssi", candidate.rssi as i32, true)?;
        writer.push("}")?;
    }
    writer.push("]}")?;

    let len = writer.len();
    Ok(str::from_utf8(&output[..len]).unwrap_or(""))
}

pub fn write_config_json<'a>(
    output: &'a mut [u8],
    settings: &DeviceSettings,
) -> Result<&'a str, ApiError> {
    let mut writer = JsonWriter::new(output);

    writer.push("{")?;
    writer.u32_field("brightness", settings.brightness_percent as u32, false)?;
    writer.string_field(
        "display_rotation",
        settings.display_rotation.as_config_value(),
        true,
    )?;
    writer.string_field("speed_unit", speed_unit_text(settings.speed_unit), true)?;
    writer.string_field("language", settings.language.as_config_value(), true)?;
    writer.string_field("setup_ap_ssid", settings.wifi.setup_ap_ssid.as_str(), true)?;
    writer.bool_field(
        "external_wifi_saved",
        settings.wifi.external_wifi_saved,
        true,
    )?;
    writer.string_field("external_ssid", settings.wifi.external_ssid.as_str(), true)?;
    writer.bool_field(
        "setup_ap_password_saved",
        settings.wifi.setup_ap_password_saved,
        true,
    )?;
    writer.string_field("setup_ap_state", setup_ap_state_text(settings), true)?;
    if let Some(mac) = settings.bms.bound_mac {
        let mac = mac_to_text(mac);
        writer.string_field("bms_mac", mac.as_str(), true)?;
    } else {
        writer.null_field("bms_mac", true)?;
    }
    writer.push("}")?;

    let len = writer.len();
    Ok(str::from_utf8(&output[..len]).unwrap_or(""))
}

pub fn parse_wifi_update_json(body: &str) -> Result<WifiUpdate<'_>, ApiError> {
    Ok(WifiUpdate {
        ssid: json_string_field(body, "ssid")?,
        password: json_string_field(body, "password")?,
    })
}

pub fn parse_ap_password_json(body: &str) -> Result<&str, ApiError> {
    json_string_field(body, "password")
}

pub fn parse_bms_bind_json(body: &str) -> Result<&str, ApiError> {
    json_string_field(body, "mac")
}

pub fn parse_config_update_json(body: &str) -> Result<DeviceConfigUpdate<'_>, ApiError> {
    Ok(DeviceConfigUpdate {
        brightness_percent: json_u8_field(body, "brightness")?,
        display_rotation: json_optional_string_field(body, "display_rotation")?,
        speed_unit: json_optional_string_field(body, "speed_unit")?,
        language: json_optional_string_field(body, "language")?,
        bms_mac: json_optional_string_field(body, "bms_mac")?,
    })
}

pub fn apply_wifi_update(
    settings: &mut DeviceSettings,
    update: WifiUpdate<'_>,
) -> Result<(), ApiError> {
    validate_ssid(update.ssid)?;
    validate_external_wifi_password(update.password)?;
    settings
        .wifi
        .set_external_credentials(update.ssid, update.password)?;
    Ok(())
}

pub fn apply_ap_password_update(
    settings: &mut DeviceSettings,
    password: &str,
) -> Result<(), ApiError> {
    validate_setup_ap_password(password)?;
    settings.wifi.set_setup_ap_password(password)?;
    Ok(())
}

pub fn apply_config_update(
    settings: &mut DeviceSettings,
    update: DeviceConfigUpdate<'_>,
) -> Result<(), ApiError> {
    if let Some(brightness) = update.brightness_percent {
        if !(10..=100).contains(&brightness) {
            return Err(ApiError::InvalidBrightness);
        }
        settings.brightness_percent = brightness;
    }
    if let Some(rotation) = update.display_rotation {
        settings.display_rotation = parse_display_rotation(rotation)?;
    }
    if let Some(unit) = update.speed_unit {
        settings.speed_unit = parse_speed_unit(unit)?;
    }
    if let Some(language) = update.language {
        settings.language = parse_language(language)?;
    }
    if let Some(mac) = update.bms_mac {
        settings.bms.bound_mac = if mac.is_empty() {
            None
        } else {
            Some(MacAddress::parse_colon_hex(mac)?)
        };
    }

    Ok(())
}

pub fn apply_bms_bind(settings: &mut DeviceSettings, mac: &str) -> Result<(), ApiError> {
    settings.bms.bound_mac = Some(MacAddress::parse_colon_hex(mac)?);
    Ok(())
}

fn write_optional_u32(
    writer: &mut JsonWriter<'_>,
    name: &str,
    value: Option<u32>,
    comma: bool,
) -> Result<(), ApiError> {
    match value {
        Some(value) => writer.u32_field(name, value, comma),
        None => writer.null_field(name, comma),
    }
}

fn write_optional_i32(
    writer: &mut JsonWriter<'_>,
    name: &str,
    value: Option<i32>,
    comma: bool,
) -> Result<(), ApiError> {
    match value {
        Some(value) => writer.i32_field(name, value, comma),
        None => writer.null_field(name, comma),
    }
}

fn speed_unit_text(unit: SpeedUnit) -> &'static str {
    match unit {
        SpeedUnit::Kmh => "km/h",
        SpeedUnit::Mph => "mph",
    }
}

fn wifi_state_text(state: WifiLinkState) -> &'static str {
    match state {
        WifiLinkState::SetupApOnly => "setup_ap",
        WifiLinkState::StationConnecting => "connecting",
        WifiLinkState::StationConnected => "connected",
        WifiLinkState::Offline => "offline",
    }
}

fn ota_state_text(state: OtaState) -> &'static str {
    match state {
        OtaState::Idle => "idle",
        OtaState::Checking => "checking",
        OtaState::UpdateAvailable => "update_available",
        OtaState::Downloading => "downloading",
        OtaState::Verifying => "verifying",
        OtaState::ReadyToReboot => "ready_to_reboot",
        OtaState::Failed => "failed",
    }
}

fn setup_ap_state_text(settings: &DeviceSettings) -> &'static str {
    match settings.wifi.setup_ap_state {
        crate::settings::SetupApState::FirstBoot => "first_boot",
        crate::settings::SetupApState::Disabled => "disabled",
        crate::settings::SetupApState::Reprovisioning => "reprovisioning",
    }
}

struct MacText {
    bytes: [u8; 17],
}

struct DeciText {
    bytes: [u8; 8],
    len: usize,
}

impl DeciText {
    fn from_u16(value: u16) -> Self {
        let mut bytes = [0_u8; 8];
        let mut len = write_decimal_u32(&mut bytes, (value / 10) as u32);
        bytes[len] = b'.';
        bytes[len + 1] = b'0' + (value % 10) as u8;
        len += 2;
        Self { bytes, len }
    }

    fn as_str(&self) -> &str {
        str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }
}

impl MacText {
    fn as_str(&self) -> &str {
        str::from_utf8(&self.bytes).unwrap_or("")
    }
}

fn write_decimal_u32(output: &mut [u8], mut value: u32) -> usize {
    if value == 0 {
        output[0] = b'0';
        return 1;
    }

    let mut scratch = [0_u8; 10];
    let mut len = 0;
    while value > 0 {
        scratch[len] = b'0' + (value % 10) as u8;
        value /= 10;
        len += 1;
    }
    let mut out = 0;
    while len > 0 {
        len -= 1;
        output[out] = scratch[len];
        out += 1;
    }
    out
}

fn mac_to_text(mac: MacAddress) -> MacText {
    let mut bytes = [0_u8; 17];
    let mut cursor = 0;
    for (index, octet) in mac.octets.iter().copied().enumerate() {
        if index > 0 {
            bytes[cursor] = b':';
            cursor += 1;
        }
        bytes[cursor] = hex_char(octet >> 4);
        bytes[cursor + 1] = hex_char(octet & 0x0F);
        cursor += 2;
    }
    MacText { bytes }
}

fn hex_char(value: u8) -> u8 {
    match value {
        0..=9 => b'0' + value,
        _ => b'A' + value - 10,
    }
}

fn json_string_field<'a>(body: &'a str, name: &str) -> Result<&'a str, ApiError> {
    json_optional_string_field(body, name)?.ok_or(ApiError::InvalidJson)
}

fn json_optional_string_field<'a>(body: &'a str, name: &str) -> Result<Option<&'a str>, ApiError> {
    let Some(mut index) = json_value_start(body, name) else {
        return Ok(None);
    };
    let bytes = body.as_bytes();
    if bytes.get(index) == Some(&b'n') && body[index..].starts_with("null") {
        return Ok(None);
    }
    if bytes.get(index) != Some(&b'"') {
        return Err(ApiError::InvalidJson);
    }
    index += 1;
    let start = index;
    while index < bytes.len() {
        match bytes[index] {
            b'"' => return Ok(Some(&body[start..index])),
            b'\\' => return Err(ApiError::InvalidJson),
            0x20..=0x7E => index += 1,
            _ => return Err(ApiError::InvalidJson),
        }
    }
    Err(ApiError::InvalidJson)
}

fn json_u8_field(body: &str, name: &str) -> Result<Option<u8>, ApiError> {
    let Some(mut index) = json_value_start(body, name) else {
        return Ok(None);
    };
    let bytes = body.as_bytes();
    let start = index;
    while index < bytes.len() && bytes[index].is_ascii_digit() {
        index += 1;
    }
    if start == index {
        return Err(ApiError::InvalidJson);
    }
    let value = body[start..index]
        .parse::<u16>()
        .map_err(|_| ApiError::InvalidJson)?;
    if value > u8::MAX as u16 {
        return Err(ApiError::InvalidJson);
    }
    Ok(Some(value as u8))
}

fn json_value_start(body: &str, name: &str) -> Option<usize> {
    let bytes = body.as_bytes();
    let name_bytes = name.as_bytes();
    let mut index = 0;
    while index + name_bytes.len() + 2 <= bytes.len() {
        if bytes[index] == b'"'
            && bytes[index + 1..].starts_with(name_bytes)
            && bytes.get(index + 1 + name_bytes.len()) == Some(&b'"')
        {
            let mut cursor = index + 2 + name_bytes.len();
            skip_ws(bytes, &mut cursor);
            if bytes.get(cursor) == Some(&b':') {
                cursor += 1;
                skip_ws(bytes, &mut cursor);
                return Some(cursor);
            }
        }
        index += 1;
    }
    None
}

fn skip_ws(bytes: &[u8], index: &mut usize) {
    while *index < bytes.len() && matches!(bytes[*index], b' ' | b'\n' | b'\r' | b'\t') {
        *index += 1;
    }
}

pub fn parse_display_rotation(value: &str) -> Result<DisplayRotation, ApiError> {
    match value {
        "portrait" => Ok(DisplayRotation::Portrait),
        "landscape" => Ok(DisplayRotation::Landscape),
        "inverted_portrait" => Ok(DisplayRotation::InvertedPortrait),
        "inverted_landscape" => Ok(DisplayRotation::InvertedLandscape),
        _ => Err(ApiError::InvalidRotation),
    }
}

pub fn parse_speed_unit(value: &str) -> Result<SpeedUnit, ApiError> {
    match value {
        "km/h" | "kmh" => Ok(SpeedUnit::Kmh),
        "mph" => Ok(SpeedUnit::Mph),
        _ => Err(ApiError::InvalidSpeedUnit),
    }
}

pub fn parse_language(value: &str) -> Result<Language, ApiError> {
    match value {
        "zh" | "zh-CN" => Ok(Language::Chinese),
        "en" | "en-US" => Ok(Language::English),
        _ => Err(ApiError::InvalidLanguage),
    }
}

fn method_route(
    actual: HttpMethod,
    expected: HttpMethod,
    route: ApiRoute,
) -> Result<ApiRoute, ApiError> {
    if actual == expected {
        Ok(route)
    } else {
        Err(ApiError::MethodNotAllowed)
    }
}

fn validate_ssid(ssid: &str) -> Result<(), ApiError> {
    if ssid.is_empty() || ssid.len() > 32 || !ssid.is_ascii() {
        return Err(ApiError::InvalidSsid);
    }
    Ok(())
}

fn validate_external_wifi_password(password: &str) -> Result<(), ApiError> {
    if password.len() > 64 || (!password.is_empty() && password.len() < 8) || !password.is_ascii() {
        return Err(ApiError::InvalidPassword);
    }
    Ok(())
}

fn validate_setup_ap_password(password: &str) -> Result<(), ApiError> {
    if !(8..=64).contains(&password.len()) || !password.is_ascii() {
        return Err(ApiError::InvalidPassword);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        ant_bms::{
            BalancerStatus, BatteryStatus, BmsTelemetry, MAX_CELLS, MAX_TEMPERATURES, MosfetStatus,
        },
        app_state::AppState,
        gps_nmea::GpsFix,
        settings::SetupApState,
    };

    #[test]
    fn routes_known_paths_by_method() {
        assert_eq!(route(HttpMethod::Get, INDEX_PATH), Ok(ApiRoute::Index));
        assert_eq!(route(HttpMethod::Get, STATUS_PATH), Ok(ApiRoute::Status));
        assert_eq!(
            route(HttpMethod::Get, CONFIG_PATH),
            Ok(ApiRoute::ConfigRead)
        );
        assert_eq!(
            route(HttpMethod::Post, CONFIG_PATH),
            Ok(ApiRoute::ConfigWrite)
        );
        assert_eq!(
            route(HttpMethod::Get, WIFI_PATH),
            Err(ApiError::MethodNotAllowed)
        );
        assert_eq!(
            route(HttpMethod::Get, BMS_CANDIDATES_PATH),
            Ok(ApiRoute::BmsCandidates)
        );
        assert_eq!(
            route(HttpMethod::Post, BMS_SCAN_PATH),
            Ok(ApiRoute::BmsScan)
        );
        assert_eq!(route(HttpMethod::Post, "/missing"), Err(ApiError::NotFound));
    }

    #[test]
    fn applies_wifi_and_ap_password_updates() {
        let mut settings = DeviceSettings::default();

        apply_wifi_update(
            &mut settings,
            WifiUpdate {
                ssid: "garage",
                password: "secretpass",
            },
        )
        .unwrap();
        apply_ap_password_update(&mut settings, "setup1234").unwrap();

        assert!(settings.wifi.external_wifi_saved);
        assert_eq!(settings.wifi.external_ssid.as_str(), "garage");
        assert_eq!(settings.wifi.external_password.as_str(), "secretpass");
        assert_eq!(settings.wifi.setup_ap_password.as_str(), "setup1234");
    }

    #[test]
    fn applies_device_config_update() {
        let mut settings = DeviceSettings::default();

        apply_config_update(
            &mut settings,
            DeviceConfigUpdate {
                brightness_percent: Some(42),
                display_rotation: Some("portrait"),
                speed_unit: Some("mph"),
                language: Some("en"),
                bms_mac: Some("AA:BB:CC:DD:EE:FF"),
            },
        )
        .unwrap();

        assert_eq!(settings.brightness_percent, 42);
        assert_eq!(settings.display_rotation, DisplayRotation::Portrait);
        assert_eq!(settings.speed_unit, SpeedUnit::Mph);
        assert_eq!(settings.language, Language::English);
        assert_eq!(
            settings.bms.bound_mac.unwrap().octets,
            [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]
        );
    }

    #[test]
    fn rejects_invalid_settings_payloads() {
        let mut settings = DeviceSettings::default();

        assert_eq!(
            apply_wifi_update(
                &mut settings,
                WifiUpdate {
                    ssid: "",
                    password: "secretpass"
                }
            ),
            Err(ApiError::InvalidSsid)
        );
        assert_eq!(
            apply_ap_password_update(&mut settings, "short"),
            Err(ApiError::InvalidPassword)
        );
        assert_eq!(
            apply_config_update(
                &mut settings,
                DeviceConfigUpdate {
                    brightness_percent: Some(9),
                    display_rotation: None,
                    speed_unit: None,
                    language: None,
                    bms_mac: None,
                }
            ),
            Err(ApiError::InvalidBrightness)
        );
    }

    #[test]
    fn writes_status_json_from_app_state() {
        let mut state = AppState::default();
        state.gps.update_fix(GpsFix {
            valid: true,
            speed_knots: 10.0,
        });
        state.update_battery_raw(
            2048,
            crate::battery::BatterySenseConfig::new(4095, 3300, 100_000, 100_000),
        );
        state.bms.update_telemetry(sample_telemetry());
        let mut out = [0_u8; 512];

        let json = write_status_json(&mut out, &state, "0.1.0").unwrap();

        assert!(json.contains(r#""version":"0.1.0""#));
        assert!(json.contains(r#""speed":"18.5""#));
        assert!(json.contains(r#""speed_unit":"km/h""#));
        assert!(json.contains(r#""gps_fix":true"#));
        assert!(json.contains(r#""pack_voltage_mv":52840"#));
        assert!(json.contains(r#""local_battery_mv":3300"#));
        assert!(json.contains(r#""setup_ap_enabled":true"#));
    }

    #[test]
    fn writes_config_json_without_plaintext_passwords() {
        let mut settings = DeviceSettings::default();
        settings.wifi.setup_ap_state = SetupApState::Disabled;
        settings
            .wifi
            .set_external_credentials("garage", "secretpass")
            .unwrap();
        settings.wifi.set_setup_ap_password("setup1234").unwrap();
        settings.bms.bound_mac = Some(MacAddress::new([1, 2, 3, 4, 5, 6]));
        let mut out = [0_u8; 512];

        let json = write_config_json(&mut out, &settings).unwrap();

        assert!(json.contains(r#""brightness":80"#));
        assert!(json.contains(r#""language":"zh""#));
        assert!(json.contains(r#""external_ssid":"garage""#));
        assert!(json.contains(r#""setup_ap_password_saved":true"#));
        assert!(json.contains(r#""bms_mac":"01:02:03:04:05:06""#));
        assert!(!json.contains("secretpass"));
        assert!(!json.contains("setup1234"));
    }

    #[test]
    fn writes_bms_candidates_json_with_exact_fields() {
        let mut state = AppState::default();
        state.bms_scan_candidates.upsert(
            MacAddress::new([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]),
            Some("ANT-24S"),
            -58,
        );
        state
            .bms_scan_candidates
            .upsert(MacAddress::new([1, 2, 3, 4, 5, 6]), None, -70);
        let mut out = [0_u8; 256];

        let json = write_bms_candidates_json(&mut out, &state).unwrap();

        assert_eq!(
            json,
            r#"{"candidates":[{"mac":"AA:BB:CC:DD:EE:FF","name":"ANT-24S","rssi":-58},{"mac":"01:02:03:04:05:06","name":null,"rssi":-70}]}"#
        );
    }

    #[test]
    fn parses_json_request_bodies() {
        let wifi = parse_wifi_update_json(r#"{"ssid":"garage","password":"secretpass"}"#).unwrap();
        assert_eq!(wifi.ssid, "garage");
        assert_eq!(wifi.password, "secretpass");

        assert_eq!(
            parse_ap_password_json(r#"{"password":"setup1234"}"#).unwrap(),
            "setup1234"
        );
        assert_eq!(
            parse_bms_bind_json(r#"{"mac":"AA:BB:CC:DD:EE:FF"}"#).unwrap(),
            "AA:BB:CC:DD:EE:FF"
        );

        let config = parse_config_update_json(
            r#"{"brightness":42,"display_rotation":"portrait","speed_unit":"mph","language":"en","bms_mac":null}"#,
        )
        .unwrap();
        assert_eq!(config.brightness_percent, Some(42));
        assert_eq!(config.display_rotation, Some("portrait"));
        assert_eq!(config.speed_unit, Some("mph"));
        assert_eq!(config.language, Some("en"));
        assert_eq!(config.bms_mac, None);
    }

    fn sample_telemetry() -> BmsTelemetry {
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
            temperature_count: 0,
            pack_voltage_mv: 52_840,
            current_deci_amps: 3,
            soc_percent: 91,
            state_of_health_percent: 100,
            charge_mosfet: MosfetStatus::On,
            discharge_mosfet: MosfetStatus::On,
            balancer: BalancerStatus::Off,
            total_capacity_mah: 280_000,
            capacity_remaining_mah: 252_602,
            cycle_capacity_mah: 4_862_650,
            power_watts: 15,
            total_runtime_seconds: 1,
            balanced_cell_bitmask: 0,
            max_cell_voltage_mv: 3305,
            max_voltage_cell: 2,
            min_cell_voltage_mv: 3300,
            min_voltage_cell: 1,
            delta_cell_voltage_mv: 5,
            average_cell_voltage_mv: 3302,
        }
    }
}
