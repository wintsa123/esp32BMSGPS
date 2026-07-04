use qrcodegen::{QrCode, QrCodeEcc, Version};

pub const MAX_QR_VERSION: Version = Version::new(8);
pub const MAX_QR_SIZE: usize = MAX_QR_VERSION.value() as usize * 4 + 17;
pub const MAX_QR_MODULES: usize = MAX_QR_SIZE * MAX_QR_SIZE;
const QR_BUFFER_LEN: usize = MAX_QR_VERSION.buffer_len();

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum QrError {
    DataTooLong,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct QrMatrix {
    size: u8,
    modules: [u8; MAX_QR_MODULES],
}

impl QrMatrix {
    pub fn size(&self) -> u8 {
        self.size
    }

    pub fn is_dark(&self, x: u8, y: u8) -> bool {
        if x >= self.size || y >= self.size {
            return false;
        }
        self.modules[y as usize * MAX_QR_SIZE + x as usize] != 0
    }
}

pub fn encode_text(text: &str) -> Result<QrMatrix, QrError> {
    let mut temp = [0_u8; QR_BUFFER_LEN];
    let mut out = [0_u8; QR_BUFFER_LEN];
    let qr = QrCode::encode_text(
        text,
        &mut temp,
        &mut out,
        QrCodeEcc::Medium,
        Version::MIN,
        MAX_QR_VERSION,
        None,
        true,
    )
    .map_err(|_| QrError::DataTooLong)?;

    let size = qr.size() as u8;
    let mut modules = [0_u8; MAX_QR_MODULES];
    let mut y = 0;
    while y < size {
        let mut x = 0;
        while x < size {
            modules[y as usize * MAX_QR_SIZE + x as usize] =
                u8::from(qr.get_module(x as i32, y as i32));
            x += 1;
        }
        y += 1;
    }

    Ok(QrMatrix { size, modules })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        provisioning::{generate_setup_ap_ssid, wifi_qr_payload},
        settings::FixedAscii,
    };

    #[test]
    fn encodes_wifi_payload_to_matrix() {
        let ssid = generate_setup_ap_ssid(&[1, 2, 3, 4]).unwrap();
        let password = FixedAscii::<64>::try_from_str("setup1234").unwrap();
        let payload = wifi_qr_payload(ssid, password).unwrap();

        let qr = encode_text(payload.as_str()).unwrap();

        assert!(qr.size() >= 21);
        assert!(qr.size() as usize <= MAX_QR_SIZE);
        assert!(qr.is_dark(0, 0));
        assert!(qr.is_dark(6, 0));
        assert!(qr.is_dark(0, 6));
        assert!(qr.is_dark(6, 6));
        assert!(!qr.is_dark(qr.size(), qr.size()));
    }

    #[test]
    fn rejects_text_that_does_not_fit_first_boot_qr_budget() {
        let text = "A".repeat(512);

        assert_eq!(encode_text(&text), Err(QrError::DataTooLong));
    }
}
