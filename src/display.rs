use esp_hal::{
    Blocking,
    delay::Delay,
    gpio::Output,
    spi::{Error as SpiError, master::Spi},
};
use esp32_bms_gps::{
    app_state::{DashboardSnapshot, OtaState, WifiLinkState},
    gps_nmea::SpeedUnit,
    qr::QrMatrix,
    settings::{DeviceSettings, DisplayRotation, Language, ScreenPoint},
};

use crate::board;

const SWRESET: u8 = 0x01;
const SLPOUT: u8 = 0x11;
const NORON: u8 = 0x13;
const INVOFF: u8 = 0x20;
const INVON: u8 = 0x21;
const CASET: u8 = 0x2A;
const RASET: u8 = 0x2B;
const RAMWR: u8 = 0x2C;
const MADCTL: u8 = 0x36;
const COLMOD: u8 = 0x3A;
const DISPON: u8 = 0x29;

const MADCTL_BGR: u8 = 0x08;
const MADCTL_MV: u8 = 0x20;
const MADCTL_MX: u8 = 0x40;
const MADCTL_MY: u8 = 0x80;
const RGB565: u8 = 0x55;

pub const BLACK: u16 = 0x0000;
pub const GREEN: u16 = 0x07E0;
pub const WHITE: u16 = 0xFFFF;
pub const YELLOW: u16 = 0xFFE0;
pub const CYAN: u16 = 0x07FF;
pub const ORANGE: u16 = 0xFD20;
pub const GRAY: u16 = 0x7BEF;
pub const DARK_GRAY: u16 = 0x2104;

pub struct St7789<'d> {
    spi: Spi<'d, Blocking>,
    cs: Output<'d>,
    dc: Output<'d>,
    delay: Delay,
    rotation: DisplayRotation,
}

impl<'d> St7789<'d> {
    pub fn new(spi: Spi<'d, Blocking>, cs: Output<'d>, dc: Output<'d>, delay: Delay) -> Self {
        Self {
            spi,
            cs,
            dc,
            delay,
            rotation: DisplayRotation::Portrait,
        }
    }

    pub fn init(&mut self, rotation: DisplayRotation) -> Result<(), SpiError> {
        self.cs.set_high();
        self.delay.delay_millis(20);

        self.command(SWRESET)?;
        self.delay.delay_millis(150);
        self.command(SLPOUT)?;
        self.delay.delay_millis(120);

        self.command_data(COLMOD, &[RGB565])?;
        self.set_rotation(rotation)?;

        if board::tft::INVERT_COLORS {
            self.command(INVON)?;
        } else {
            self.command(INVOFF)?;
        }
        self.command(NORON)?;
        self.delay.delay_millis(10);
        self.command(DISPON)?;
        self.delay.delay_millis(120);

        Ok(())
    }

    pub fn clear(&mut self, color: u16) -> Result<(), SpiError> {
        let (w, h) = self.rotation.logical_size();
        self.fill_rect(0, 0, w, h, color)
    }

