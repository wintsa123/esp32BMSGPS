use core::cmp::Ordering;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Sha256Digest {
    pub bytes: [u8; 32],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OtaManifest<'a> {
    pub latest: &'a str,
    pub min_supported: &'a str,
    pub firmware_url: &'a str,
    pub sha256: Sha256Digest,
    pub size: u32,
    pub notes: Option<&'a str>,
}

impl OtaManifest<'_> {
    pub fn update_available(self, current_version: &str) -> bool {
        compare_versions(self.latest, current_version) == Some(Ordering::Greater)
    }

    pub fn supports_current(self, current_version: &str) -> bool {
        matches!(
            compare_versions(current_version, self.min_supported),
            Some(Ordering::Greater | Ordering::Equal)
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ManifestError {
    MissingField,
    InvalidString,
    InvalidNumber,
    InvalidUrl,
    InvalidSha256,
    InvalidSize,
    InvalidVersion,
}

pub fn parse_manifest(json: &str) -> Result<OtaManifest<'_>, ManifestError> {
    let latest = find_string_field(json, "latest")?;
    let min_supported = find_string_field(json, "min_supported")?;
    let firmware_url = find_string_field(json, "firmware_url")?;
    let sha256 = parse_sha256_hex(find_string_field(json, "sha256")?)?;
    let size = find_u32_field(json, "size")?;
    let notes = find_string_field(json, "notes").ok();

    if latest.is_empty()
        || min_supported.is_empty()
        || compare_versions(latest, min_supported).is_none()
    {
        return Err(ManifestError::InvalidVersion);
    }
    if !(firmware_url.starts_with("https://") || firmware_url.starts_with("http://")) {
        return Err(ManifestError::InvalidUrl);
    }
    if size == 0 {
        return Err(ManifestError::InvalidSize);
    }

    Ok(OtaManifest {
        latest,
        min_supported,
        firmware_url,
        sha256,
        size,
        notes,
    })
}

pub fn compare_versions(left: &str, right: &str) -> Option<Ordering> {
    let mut left_parts = left.split('.');
    let mut right_parts = right.split('.');

    loop {
        let left_next = left_parts.next();
        let right_next = right_parts.next();
        if left_next.is_none() && right_next.is_none() {
            return Some(Ordering::Equal);
        }

        let left_value = parse_version_part(left_next.unwrap_or("0"))?;
        let right_value = parse_version_part(right_next.unwrap_or("0"))?;
        match left_value.cmp(&right_value) {
            Ordering::Equal => {}
            other => return Some(other),
        }
    }
}

fn parse_version_part(part: &str) -> Option<u32> {
    if part.is_empty() || !part.bytes().all(|byte| byte.is_ascii_digit()) {
        return None;
    }
    part.parse::<u32>().ok()
}

fn parse_sha256_hex(value: &str) -> Result<Sha256Digest, ManifestError> {
    let bytes = value.as_bytes();
    if bytes.len() != 64 {
        return Err(ManifestError::InvalidSha256);
    }

    let mut digest = [0_u8; 32];
    let mut index = 0;
    while index < 32 {
        digest[index] = (hex_nibble(bytes[index * 2])? << 4) | hex_nibble(bytes[index * 2 + 1])?;
        index += 1;
    }

    Ok(Sha256Digest { bytes: digest })
}

fn hex_nibble(value: u8) -> Result<u8, ManifestError> {
    match value {
        b'0'..=b'9' => Ok(value - b'0'),
        b'a'..=b'f' => Ok(value - b'a' + 10),
        b'A'..=b'F' => Ok(value - b'A' + 10),
        _ => Err(ManifestError::InvalidSha256),
    }
}

fn find_string_field<'a>(json: &'a str, field: &str) -> Result<&'a str, ManifestError> {
    let mut index = find_value_start(json, field)?;
    let bytes = json.as_bytes();
    if bytes.get(index) != Some(&b'"') {
        return Err(ManifestError::InvalidString);
    }
    index += 1;
    let start = index;

    while index < bytes.len() {
        match bytes[index] {
            b'"' => return Ok(&json[start..index]),
            b'\\' => return Err(ManifestError::InvalidString),
            _ => index += 1,
        }
    }

    Err(ManifestError::InvalidString)
}

fn find_u32_field(json: &str, field: &str) -> Result<u32, ManifestError> {
    let mut index = find_value_start(json, field)?;
    let bytes = json.as_bytes();
    let start = index;

    while index < bytes.len() && bytes[index].is_ascii_digit() {
        index += 1;
    }
    if start == index {
        return Err(ManifestError::InvalidNumber);
    }

    json[start..index]
        .parse::<u32>()
        .map_err(|_| ManifestError::InvalidNumber)
}

fn find_value_start(json: &str, field: &str) -> Result<usize, ManifestError> {
    let bytes = json.as_bytes();
    let field_bytes = field.as_bytes();
    let mut index = 0;

    while index + field_bytes.len() + 2 <= bytes.len() {
        if bytes[index] == b'"'
            && bytes[index + 1..].starts_with(field_bytes)
            && bytes.get(index + 1 + field_bytes.len()) == Some(&b'"')
        {
            let mut cursor = index + 2 + field_bytes.len();
            skip_ws(bytes, &mut cursor);
            if bytes.get(cursor) != Some(&b':') {
                index += 1;
                continue;
            }
            cursor += 1;
            skip_ws(bytes, &mut cursor);
            return Ok(cursor);
        }
        index += 1;
    }

    Err(ManifestError::MissingField)
}

fn skip_ws(bytes: &[u8], index: &mut usize) {
    while *index < bytes.len() && matches!(bytes[*index], b' ' | b'\n' | b'\r' | b'\t') {
        *index += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"{
      "latest": "0.2.0",
      "min_supported": "0.1.0",
      "firmware_url": "https://example.com/firmware.bin",
      "sha256": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
      "size": 1048576,
      "notes": "test"
    }"#;

    #[test]
    fn parses_manifest_and_detects_update() {
        let manifest = parse_manifest(SAMPLE).unwrap();

        assert_eq!(manifest.latest, "0.2.0");
        assert_eq!(manifest.min_supported, "0.1.0");
        assert_eq!(manifest.sha256.bytes[0], 0x00);
        assert_eq!(manifest.sha256.bytes[31], 0xFF);
        assert_eq!(manifest.size, 1_048_576);
        assert!(manifest.update_available("0.1.0"));
        assert!(!manifest.update_available("0.2.0"));
        assert!(manifest.supports_current("0.1.0"));
    }

    #[test]
    fn rejects_bad_sha_and_zero_size() {
        assert_eq!(
            parse_manifest(
                r#"{"latest":"1.0.0","min_supported":"1.0.0","firmware_url":"https://x","sha256":"00","size":1}"#
            ),
            Err(ManifestError::InvalidSha256)
        );
        assert_eq!(
            parse_manifest(
                r#"{"latest":"1.0.0","min_supported":"1.0.0","firmware_url":"https://x","sha256":"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff","size":0}"#
            ),
            Err(ManifestError::InvalidSize)
        );
    }

    #[test]
    fn compares_semver_like_versions() {
        assert_eq!(compare_versions("1.2.10", "1.2.3"), Some(Ordering::Greater));
        assert_eq!(compare_versions("1.2", "1.2.0"), Some(Ordering::Equal));
        assert_eq!(compare_versions("1.a", "1.0"), None);
    }
}
