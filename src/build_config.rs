pub const FIRMWARE_NAME: &str = env!("CARGO_PKG_NAME");
pub const FIRMWARE_VERSION: &str = env!("CARGO_PKG_VERSION");

const DEFAULT_OTA_MANIFEST_URL: &str = "https://example.invalid/esp32-bms-gps/manifest.json";

pub fn ota_manifest_url() -> &'static str {
    match option_env!("OTA_MANIFEST_URL") {
        Some(url) if !url.is_empty() => url,
        _ => DEFAULT_OTA_MANIFEST_URL,
    }
}