    pub fn draw_calibration_target(&mut self, point: ScreenPoint) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        self.draw_crosshair(point, WHITE)?;
        self.fill_rect(
            point.x.saturating_sub(3),
            point.y.saturating_sub(3),
            7,
            7,
            YELLOW,
        )
    }

    pub fn draw_touch_feedback(&mut self, point: ScreenPoint) -> Result<(), SpiError> {
        self.draw_crosshair(point, GREEN)
    }

    pub fn draw_dashboard(
        &mut self,
        snapshot: &DashboardSnapshot,
        firmware_version: &str,
    ) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        self.draw_text(10, 8, 2, CYAN, "GPS")?;
        self.draw_speed(snapshot)?;

        self.draw_text(10, 76, 1, GRAY, "BMS")?;
        if snapshot.bms_online {
            self.draw_text(46, 76, 1, GREEN, "ONLINE")?;
        } else {
            self.draw_text(46, 76, 1, ORANGE, "OFFLINE")?;
        }

        self.draw_metric_mv(10, 96, "PACK", snapshot.pack_voltage_mv)?;
        self.draw_metric_deci_i16(170, 96, "CUR", snapshot.current_deci_amps, "A")?;
        self.draw_metric_u16(10, 122, "SOC", snapshot.soc_percent, "%")?;
        self.draw_cell_span(snapshot)?;

        self.draw_text(10, 174, 1, GRAY, "WIFI")?;
        self.draw_text(58, 174, 1, WHITE, wifi_label(snapshot.wifi))?;
        if snapshot.setup_ap_enabled {
            self.draw_text(10, 192, 1, YELLOW, "SETUP AP ON")?;
        }

        self.draw_text(10, 216, 1, GRAY, "OTA")?;
        self.draw_text(46, 216, 1, WHITE, ota_label(snapshot.ota))?;
        self.draw_text(190, 216, 1, GRAY, "FW")?;
        self.draw_text(214, 216, 1, WHITE, firmware_version)?;
        self.draw_text(10, 232, 1, DARK_GRAY, "TAP BOTTOM FOR SETTINGS")
    }

    pub fn draw_settings_menu(&mut self, settings: &DeviceSettings) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        self.draw_text(10, 8, 2, CYAN, "SETTINGS")?;
        self.draw_text(212, 12, 1, DARK_GRAY, "TAP TOP=BACK")?;
        self.draw_menu_row_value(0, "WIFI AP", setup_ap_label(settings))?;
        self.draw_menu_row_u8(1, "BRIGHT", settings.brightness_percent, "%")?;
        self.draw_menu_row_value(2, "ROT", rotation_label(settings.display_rotation))?;
        self.draw_menu_row_value(3, "SPEED", speed_unit_label(settings.speed_unit))?;
        self.draw_menu_row_value(4, "LANG", language_label(settings.language))?;
        self.draw_menu_row_value(5, "BMS", bms_label(settings))?;
        self.draw_menu_row_value(6, "RESET", "DEFAULTS")
    }

    pub fn draw_setup_ap_qr(
        &mut self,
        qr: &QrMatrix,
        ssid: &str,
        password: &str,
    ) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        self.draw_text(10, 8, 2, CYAN, "SETUP WIFI")?;
        self.draw_text(10, 34, 1, WHITE, "SSID")?;
        self.draw_text(58, 34, 1, YELLOW, ssid)?;
        self.draw_text(10, 50, 1, WHITE, "PASS")?;
        self.draw_text(58, 50, 1, YELLOW, password)?;
        self.draw_text(10, 66, 1, GRAY, "HTTP://192.168.4.1")?;

        let (width, height) = self.rotation.logical_size();
        let quiet_modules = 4_u16;
        let module_count = qr.size() as u16 + quiet_modules * 2;
        let max_qr_width = width.saturating_sub(24);
        let max_qr_height = height.saturating_sub(92);
        let module_px = (max_qr_width.min(max_qr_height) / module_count).max(1);
        let qr_px = module_count * module_px;
        let start_x = (width.saturating_sub(qr_px)) / 2;
        let start_y = 84_u16;

        self.fill_rect(
            start_x.saturating_sub(3),
            start_y.saturating_sub(3),
            qr_px + 6,
            qr_px + 6,
            WHITE,
        )?;
        self.fill_rect(start_x, start_y, qr_px, qr_px, WHITE)?;
        let mut y = 0;
        while y < qr.size() {
            let mut x = 0;
            while x < qr.size() {
                if qr.is_dark(x, y) {
                    self.fill_rect(
                        start_x + (x as u16 + quiet_modules) * module_px,
                        start_y + (y as u16 + quiet_modules) * module_px,
                        module_px,
                        module_px,
                        BLACK,
                    )?;
                }
                x += 1;
            }
            y += 1;
        }

        self.draw_text(10, height.saturating_sub(18), 1, GRAY, "SCAN QR TO JOIN")
    }

    pub fn draw_text(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        text: &str,
    ) -> Result<(), SpiError> {
        if scale == 0 {
            return Ok(());
        }

        let mut cursor_x = x;
        for byte in text.bytes() {
            self.draw_char(cursor_x, y, scale, color, byte)?;
            cursor_x = cursor_x.saturating_add(6 * scale);
        }
        Ok(())
    }

    pub fn set_rotation(&mut self, rotation: DisplayRotation) -> Result<(), SpiError> {
        self.rotation = rotation;
        self.command_data(MADCTL, &[madctl(rotation)])
    }

    pub fn fill_rect(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        height: u16,
        color: u16,
    ) -> Result<(), SpiError> {
        let (screen_width, screen_height) = self.rotation.logical_size();
        if width == 0 || height == 0 || x >= screen_width || y >= screen_height {
            return Ok(());
        }

        let width = width.min(screen_width - x);
        let height = height.min(screen_height - y);

        self.set_window(x, y, x + width - 1, y + height - 1)?;
        self.command(RAMWR)?;
        self.dc.set_high();
        self.cs.set_low();

        let hi = (color >> 8) as u8;
        let lo = color as u8;
        let mut chunk = [0_u8; 128];
        for pair in chunk.chunks_exact_mut(2) {
            pair[0] = hi;
            pair[1] = lo;
        }

        let mut pixels = width as u32 * height as u32;
        while pixels > 0 {
            let send_pixels = pixels.min((chunk.len() / 2) as u32) as usize;
            self.spi.write(&chunk[..send_pixels * 2])?;
            pixels -= send_pixels as u32;
        }

        self.cs.set_high();
        Ok(())
    }

    fn draw_crosshair(&mut self, point: ScreenPoint, color: u16) -> Result<(), SpiError> {
        self.fill_rect(point.x.saturating_sub(18), point.y, 37, 1, color)?;
        self.fill_rect(point.x, point.y.saturating_sub(18), 1, 37, color)?;
        self.fill_rect(
            point.x.saturating_sub(10),
            point.y.saturating_sub(10),
            21,
            1,
            color,
        )?;
        self.fill_rect(
            point.x.saturating_sub(10),
            point.y.saturating_add(10),
            21,
            1,
            color,
        )?;
        self.fill_rect(
            point.x.saturating_sub(10),
            point.y.saturating_sub(10),
            1,
            21,
            color,
        )?;
        self.fill_rect(
            point.x.saturating_add(10),
            point.y.saturating_sub(10),
            1,
            21,
            color,
        )
    }

    fn draw_speed(&mut self, snapshot: &DashboardSnapshot) -> Result<(), SpiError> {
        match snapshot.speed_deci_units {
            Some(speed) => {
                self.draw_u16_deci(80, 8, 5, WHITE, speed)?;
            }
            None => {
                self.draw_text(82, 8, 5, WHITE, "--.-")?;
            }
        }
        self.draw_text(248, 50, 1, GRAY, speed_unit_label(snapshot.speed_unit))
    }

    fn draw_metric_mv(
        &mut self,
        x: u16,
        y: u16,
        label: &str,
        value_mv: Option<u32>,
    ) -> Result<(), SpiError> {
        self.draw_text(x, y, 1, GRAY, label)?;
        match value_mv {
            Some(value) => self.draw_u32_deci(x + 54, y, 1, WHITE, value / 100, "V"),
            None => self.draw_text(x + 54, y, 1, WHITE, "--.-V"),
        }
    }

    fn draw_metric_deci_i16(
        &mut self,
        x: u16,
        y: u16,
        label: &str,
        value: Option<i16>,
        suffix: &str,
    ) -> Result<(), SpiError> {
        self.draw_text(x, y, 1, GRAY, label)?;
        match value {
            Some(value) => self.draw_i16_deci(x + 42, y, 1, WHITE, value, suffix),
            None => self.draw_text(x + 42, y, 1, WHITE, "--.-A"),
        }
    }

    fn draw_metric_u16(
        &mut self,
        x: u16,
        y: u16,
        label: &str,
        value: Option<u16>,
        suffix: &str,
    ) -> Result<(), SpiError> {
        self.draw_text(x, y, 1, GRAY, label)?;
        match value {
            Some(value) => self.draw_u16(x + 42, y, 1, WHITE, value, suffix),
            None => self.draw_text(x + 42, y, 1, WHITE, "--%"),
        }
    }

    fn draw_cell_span(&mut self, snapshot: &DashboardSnapshot) -> Result<(), SpiError> {
        self.draw_text(170, 122, 1, GRAY, "CELL")?;
        match (snapshot.min_cell_voltage_mv, snapshot.max_cell_voltage_mv) {
            (Some(min), Some(max)) => {
                self.draw_u16_deci(212, 122, 1, WHITE, min / 10)?;
                self.draw_text(248, 122, 1, GRAY, "-")?;
                self.draw_u16_deci(260, 122, 1, WHITE, max / 10)
            }
            _ => self.draw_text(212, 122, 1, WHITE, "--"),
        }
    }

    fn draw_menu_row_value(
        &mut self,
        index: u16,
        label: &str,
        value: &str,
    ) -> Result<(), SpiError> {
        let y = 48 + index * 28;
        self.fill_rect(8, y - 4, 304, 24, DARK_GRAY)?;
        self.draw_text(18, y, 1, WHITE, label)?;
        self.draw_text(128, y, 1, YELLOW, value)
    }

    fn draw_menu_row_u8(
        &mut self,
        index: u16,
        label: &str,
        value: u8,
        suffix: &str,
    ) -> Result<(), SpiError> {
        let y = 48 + index * 28;
        self.fill_rect(8, y - 4, 304, 24, DARK_GRAY)?;
        self.draw_text(18, y, 1, WHITE, label)?;
        self.draw_u16(128, y, 1, YELLOW, value as u16, suffix)
    }

    fn draw_u16_deci(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        value: u16,
    ) -> Result<(), SpiError> {
        let whole = value / 10;
        let frac = value % 10;
        let mut buf = [0_u8; 8];
        let len = write_u32(&mut buf, whole as u32);
        buf[len] = b'.';
        buf[len + 1] = b'0' + frac as u8;
        let text = core::str::from_utf8(&buf[..len + 2]).unwrap_or("--.-");
        self.draw_text(x, y, scale, color, text)
    }

    fn draw_u32_deci(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        value: u32,
        suffix: &str,
    ) -> Result<(), SpiError> {
        let whole = value / 10;
        let frac = value % 10;
        let mut buf = [0_u8; 16];
        let len = write_u32(&mut buf, whole);
        buf[len] = b'.';
        buf[len + 1] = b'0' + frac as u8;
        let text = core::str::from_utf8(&buf[..len + 2]).unwrap_or("--.-");
        self.draw_text(x, y, scale, color, text)?;
        self.draw_text(x + (len as u16 + 2) * 6 * scale, y, scale, color, suffix)
    }

    fn draw_i16_deci(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        value: i16,
        suffix: &str,
    ) -> Result<(), SpiError> {
        let mut buf = [0_u8; 16];
        let mut cursor = 0;
        let magnitude = if value < 0 {
            buf[0] = b'-';
            cursor = 1;
            value.saturating_abs() as u16
        } else {
            value as u16
        };
        let len = write_u32(&mut buf[cursor..], (magnitude / 10) as u32);
        cursor += len;
        buf[cursor] = b'.';
        buf[cursor + 1] = b'0' + (magnitude % 10) as u8;
        let text = core::str::from_utf8(&buf[..cursor + 2]).unwrap_or("--.-");
        self.draw_text(x, y, scale, color, text)?;
        self.draw_text(x + (cursor as u16 + 2) * 6 * scale, y, scale, color, suffix)
    }

    fn draw_u16(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        value: u16,
        suffix: &str,
    ) -> Result<(), SpiError> {
        let mut buf = [0_u8; 8];
        let len = write_u32(&mut buf, value as u32);
        let text = core::str::from_utf8(&buf[..len]).unwrap_or("--");
        self.draw_text(x, y, scale, color, text)?;
        self.draw_text(x + len as u16 * 6 * scale, y, scale, color, suffix)
    }

    fn draw_char(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        byte: u8,
    ) -> Result<(), SpiError> {
        let glyph = glyph_columns(byte);
        for (col, bits) in glyph.iter().copied().enumerate() {
            for row in 0..7 {
                if bits & (1 << row) != 0 {
                    self.fill_rect(x + col as u16 * scale, y + row * scale, scale, scale, color)?;
                }
            }
        }
        Ok(())
    }

    fn set_window(&mut self, x0: u16, y0: u16, x1: u16, y1: u16) -> Result<(), SpiError> {
        self.command_data(CASET, &pack_u16_pair(x0, x1))?;
        self.command_data(RASET, &pack_u16_pair(y0, y1))
    }

    fn command(&mut self, command: u8) -> Result<(), SpiError> {
        self.dc.set_low();
        self.cs.set_low();
        let result = self.spi.write(&[command]);
        self.cs.set_high();
        result
    }

    fn command_data(&mut self, command: u8, data: &[u8]) -> Result<(), SpiError> {
        self.command(command)?;
        self.dc.set_high();
        self.cs.set_low();
        let result = self.spi.write(data);
        self.cs.set_high();
        result
    }
}

