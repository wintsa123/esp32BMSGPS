use crate::{
    gps_nmea::SpeedUnit,
    settings::{
        BmsSettings, DeviceSettings, DisplayRotation, FixedAscii, Language, MacAddress,
        SetupApState, TouchCalibration, WifiSettings,
    },
};

const MAGIC: [u8; 4] = *b"DSET";
const VERSION: u8 = 1;
const HEADER_LEN: usize = 11;
const PAYLOAD_LEN: usize = 256;
const CHECKSUM_LEN: usize = 4;
pub const RECORD_LEN: usize = HEADER_LEN + PAYLOAD_LEN + CHECKSUM_LEN;
const CHECKSUM_OFFSET: usize = HEADER_LEN + PAYLOAD_LEN;

const FLAG_EXTERNAL_WIFI_SAVED: u8 = 0x01;
const FLAG_SETUP_AP_PASSWORD_SAVED: u8 = 0x02;
const FLAG_BMS_MAC: u8 = 0x04;
const FLAG_TOUCH_CALIBRATION: u8 = 0x08;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StoredSettings {
    pub settings: DeviceSettings,
    pub generation: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SettingsRecordError {
    BadMagic,
    BadVersion,
    BadLength,
    BadChecksum,
    BadValue,
}

pub fn encode_record(settings: &DeviceSettings, generation: u32) -> [u8; RECORD_LEN] {
    let mut record = [0xFF_u8; RECORD_LEN];
    record[..4].copy_from_slice(&MAGIC);
    record[4] = VERSION;
    record[5..9].copy_from_slice(&generation.to_le_bytes());
    record[9..11].copy_from_slice(&(PAYLOAD_LEN as u16).to_le_bytes());

    let mut payload = [0_u8; PAYLOAD_LEN];
    encode_payload(settings, &mut payload);
    record[HEADER_LEN..CHECKSUM_OFFSET].copy_from_slice(&payload);

    let checksum = checksum(&record[..CHECKSUM_OFFSET]).to_le_bytes();
    record[CHECKSUM_OFFSET..].copy_from_slice(&checksum);
    record
}

pub fn decode_record(
    record: &[u8; RECORD_LEN],
) -> Result<Option<StoredSettings>, SettingsRecordError> {
    if is_erased(record) {
        return Ok(None);
    }
    if record[..4] != MAGIC {
        return Err(SettingsRecordError::BadMagic);
    }
    if record[4] != VERSION {
        return Err(SettingsRecordError::BadVersion);
    }

    let generation = u32::from_le_bytes([record[5], record[6], record[7], record[8]]);
    let payload_len = u16::from_le_bytes([record[9], record[10]]) as usize;
    if payload_len != PAYLOAD_LEN {
        return Err(SettingsRecordError::BadLength);
    }

    let stored_checksum = u32::from_le_bytes([
        record[CHECKSUM_OFFSET],
        record[CHECKSUM_OFFSET + 1],
        record[CHECKSUM_OFFSET + 2],
        record[CHECKSUM_OFFSET + 3],
    ]);
    if checksum(&record[..CHECKSUM_OFFSET]) != stored_checksum {
        return Err(SettingsRecordError::BadChecksum);
    }

    let settings = decode_payload(&record[HEADER_LEN..CHECKSUM_OFFSET])?;
    Ok(Some(StoredSettings {
        settings,
        generation,
    }))
}

pub fn select_latest(
    left: &[u8; RECORD_LEN],
    right: &[u8; RECORD_LEN],
) -> Result<Option<StoredSettings>, SettingsRecordError> {
    let left = decode_record(left).unwrap_or(None);
    let right = decode_record(right).unwrap_or(None);

    match (left, right) {
        (Some(left), Some(right)) => {
            if left.generation >= right.generation {
                Ok(Some(left))
            } else {
                Ok(Some(right))
            }
        }
        (Some(settings), None) | (None, Some(settings)) => Ok(Some(settings)),
        (None, None) => Ok(None),
    }
}

fn encode_payload(settings: &DeviceSettings, payload: &mut [u8; PAYLOAD_LEN]) {
    let mut cursor = 0;
    payload[cursor] = rotation_code(settings.display_rotation);
    cursor += 1;
    payload[cursor] = settings.brightness_percent;
    cursor += 1;
    payload[cursor] = speed_unit_code(settings.speed_unit);
    cursor += 1;
    payload[cursor] = setup_ap_state_code(settings.wifi.setup_ap_state);
    cursor += 1;

    let mut flags = 0_u8;
    if settings.wifi.external_wifi_saved {
        flags |= FLAG_EXTERNAL_WIFI_SAVED;
    }
    if settings.wifi.setup_ap_password_saved {
        flags |= FLAG_SETUP_AP_PASSWORD_SAVED;
    }
    if settings.bms.bound_mac.is_some() {
        flags |= FLAG_BMS_MAC;
    }
    if settings.touch_calibration.is_some() {
        flags |= FLAG_TOUCH_CALIBRATION;
    }
    payload[cursor] = flags;
    cursor += 1;

    write_fixed::<32>(payload, &mut cursor, settings.wifi.setup_ap_ssid);
    write_fixed::<64>(payload, &mut cursor, settings.wifi.setup_ap_password);
    write_fixed::<32>(payload, &mut cursor, settings.wifi.external_ssid);
    write_fixed::<64>(payload, &mut cursor, settings.wifi.external_password);

    if let Some(mac) = settings.bms.bound_mac {
        payload[cursor..cursor + 6].copy_from_slice(&mac.octets);
    }
    cursor += 6;

    if let Some(calibration) = settings.touch_calibration {
        payload[cursor..cursor + TouchCalibration::ENCODED_LEN]
            .copy_from_slice(&calibration.encode());
    }
    cursor += TouchCalibration::ENCODED_LEN;

    payload[cursor] = language_code(settings.language);
}

fn decode_payload(payload: &[u8]) -> Result<DeviceSettings, SettingsRecordError> {
    if payload.len() != PAYLOAD_LEN {
        return Err(SettingsRecordError::BadLength);
    }

    let mut cursor = 0;
    let display_rotation = decode_rotation(payload[cursor])?;
    cursor += 1;
    let brightness_percent = payload[cursor];
    if !(10..=100).contains(&brightness_percent) {
        return Err(SettingsRecordError::BadValue);
    }
    cursor += 1;
    let speed_unit = decode_speed_unit(payload[cursor])?;
    cursor += 1;
    let setup_ap_state = decode_setup_ap_state(payload[cursor])?;
    cursor += 1;
    let flags = payload[cursor];
    cursor += 1;

    let setup_ap_ssid = read_fixed::<32>(payload, &mut cursor)?;
    let setup_ap_password = read_fixed::<64>(payload, &mut cursor)?;
    let external_ssid = read_fixed::<32>(payload, &mut cursor)?;
    let external_password = read_fixed::<64>(payload, &mut cursor)?;

    let bound_mac = if flags & FLAG_BMS_MAC != 0 {
        let mut octets = [0_u8; 6];
        octets.copy_from_slice(&payload[cursor..cursor + 6]);
        Some(MacAddress::new(octets))
    } else {
        None
    };
    cursor += 6;

    let touch_calibration = if flags & FLAG_TOUCH_CALIBRATION != 0 {
        let mut encoded = [0_u8; TouchCalibration::ENCODED_LEN];
        encoded.copy_from_slice(&payload[cursor..cursor + TouchCalibration::ENCODED_LEN]);
        TouchCalibration::decode(&encoded)
    } else {
        None
    };
    if flags & FLAG_TOUCH_CALIBRATION != 0 && touch_calibration.is_none() {
        return Err(SettingsRecordError::BadValue);
    }
    cursor += TouchCalibration::ENCODED_LEN;

    let language = decode_language(payload[cursor])?;

    Ok(DeviceSettings {
        display_rotation,
        brightness_percent,
        speed_unit,
        language,
        wifi: WifiSettings {
            setup_ap_state,
            external_wifi_saved: flags & FLAG_EXTERNAL_WIFI_SAVED != 0,
            setup_ap_password_saved: flags & FLAG_SETUP_AP_PASSWORD_SAVED != 0,
            setup_ap_ssid,
            setup_ap_password,
            external_ssid,
            external_password,
        },
        bms: BmsSettings { bound_mac },
        touch_calibration,
    })
}

fn write_fixed<const N: usize>(
    payload: &mut [u8; PAYLOAD_LEN],
    cursor: &mut usize,
    value: FixedAscii<N>,
) {
    payload[*cursor] = value.len() as u8;
    *cursor += 1;
    let bytes = value.raw_bytes();
    payload[*cursor..*cursor + N].copy_from_slice(&bytes);
    *cursor += N;
}

fn read_fixed<const N: usize>(
    payload: &[u8],
    cursor: &mut usize,
) -> Result<FixedAscii<N>, SettingsRecordError> {
    let len = payload[*cursor] as usize;
    *cursor += 1;
    if len > N || *cursor + N > payload.len() {
        return Err(SettingsRecordError::BadLength);
    }
    let value = FixedAscii::from_ascii_bytes(&payload[*cursor..*cursor + len])
        .map_err(|_| SettingsRecordError::BadValue)?;
    *cursor += N;
    Ok(value)
}

fn rotation_code(rotation: DisplayRotation) -> u8 {
    match rotation {
        DisplayRotation::Portrait => 0,
        DisplayRotation::Landscape => 1,
        DisplayRotation::InvertedPortrait => 2,
        DisplayRotation::InvertedLandscape => 3,
    }
}

fn decode_rotation(code: u8) -> Result<DisplayRotation, SettingsRecordError> {
    match code {
        0 => Ok(DisplayRotation::Portrait),
        1 => Ok(DisplayRotation::Landscape),
        2 => Ok(DisplayRotation::InvertedPortrait),
        3 => Ok(DisplayRotation::InvertedLandscape),
        _ => Err(SettingsRecordError::BadValue),
    }
}

fn speed_unit_code(unit: SpeedUnit) -> u8 {
    match unit {
        SpeedUnit::Kmh => 0,
        SpeedUnit::Mph => 1,
    }
}

fn decode_speed_unit(code: u8) -> Result<SpeedUnit, SettingsRecordError> {
    match code {
        0 => Ok(SpeedUnit::Kmh),
        1 => Ok(SpeedUnit::Mph),
        _ => Err(SettingsRecordError::BadValue),
    }
}

fn language_code(language: Language) -> u8 {
    match language {
        Language::Chinese => 0,
        Language::English => 1,
    }
}

fn decode_language(code: u8) -> Result<Language, SettingsRecordError> {
    match code {
        0 => Ok(Language::Chinese),
        1 => Ok(Language::English),
        _ => Err(SettingsRecordError::BadValue),
    }
}

fn setup_ap_state_code(state: SetupApState) -> u8 {
    match state {
        SetupApState::FirstBoot => 0,
        SetupApState::Disabled => 1,
        SetupApState::Reprovisioning => 2,
    }
}

fn decode_setup_ap_state(code: u8) -> Result<SetupApState, SettingsRecordError> {
    match code {
        0 => Ok(SetupApState::FirstBoot),
        1 => Ok(SetupApState::Disabled),
        2 => Ok(SetupApState::Reprovisioning),
        _ => Err(SettingsRecordError::BadValue),
    }
}

fn is_erased(bytes: &[u8]) -> bool {
    bytes.iter().all(|&byte| byte == 0xFF)
}

fn checksum(bytes: &[u8]) -> u32 {
    let mut hash = 0x811C_9DC5_u32;
    for &byte in bytes {
        hash ^= byte as u32;
        hash = hash.wrapping_mul(0x0100_0193);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_settings() -> DeviceSettings {
        let mut settings = DeviceSettings {
            display_rotation: DisplayRotation::InvertedLandscape,
            brightness_percent: 66,
            speed_unit: SpeedUnit::Mph,
            language: Language::English,
            ..DeviceSettings::default()
        };
        settings
            .wifi
            .set_external_credentials("garage", "secretpass")
            .unwrap();
        settings.wifi.set_setup_ap_password("setup1234").unwrap();
        settings.bms.bound_mac = Some(MacAddress::new([1, 2, 3, 4, 5, 6]));
        settings.touch_calibration = Some(TouchCalibration {
            raw_x_min: 453,
            raw_x_max: 3549,
            raw_y_min: 613,
            raw_y_max: 3485,
            swap_xy: true,
            invert_x: false,
            invert_y: false,
            width: 320,
            height: 240,
        });
        settings
    }

    #[test]
    fn settings_record_round_trips() {
        let settings = sample_settings();
        let record = encode_record(&settings, 42);
        let decoded = decode_record(&record).unwrap().unwrap();

        assert_eq!(decoded.generation, 42);
        assert_eq!(decoded.settings, settings);
    }

    #[test]
    fn select_latest_uses_highest_generation_valid_record() {
        let older = encode_record(&DeviceSettings::default(), 1);
        let newer = encode_record(&sample_settings(), 2);

        let selected = select_latest(&older, &newer).unwrap().unwrap();

        assert_eq!(selected.generation, 2);
        assert_eq!(selected.settings.brightness_percent, 66);
    }

    #[test]
    fn corrupt_record_is_rejected() {
        let mut record = encode_record(&sample_settings(), 1);
        record[20] ^= 0x55;

        assert_eq!(
            decode_record(&record),
            Err(SettingsRecordError::BadChecksum)
        );
    }

    #[test]
    fn erased_record_is_empty() {
        let record = [0xFF_u8; RECORD_LEN];

        assert_eq!(decode_record(&record), Ok(None));
    }

    #[test]
    fn select_latest_ignores_one_corrupt_record() {
        let mut corrupt_newer = encode_record(&sample_settings(), 9);
        corrupt_newer[20] ^= 0x55;
        let valid_older = encode_record(&DeviceSettings::default(), 1);

        let selected = select_latest(&corrupt_newer, &valid_older)
            .unwrap()
            .unwrap();

        assert_eq!(selected.generation, 1);
        assert_eq!(selected.settings, DeviceSettings::default());
    }
}
