use embedded_storage::{ReadStorage, Storage};
use esp_storage::{FlashStorage, FlashStorageError};
use esp32_bms_gps::{
    settings::{DeviceSettings, TouchCalibration},
    settings_store::{self, StoredSettings},
};

use crate::board;

const MAGIC: [u8; 4] = *b"TCHC";
const VERSION: u8 = 1;
const HEADER_LEN: usize = 8;
const PAYLOAD_LEN: usize = TouchCalibration::ENCODED_LEN;
const CHECKSUM_LEN: usize = 4;
const RECORD_LEN: usize = HEADER_LEN + PAYLOAD_LEN + CHECKSUM_LEN;
const CHECKSUM_OFFSET: usize = RECORD_LEN - CHECKSUM_LEN;

pub struct DeviceSettingsStore<'d> {
    flash: FlashStorage<'d>,
}

impl<'d> DeviceSettingsStore<'d> {
    pub fn new(flash: esp_hal::peripherals::FLASH<'d>) -> Self {
        Self {
            flash: FlashStorage::new(flash).multicore_auto_park(),
        }
    }

    pub fn load(&mut self) -> Result<Option<StoredSettings>, FlashStorageError> {
        let slot_a = self.read_settings_slot(board::storage::DEVICE_SETTINGS_SLOT_A_OFFSET)?;
        let slot_b = self.read_settings_slot(board::storage::DEVICE_SETTINGS_SLOT_B_OFFSET)?;

        if let Ok(Some(settings)) = settings_store::select_latest(&slot_a, &slot_b) {
            return Ok(Some(settings));
        }

        if let Some(calibration) = self.load_legacy_touch_calibration()? {
            let settings = DeviceSettings {
                touch_calibration: Some(calibration),
                ..DeviceSettings::default()
            };
            return Ok(Some(StoredSettings {
                settings,
                generation: 0,
            }));
        }

        Ok(None)
    }

    pub fn save(&mut self, settings: &DeviceSettings) -> Result<u32, FlashStorageError> {
        let slot_a = self.read_settings_slot(board::storage::DEVICE_SETTINGS_SLOT_A_OFFSET)?;
        let slot_b = self.read_settings_slot(board::storage::DEVICE_SETTINGS_SLOT_B_OFFSET)?;
        let left = settings_store::decode_record(&slot_a).ok().flatten();
        let right = settings_store::decode_record(&slot_b).ok().flatten();

        let left_generation = left.map(|stored| stored.generation).unwrap_or(0);
        let right_generation = right.map(|stored| stored.generation).unwrap_or(0);
        let next_generation = left_generation.max(right_generation).saturating_add(1);
        let target_offset = if left_generation <= right_generation {
            board::storage::DEVICE_SETTINGS_SLOT_A_OFFSET
        } else {
            board::storage::DEVICE_SETTINGS_SLOT_B_OFFSET
        };

        let record = settings_store::encode_record(settings, next_generation);
        self.flash.write(target_offset, &record)?;
        Ok(next_generation)
    }

    fn read_settings_slot(
        &mut self,
        offset: u32,
    ) -> Result<[u8; settings_store::RECORD_LEN], FlashStorageError> {
        let mut record = [0_u8; settings_store::RECORD_LEN];
        self.flash.read(offset, &mut record)?;
        Ok(record)
    }

    fn load_legacy_touch_calibration(
        &mut self,
    ) -> Result<Option<TouchCalibration>, FlashStorageError> {
        let mut record = [0_u8; RECORD_LEN];
        self.flash
            .read(board::storage::TOUCH_CALIBRATION_OFFSET, &mut record)?;
        Ok(decode_touch_record(&record))
    }
}

fn decode_touch_record(record: &[u8; RECORD_LEN]) -> Option<TouchCalibration> {
    if is_erased(record) || record[..4] != MAGIC || record[4] != VERSION {
        return None;
    }

    let stored_checksum = u32::from_le_bytes([
        record[CHECKSUM_OFFSET],
        record[CHECKSUM_OFFSET + 1],
        record[CHECKSUM_OFFSET + 2],
        record[CHECKSUM_OFFSET + 3],
    ]);
    if checksum(&record[..CHECKSUM_OFFSET]) != stored_checksum {
        return None;
    }

    let mut payload = [0_u8; PAYLOAD_LEN];
    payload.copy_from_slice(&record[HEADER_LEN..CHECKSUM_OFFSET]);
    TouchCalibration::decode(&payload)
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

    #[test]
    fn decodes_existing_valid_touch_calibration_record() {
        let record = [
            0x54, 0x43, 0x48, 0x43, 0x01, 0xFF, 0xFF, 0xFF, 0xC5, 0x01, 0xDD, 0x0D, 0x65, 0x02,
            0x9D, 0x0D, 0x40, 0x01, 0xF0, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x31, 0x6A, 0xE9,
        ];

        let calibration = decode_touch_record(&record).unwrap();

        assert_eq!(
            calibration,
            TouchCalibration {
                raw_x_min: 453,
                raw_x_max: 3549,
                raw_y_min: 613,
                raw_y_max: 3485,
                swap_xy: true,
                invert_x: false,
                invert_y: false,
                width: 320,
                height: 240,
            }
        );
        assert_eq!(checksum(&record[..CHECKSUM_OFFSET]), 0xE96A31F0);
    }
}