fn speed_unit_label(unit: SpeedUnit) -> &'static str {
    match unit {
        SpeedUnit::Kmh => "KM/H",
        SpeedUnit::Mph => "MPH",
    }
}

fn language_label(language: Language) -> &'static str {
    match language {
        Language::Chinese => "ZH",
        Language::English => "EN",
    }
}

fn rotation_label(rotation: DisplayRotation) -> &'static str {
    match rotation {
        DisplayRotation::Portrait => "PORTRAIT",
        DisplayRotation::Landscape => "LAND",
        DisplayRotation::InvertedPortrait => "INV PORT",
        DisplayRotation::InvertedLandscape => "INV LAND",
    }
}

fn setup_ap_label(settings: &DeviceSettings) -> &'static str {
    match settings.wifi.setup_ap_state {
        esp32_bms_gps::settings::SetupApState::FirstBoot => "FIRST",
        esp32_bms_gps::settings::SetupApState::Disabled => "OFF",
        esp32_bms_gps::settings::SetupApState::Reprovisioning => "ON",
    }
}

fn bms_label(settings: &DeviceSettings) -> &'static str {
    if settings.bms.bound_mac.is_some() {
        "BOUND"
    } else {
        "SCAN/BIND"
    }
}

fn wifi_label(state: WifiLinkState) -> &'static str {
    match state {
        WifiLinkState::SetupApOnly => "SETUP AP",
        WifiLinkState::StationConnecting => "CONNECTING",
        WifiLinkState::StationConnected => "CONNECTED",
        WifiLinkState::Offline => "OFFLINE",
    }
}

