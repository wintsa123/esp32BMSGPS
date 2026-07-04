use crate::settings::{FixedAscii, FixedTextError};

pub const CURRENT_SETUP_AP_SSID_PREFIX: &str = "fuckingBms_";
pub const SETUP_AP_PASSWORD_LEN: usize = 8;
pub const SETUP_AP_SSID_RANDOM_SUFFIX_LEN: usize = 6;

const PASSWORD_ALPHABET: &[u8] = b"0123456789";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProvisioningError {
    MissingRandomness,
    Text(FixedTextError),
    QrPayloadTooLong,
}

impl From<FixedTextError> for ProvisioningError {
    fn from(value: FixedTextError) -> Self {
        Self::Text(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WifiQrPayload {
    bytes: [u8; 160],
    len: usize,
}

impl WifiQrPayload {
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }
}

pub fn generate_setup_ap_ssid(random_bytes: &[u8]) -> Result<FixedAscii<32>, ProvisioningError> {
    if random_bytes.is_empty() {
        return Err(ProvisioningError::MissingRandomness);
    }

    let mut bytes = [0_u8; CURRENT_SETUP_AP_SSID_PREFIX.len() + SETUP_AP_SSID_RANDOM_SUFFIX_LEN];
    bytes[..CURRENT_SETUP_AP_SSID_PREFIX.len()]
        .copy_from_slice(CURRENT_SETUP_AP_SSID_PREFIX.as_bytes());
    let suffix = seed_from_bytes(random_bytes) as u32 & 0x00FF_FFFF;
    write_hex_u24(suffix, &mut bytes[CURRENT_SETUP_AP_SSID_PREFIX.len()..]);
    Ok(FixedAscii::from_ascii_bytes(&bytes)?)
}

pub fn setup_ap_ssid_matches_policy(ssid: FixedAscii<32>) -> bool {
    let bytes = ssid.as_bytes();
    bytes.len() == CURRENT_SETUP_AP_SSID_PREFIX.len() + SETUP_AP_SSID_RANDOM_SUFFIX_LEN
        && bytes.starts_with(CURRENT_SETUP_AP_SSID_PREFIX.as_bytes())
        && bytes[CURRENT_SETUP_AP_SSID_PREFIX.len()..]
            .iter()
            .all(|byte| byte.is_ascii_hexdigit())
}

pub fn generate_setup_ap_password(
    random_bytes: &[u8],
) -> Result<FixedAscii<64>, ProvisioningError> {
    if random_bytes.is_empty() {
        return Err(ProvisioningError::MissingRandomness);
    }

    let mut password = [0_u8; SETUP_AP_PASSWORD_LEN];
    let mut state = seed_from_bytes(random_bytes);
    let mut index = 0;
    while index < password.len() {
        state = xorshift64(state);
        let alphabet_index = state as usize % PASSWORD_ALPHABET.len();
        password[index] = PASSWORD_ALPHABET[alphabet_index];
        index += 1;
    }

    Ok(FixedAscii::from_ascii_bytes(&password)?)
}

pub fn setup_ap_password_matches_policy(password: FixedAscii<64>) -> bool {
    password.len() == SETUP_AP_PASSWORD_LEN
        && password.as_bytes().iter().all(|byte| byte.is_ascii_digit())
}

pub fn wifi_qr_payload(
    ssid: FixedAscii<32>,
    password: FixedAscii<64>,
) -> Result<WifiQrPayload, ProvisioningError> {
    let mut payload = WifiQrPayload {
        bytes: [0; 160],
        len: 0,
    };

    push(&mut payload, b"WIFI:S:")?;
    push_escaped(&mut payload, ssid.as_bytes())?;
    push(&mut payload, b";T:WPA;P:")?;
    push_escaped(&mut payload, password.as_bytes())?;
    push(&mut payload, b";;")?;

    Ok(payload)
}

fn push(payload: &mut WifiQrPayload, bytes: &[u8]) -> Result<(), ProvisioningError> {
    if payload.len + bytes.len() > payload.bytes.len() {
        return Err(ProvisioningError::QrPayloadTooLong);
    }
    payload.bytes[payload.len..payload.len + bytes.len()].copy_from_slice(bytes);
    payload.len += bytes.len();
    Ok(())
}

fn push_escaped(payload: &mut WifiQrPayload, bytes: &[u8]) -> Result<(), ProvisioningError> {
    for &byte in bytes {
        if matches!(byte, b'\\' | b';' | b',' | b':') {
            push(payload, b"\\")?;
        }
        push(payload, &[byte])?;
    }
    Ok(())
}

fn seed_from_bytes(bytes: &[u8]) -> u64 {
    let mut seed = 0x9E37_79B9_7F4A_7C15_u64;
    for &byte in bytes {
        seed ^= byte as u64;
        seed = seed.wrapping_mul(0xBF58_476D_1CE4_E5B9);
        seed ^= seed >> 27;
    }
    if seed == 0 { 1 } else { seed }
}

fn xorshift64(mut value: u64) -> u64 {
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    value
}

fn write_hex_u24(value: u32, out: &mut [u8]) {
    let mut shift = 20;
    let mut index = 0;
    while index < 6 {
        let nibble = ((value >> shift) & 0x0F) as u8;
        out[index] = if nibble < 10 {
            b'0' + nibble
        } else {
            b'A' + nibble - 10
        };
        if shift >= 4 {
            shift -= 4;
        }
        index += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generates_setup_ssid_from_random_bytes() {
        let ssid = generate_setup_ap_ssid(&[1, 2, 3, 4]).unwrap();

        assert!(setup_ap_ssid_matches_policy(ssid));
        assert_ne!(ssid, generate_setup_ap_ssid(&[4, 3, 2, 1]).unwrap());
    }

    #[test]
    fn generates_8_digit_setup_password_from_random_bytes() {
        let password = generate_setup_ap_password(&[1, 2, 3, 4, 5, 6, 7, 8]).unwrap();

        assert_eq!(password.len(), SETUP_AP_PASSWORD_LEN);
        assert!(password.as_bytes().iter().all(|byte| byte.is_ascii_digit()));
        assert_ne!(
            password,
            generate_setup_ap_password(&[8, 7, 6, 5, 4, 3, 2, 1]).unwrap()
        );
        assert!(setup_ap_password_matches_policy(password));
    }

    #[test]
    fn builds_wifi_qr_payload_and_escapes_special_chars() {
        let ssid = FixedAscii::<32>::try_from_str("BMS;GPS").unwrap();
        let password = FixedAscii::<64>::try_from_str("pa:ss,word").unwrap();
        let payload = wifi_qr_payload(ssid, password).unwrap();

        assert_eq!(
            payload.as_str(),
            r"WIFI:S:BMS\;GPS;T:WPA;P:pa\:ss\,word;;"
        );
    }
}
