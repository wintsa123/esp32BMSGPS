use crate::settings::{FixedAscii, MacAddress};

pub const MAX_BMS_SCAN_CANDIDATES: usize = 6;
pub const MAX_BMS_SCAN_NAME_LEN: usize = 24;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BmsScanCandidate {
    pub mac: MacAddress,
    pub name: Option<FixedAscii<MAX_BMS_SCAN_NAME_LEN>>,
    pub rssi: i8,
}

impl BmsScanCandidate {
    pub const EMPTY: Self = Self {
        mac: MacAddress::new([0; 6]),
        name: None,
        rssi: i8::MIN,
    };

    pub fn new(mac: MacAddress, name: Option<&str>, rssi: i8) -> Self {
        Self {
            mac,
            name: fixed_name(name),
            rssi,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BmsScanUpdate {
    Inserted,
    Updated,
    ReplacedWeakest,
    IgnoredFull,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BmsScanCandidates {
    entries: [BmsScanCandidate; MAX_BMS_SCAN_CANDIDATES],
    len: u8,
}

impl BmsScanCandidates {
    pub const fn new() -> Self {
        Self {
            entries: [BmsScanCandidate::EMPTY; MAX_BMS_SCAN_CANDIDATES],
            len: 0,
        }
    }

    pub fn clear(&mut self) {
        self.entries = [BmsScanCandidate::EMPTY; MAX_BMS_SCAN_CANDIDATES];
        self.len = 0;
    }

    pub const fn len(&self) -> usize {
        self.len as usize
    }

    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub fn as_slice(&self) -> &[BmsScanCandidate] {
        &self.entries[..self.len()]
    }

    pub fn upsert(&mut self, mac: MacAddress, name: Option<&str>, rssi: i8) -> BmsScanUpdate {
        let candidate = BmsScanCandidate::new(mac, name, rssi);

        if let Some(index) = self.find_index(mac) {
            self.entries[index].rssi = rssi;
            if candidate.name.is_some() {
                self.entries[index].name = candidate.name;
            }
            return BmsScanUpdate::Updated;
        }

        let len = self.len();
        if len < MAX_BMS_SCAN_CANDIDATES {
            self.entries[len] = candidate;
            self.len += 1;
            return BmsScanUpdate::Inserted;
        }

        let weakest_index = self.weakest_index();
        if rssi > self.entries[weakest_index].rssi {
            self.entries[weakest_index] = candidate;
            BmsScanUpdate::ReplacedWeakest
        } else {
            BmsScanUpdate::IgnoredFull
        }
    }

    fn find_index(&self, mac: MacAddress) -> Option<usize> {
        self.as_slice()
            .iter()
            .position(|candidate| candidate.mac == mac)
    }

    fn weakest_index(&self) -> usize {
        let mut weakest = 0;
        for index in 1..MAX_BMS_SCAN_CANDIDATES {
            if self.entries[index].rssi < self.entries[weakest].rssi {
                weakest = index;
            }
        }
        weakest
    }
}

impl Default for BmsScanCandidates {
    fn default() -> Self {
        Self::new()
    }
}

fn fixed_name(name: Option<&str>) -> Option<FixedAscii<MAX_BMS_SCAN_NAME_LEN>> {
    let name = name?;
    let bytes = name.as_bytes();
    if !bytes.is_ascii() {
        return None;
    }
    let len = bytes.len().min(MAX_BMS_SCAN_NAME_LEN);
    FixedAscii::from_ascii_bytes(&bytes[..len]).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mac(last: u8) -> MacAddress {
        MacAddress::new([0, 1, 2, 3, 4, last])
    }

    #[test]
    fn upsert_deduplicates_by_mac_and_preserves_existing_name() {
        let mut candidates = BmsScanCandidates::new();

        assert_eq!(
            candidates.upsert(mac(1), Some("ANT-24S"), -70),
            BmsScanUpdate::Inserted
        );
        assert_eq!(candidates.upsert(mac(1), None, -55), BmsScanUpdate::Updated);

        assert_eq!(candidates.len(), 1);
        assert_eq!(candidates.as_slice()[0].rssi, -55);
        assert_eq!(candidates.as_slice()[0].name.unwrap().as_str(), "ANT-24S");
    }

    #[test]
    fn capacity_replaces_weakest_only_when_new_signal_is_stronger() {
        let mut candidates = BmsScanCandidates::new();
        for index in 0..MAX_BMS_SCAN_CANDIDATES {
            let rssi = -30 - index as i8;
            assert_eq!(
                candidates.upsert(mac(index as u8), Some("ANT-BMS"), rssi),
                BmsScanUpdate::Inserted
            );
        }

        assert_eq!(
            candidates.upsert(mac(99), Some("ANT-STRONG"), -20),
            BmsScanUpdate::ReplacedWeakest
        );
        assert!(
            candidates
                .as_slice()
                .iter()
                .any(|candidate| candidate.mac == mac(99))
        );
        assert_eq!(
            candidates.upsert(mac(100), Some("ANT-WEAK"), -90),
            BmsScanUpdate::IgnoredFull
        );
        assert!(
            !candidates
                .as_slice()
                .iter()
                .any(|candidate| candidate.mac == mac(100))
        );
    }

    #[test]
    fn name_is_ascii_and_bounded() {
        let candidate = BmsScanCandidate::new(mac(1), Some("ANT-012345678901234567890123"), -40);
        assert_eq!(candidate.name.unwrap().as_str(), "ANT-01234567890123456789");

        let candidate = BmsScanCandidate::new(mac(2), Some("蚂蚁"), -40);
        assert_eq!(candidate.name, None);
    }
}