fn ota_label(state: OtaState) -> &'static str {
    match state {
        OtaState::Idle => "IDLE",
        OtaState::Checking => "CHECKING",
        OtaState::UpdateAvailable => "AVAILABLE",
        OtaState::Downloading => "DOWNLOADING",
        OtaState::Verifying => "VERIFYING",
        OtaState::ReadyToReboot => "READY",
        OtaState::Failed => "FAILED",
    }
}

fn write_u32(buf: &mut [u8], mut value: u32) -> usize {
    if value == 0 {
        buf[0] = b'0';
        return 1;
    }

    let mut scratch = [0_u8; 10];
    let mut len = 0;
    while value > 0 && len < scratch.len() {
        scratch[len] = b'0' + (value % 10) as u8;
        value /= 10;
        len += 1;
    }
    for index in 0..len {
        buf[index] = scratch[len - index - 1];
    }
    len
}

fn glyph_columns(byte: u8) -> [u8; 5] {
    match byte.to_ascii_uppercase() {
        b'0' => [0x3E, 0x51, 0x49, 0x45, 0x3E],
        b'1' => [0x00, 0x42, 0x7F, 0x40, 0x00],
        b'2' => [0x42, 0x61, 0x51, 0x49, 0x46],
        b'3' => [0x21, 0x41, 0x45, 0x4B, 0x31],
        b'4' => [0x18, 0x14, 0x12, 0x7F, 0x10],
        b'5' => [0x27, 0x45, 0x45, 0x45, 0x39],
        b'6' => [0x3C, 0x4A, 0x49, 0x49, 0x30],
        b'7' => [0x01, 0x71, 0x09, 0x05, 0x03],
        b'8' => [0x36, 0x49, 0x49, 0x49, 0x36],
        b'9' => [0x06, 0x49, 0x49, 0x29, 0x1E],
        b'A' => [0x7E, 0x11, 0x11, 0x11, 0x7E],
        b'B' => [0x7F, 0x49, 0x49, 0x49, 0x36],
        b'C' => [0x3E, 0x41, 0x41, 0x41, 0x22],
        b'D' => [0x7F, 0x41, 0x41, 0x22, 0x1C],
        b'E' => [0x7F, 0x49, 0x49, 0x49, 0x41],
        b'F' => [0x7F, 0x09, 0x09, 0x09, 0x01],
        b'G' => [0x3E, 0x41, 0x49, 0x49, 0x7A],
        b'H' => [0x7F, 0x08, 0x08, 0x08, 0x7F],
        b'I' => [0x00, 0x41, 0x7F, 0x41, 0x00],
        b'J' => [0x20, 0x40, 0x41, 0x3F, 0x01],
        b'K' => [0x7F, 0x08, 0x14, 0x22, 0x41],
        b'L' => [0x7F, 0x40, 0x40, 0x40, 0x40],
        b'M' => [0x7F, 0x02, 0x0C, 0x02, 0x7F],
        b'N' => [0x7F, 0x04, 0x08, 0x10, 0x7F],
        b'O' => [0x3E, 0x41, 0x41, 0x41, 0x3E],
        b'P' => [0x7F, 0x09, 0x09, 0x09, 0x06],
        b'Q' => [0x3E, 0x41, 0x51, 0x21, 0x5E],
        b'R' => [0x7F, 0x09, 0x19, 0x29, 0x46],
        b'S' => [0x46, 0x49, 0x49, 0x49, 0x31],
        b'T' => [0x01, 0x01, 0x7F, 0x01, 0x01],
        b'U' => [0x3F, 0x40, 0x40, 0x40, 0x3F],
        b'V' => [0x1F, 0x20, 0x40, 0x20, 0x1F],
        b'W' => [0x7F, 0x20, 0x18, 0x20, 0x7F],
        b'X' => [0x63, 0x14, 0x08, 0x14, 0x63],
        b'Y' => [0x07, 0x08, 0x70, 0x08, 0x07],
        b'Z' => [0x61, 0x51, 0x49, 0x45, 0x43],
        b'%' => [0x23, 0x13, 0x08, 0x64, 0x62],
        b'.' => [0x00, 0x60, 0x60, 0x00, 0x00],
        b'-' => [0x08, 0x08, 0x08, 0x08, 0x08],
        b'/' => [0x20, 0x10, 0x08, 0x04, 0x02],
        b':' => [0x00, 0x36, 0x36, 0x00, 0x00],
        b' ' => [0x00, 0x00, 0x00, 0x00, 0x00],
        _ => [0x7F, 0x41, 0x5D, 0x41, 0x7F],
    }
}

fn madctl(rotation: DisplayRotation) -> u8 {
    let rotation_bits = match rotation {
        DisplayRotation::Portrait => 0,
        DisplayRotation::Landscape => MADCTL_MV | MADCTL_MX,
        DisplayRotation::InvertedPortrait => MADCTL_MX | MADCTL_MY,
        DisplayRotation::InvertedLandscape => MADCTL_MV | MADCTL_MY,
    };

    rotation_bits | MADCTL_BGR
}

fn pack_u16_pair(first: u16, second: u16) -> [u8; 4] {
    [
        (first >> 8) as u8,
        first as u8,
        (second >> 8) as u8,
        second as u8,
    ]
}
