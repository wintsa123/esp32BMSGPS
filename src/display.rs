use esp_hal::{
    Blocking,
    delay::Delay,
    gpio::Output,
    spi::{Error as SpiError, master::Spi},
};
use esp32_bms_gps::{
    app_state::DashboardSnapshot,
    gps_nmea::SpeedUnit,
    qr::QrMatrix,
    settings::{DeviceSettings, DisplayRotation, Language, ScreenPoint},
    touch_ui,
};

use crate::{board, cjk_font};

const SWRESET: u8 = 0x01;
const SLPOUT: u8 = 0x11;
const NORON: u8 = 0x13;
const INVOFF: u8 = 0x20;
const INVON: u8 = 0x21;
const GAMMASET: u8 = 0x26;
const RDDID: u8 = 0x04;
const CASET: u8 = 0x2A;
const RASET: u8 = 0x2B;
const RAMWR: u8 = 0x2C;
const MADCTL: u8 = 0x36;
const COLMOD: u8 = 0x3A;
const DISPON: u8 = 0x29;
const FRMCTR1: u8 = 0xB1;
const DFUNCTR: u8 = 0xB6;
const PWCTR1: u8 = 0xC0;
const PWCTR2: u8 = 0xC1;
const VMCTR1: u8 = 0xC5;
const VMCTR2: u8 = 0xC7;
const PGAMCTRL: u8 = 0xE0;
const NGAMCTRL: u8 = 0xE1;

const MADCTL_BGR: u8 = 0x08;
const MADCTL_MV: u8 = 0x20;
const MADCTL_MX: u8 = 0x40;
const MADCTL_MY: u8 = 0x80;
const RGB565: u8 = 0x55;

pub const BLACK: u16 = 0x0000;
pub const BLUE: u16 = 0x001F;
pub const GREEN: u16 = 0x07E0;
pub const RED: u16 = 0xF800;
pub const WHITE: u16 = 0xFFFF;
pub const YELLOW: u16 = 0xFFE0;
pub const CYAN: u16 = 0x07FF;
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

    pub fn wait_for_power(&mut self) {
        self.cs.set_high();
        self.delay.delay_millis(board::tft::POWER_ON_DELAY_MS);
    }

    pub fn init(
        &mut self,
        controller: board::tft::Controller,
        rotation: DisplayRotation,
    ) -> Result<(), SpiError> {
        self.init_with_inversion(controller, rotation, board::tft::INVERT_COLORS)
    }

    pub fn init_with_inversion(
        &mut self,
        controller: board::tft::Controller,
        rotation: DisplayRotation,
        invert_colors: bool,
    ) -> Result<(), SpiError> {
        self.cs.set_high();
        self.delay.delay_millis(20);

        self.command(SWRESET)?;
        self.delay.delay_millis(150);

        match controller {
            board::tft::Controller::St7789 => {
                self.command(SLPOUT)?;
                self.delay.delay_millis(120);
                self.init_st7789_power()?;
            }
            board::tft::Controller::Ili9341 => {
                self.init_ili9341_power()?;
                self.command(SLPOUT)?;
                self.delay.delay_millis(120);
            }
        }

        self.command_data(COLMOD, &[RGB565])?;
        self.set_rotation(rotation)?;

        if invert_colors {
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

    fn init_st7789_power(&mut self) -> Result<(), SpiError> {
        self.command_data(0xB2, &[0x0C, 0x0C, 0x00, 0x33, 0x33])?;
        self.command_data(0xB7, &[0x35])?;
        self.command_data(0xBB, &[0x19])?;
        self.command_data(0xC0, &[0x2C])?;
        self.command_data(0xC2, &[0x01])?;
        self.command_data(0xC3, &[0x12])?;
        self.command_data(0xC4, &[0x20])?;
        self.command_data(0xC6, &[0x0F])?;
        self.command_data(0xD0, &[0xA4, 0xA1])?;
        self.command_data(
            PGAMCTRL,
            &[
                0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
            ],
        )?;
        self.command_data(
            NGAMCTRL,
            &[
                0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
            ],
        )
    }

    fn init_ili9341_power(&mut self) -> Result<(), SpiError> {
        self.command_data(0xEF, &[0x03, 0x80, 0x02])?;
        self.command_data(0xCF, &[0x00, 0xC1, 0x30])?;
        self.command_data(0xED, &[0x64, 0x03, 0x12, 0x81])?;
        self.command_data(0xE8, &[0x85, 0x00, 0x78])?;
        self.command_data(0xCB, &[0x39, 0x2C, 0x00, 0x34, 0x02])?;
        self.command_data(0xF7, &[0x20])?;
        self.command_data(0xEA, &[0x00, 0x00])?;
        self.command_data(PWCTR1, &[0x23])?;
        self.command_data(PWCTR2, &[0x10])?;
        self.command_data(VMCTR1, &[0x3E, 0x28])?;
        self.command_data(VMCTR2, &[0x86])?;
        self.command_data(FRMCTR1, &[0x00, 0x18])?;
        self.command_data(DFUNCTR, &[0x08, 0x82, 0x27])?;
        self.command_data(0xF2, &[0x00])?;
        self.command_data(GAMMASET, &[0x01])?;
        self.command_data(
            PGAMCTRL,
            &[
                0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09,
                0x00,
            ],
        )?;
        self.command_data(
            NGAMCTRL,
            &[
                0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36,
                0x0F,
            ],
        )
    }

    pub fn clear(&mut self, color: u16) -> Result<(), SpiError> {
        let (w, h) = self.rotation.logical_size();
        self.fill_rect(0, 0, w, h, color)
    }

    pub fn read_id(&mut self) -> Result<[u8; 4], SpiError> {
        let mut id = [0_u8; 4];
        self.dc.set_low();
        self.cs.set_low();
        self.spi.write(&[RDDID])?;
        self.dc.set_high();
        let result = self.spi.read(&mut id);
        self.cs.set_high();
        result.map(|_| id)
    }

    pub fn draw_boot_diagnostics(&mut self) -> Result<(), SpiError> {
        let (width, height) = self.rotation.logical_size();
        let band = (height / 4).max(1);
        self.fill_rect(0, 0, width, band, RED)?;
        self.fill_rect(0, band, width, band, GREEN)?;
        self.fill_rect(0, band * 2, width, band, BLUE)?;
        self.fill_rect(0, band * 3, width, height.saturating_sub(band * 3), WHITE)?;
        self.draw_text(34, 28, 4, WHITE, "TFT")?;
        self.draw_text(34, 78, 4, BLACK, "OK")
    }

    pub fn write_raw_screen_color(&mut self, color: u16) -> Result<(), SpiError> {
        self.command(RAMWR)?;
        self.write_color_pixels(color, board::tft::WIDTH as u32 * board::tft::HEIGHT as u32)
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
        _firmware_version: &str,
    ) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        let (width, height) = self.rotation.logical_size();
        self.draw_bms_dashboard(snapshot, width, height)?;
        self.draw_dashboard_pull_hint(width)
    }

    pub fn draw_dashboard_notice(&mut self, title: &str, body: &str) -> Result<(), SpiError> {
        let (width, height) = self.rotation.logical_size();
        let notice_width = width.saturating_sub(28).min(184);
        let notice_height = 58.min(height.saturating_sub(16));
        let x = (width.saturating_sub(notice_width)) / 2;
        let y = (height.saturating_sub(notice_height)) / 2;

        self.fill_rounded_rect(
            x.saturating_add(3),
            y.saturating_add(3),
            notice_width,
            notice_height,
            6,
            BLACK,
        )?;
        self.fill_rounded_rect(x, y, notice_width, notice_height, 6, 0x3186)?;
        self.draw_centered_text(width / 2, y + 13, 2, YELLOW, title)?;
        self.draw_centered_text(width / 2, y + 38, 1, WHITE, body)
    }

    pub fn draw_quick_menu(&mut self) -> Result<(), SpiError> {
        self.draw_quick_menu_panel(touch_ui::QUICK_MENU_PANEL_HEIGHT)
    }

    pub fn draw_quick_menu_panel(&mut self, panel_height: u16) -> Result<(), SpiError> {
        let (width, height) = self.rotation.logical_size();
        let panel_height = panel_height
            .min(touch_ui::QUICK_MENU_PANEL_HEIGHT)
            .min(height);
        if panel_height == 0 {
            return Ok(());
        }

        let (settings_center, rotate_center) = touch_ui::quick_menu_button_centers(width);

        self.fill_rect(0, 0, width, panel_height, DARK_GRAY)?;
        if panel_height >= 18 {
            self.fill_rect(width / 2 - 22, panel_height.saturating_sub(12), 44, 4, GRAY)?;
        }

        let button_bottom = touch_ui::QUICK_MENU_BUTTON_RADIUS
            + settings_center.y
            + touch_ui::QUICK_MENU_BUTTON_RADIUS / 4;
        if panel_height >= button_bottom {
            self.draw_round_button(settings_center, touch_ui::QUICK_MENU_BUTTON_RADIUS, CYAN)?;
            self.draw_round_button(rotate_center, touch_ui::QUICK_MENU_BUTTON_RADIUS, YELLOW)?;
            self.draw_settings_icon(settings_center, WHITE)?;
            self.draw_rotate_icon(rotate_center, WHITE)?;
        }

        Ok(())
    }

    pub fn draw_settings_menu(&mut self, settings: &DeviceSettings) -> Result<(), SpiError> {
        self.clear(BLACK)?;
        self.draw_text_zh(10, 8, 2, CYAN, "设置")?;
        self.draw_text_zh(212, 12, 1, DARK_GRAY, "顶部返回")?;
        self.draw_menu_row_value(0, "无线热点", setup_ap_label(settings))?;
        self.draw_menu_row_u8(1, "亮度", settings.brightness_percent, "%")?;
        self.draw_menu_row_value(2, "方向", rotation_label(settings.display_rotation))?;
        self.draw_menu_row_value(3, "速度", speed_unit_label(settings.speed_unit))?;
        self.draw_menu_row_value(4, "语言", language_label(settings.language))?;
        self.draw_menu_row_value(5, "电池绑定", bms_label(settings))?;
        self.draw_menu_row_value(6, "恢复默认", "")
    }

    #[allow(dead_code)]
    pub fn draw_settings_menu_row(
        &mut self,
        index: u16,
        settings: &DeviceSettings,
    ) -> Result<(), SpiError> {
        match index {
            0 => self.draw_menu_row_value(0, "无线热点", setup_ap_label(settings)),
            1 => self.draw_menu_row_u8(1, "亮度", settings.brightness_percent, "%"),
            2 => self.draw_menu_row_value(2, "方向", rotation_label(settings.display_rotation)),
            3 => self.draw_menu_row_value(3, "速度", speed_unit_label(settings.speed_unit)),
            4 => self.draw_menu_row_value(4, "语言", language_label(settings.language)),
            5 => self.draw_menu_row_value(5, "电池绑定", bms_label(settings)),
            6 => self.draw_menu_row_value(6, "恢复默认", ""),
            _ => Ok(()),
        }
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

    pub fn draw_text_zh(
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
        for ch in text.chars() {
            if ch.is_ascii() {
                self.draw_char(cursor_x, y, scale, color, ch as u8)?;
                cursor_x = cursor_x.saturating_add(6 * scale);
            } else if let Some(rows) = cjk_font::glyph_rows(ch) {
                self.draw_cjk_char(cursor_x, y, scale, color, rows)?;
                cursor_x = cursor_x.saturating_add(17 * scale);
            } else {
                self.draw_char(cursor_x, y, scale, color, b'?')?;
                cursor_x = cursor_x.saturating_add(6 * scale);
            }
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
        self.write_color_pixels(color, width as u32 * height as u32)
    }

    pub fn write_rgb565_window(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        height: u16,
        data: &[u8],
    ) -> Result<(), SpiError> {
        let (screen_width, screen_height) = self.rotation.logical_size();
        if width == 0 || height == 0 || x >= screen_width || y >= screen_height {
            return Ok(());
        }

        let width = width.min(screen_width - x);
        let height = height.min(screen_height - y);
        let needed = width as usize * height as usize * 2;
        let data = &data[..data.len().min(needed)];

        self.set_window(x, y, x + width - 1, y + height - 1)?;
        self.command(RAMWR)?;
        self.dc.set_high();
        self.cs.set_low();
        for chunk in data.chunks(1024) {
            self.spi.write(chunk)?;
        }
        self.cs.set_high();
        Ok(())
    }

    fn write_color_pixels(&mut self, color: u16, mut pixels: u32) -> Result<(), SpiError> {
        self.dc.set_high();
        self.cs.set_low();

        let hi = (color >> 8) as u8;
        let lo = color as u8;
        let mut chunk = [0_u8; 128];
        for pair in chunk.chunks_exact_mut(2) {
            pair[0] = hi;
            pair[1] = lo;
        }

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

    fn draw_bms_dashboard(
        &mut self,
        snapshot: &DashboardSnapshot,
        width: u16,
        height: u16,
    ) -> Result<(), SpiError> {
        let margin = 8;
        let gap = 6;
        let content_width = width.saturating_sub(margin * 2).min(304);
        let origin_x = (width.saturating_sub(content_width)) / 2;
        let origin_y = if height >= 240 { 14 } else { 8 };
        let left_width = ((content_width * 43) / 100).clamp(104, 132);
        let right_x = origin_x + left_width + gap;
        let right_width = content_width.saturating_sub(left_width + gap);
        let card = 0x18C3;
        let blue = 0x24BF;

        self.fill_rounded_rect(origin_x, origin_y, left_width, 84, 6, blue)?;
        self.draw_soc_value(origin_x, origin_y, left_width, snapshot.soc_percent)?;

        self.draw_pack_value(right_x, origin_y + 6, right_width, snapshot.pack_voltage_mv)?;
        self.draw_current_value(
            right_x,
            origin_y + 48,
            right_width,
            snapshot.current_deci_amps,
        )?;

        let mid_y = origin_y + 92;
        let cap_width = (left_width.saturating_sub(gap)) / 2;
        self.draw_capacity_card(
            origin_x,
            mid_y,
            cap_width,
            "TOTAL",
            snapshot.total_capacity_mah,
        )?;
        self.draw_capacity_card(
            origin_x + cap_width + gap,
            mid_y,
            cap_width,
            "LEFT",
            snapshot.capacity_remaining_mah,
        )?;
        self.draw_cell_stats_card(right_x, mid_y, right_width, snapshot, card)?;

        let bottom_y = mid_y + 84;
        let temp_width = (content_width * 58) / 100;
        self.fill_rounded_rect(origin_x, bottom_y, temp_width, 48, 5, card)?;
        self.draw_temperature_grid(origin_x + 7, bottom_y + 8, snapshot)?;

        let aux_x = origin_x + temp_width + gap;
        let aux_width = content_width.saturating_sub(temp_width + gap);
        self.fill_rounded_rect(aux_x, bottom_y, aux_width, 48, 5, card)?;
        self.draw_temperature_line(
            aux_x + 8,
            bottom_y + 10,
            "BAL",
            snapshot.temperature_celsius[4],
        )?;
        self.draw_temperature_line(
            aux_x + 8,
            bottom_y + 28,
            "MOS",
            snapshot.temperature_celsius[5],
        )?;

        Ok(())
    }

    fn draw_soc_value(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        soc_percent: Option<u16>,
    ) -> Result<(), SpiError> {
        let mut buf = [0_u8; 8];
        let text = match soc_percent {
            Some(value) => {
                let len = write_u32(&mut buf, value.min(999) as u32);
                buf[len] = b'%';
                core::str::from_utf8(&buf[..len + 1]).unwrap_or("--%")
            }
            None => "--%",
        };
        self.draw_centered_text(x + width / 2, y + 25, 5, WHITE, text)
    }

    fn draw_pack_value(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        pack_voltage_mv: Option<u32>,
    ) -> Result<(), SpiError> {
        match pack_voltage_mv {
            Some(value) => {
                self.draw_u32_deci(x + width.saturating_sub(102), y, 3, WHITE, value / 100, "V")
            }
            None => self.draw_text(x + width.saturating_sub(86), y, 3, WHITE, "--.-V"),
        }
    }

    fn draw_current_value(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        current_deci_amps: Option<i16>,
    ) -> Result<(), SpiError> {
        match current_deci_amps {
            Some(value) => {
                self.draw_i16_deci(x + width.saturating_sub(84), y, 3, WHITE, value, "A")
            }
            None => self.draw_text(x + width.saturating_sub(86), y, 3, WHITE, "--.-A"),
        }
    }

    fn draw_capacity_card(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        label: &str,
        value_mah: Option<u32>,
    ) -> Result<(), SpiError> {
        self.fill_rounded_rect(x, y, width, 56, 5, DARK_GRAY)?;
        self.draw_text(x + 7, y + 8, 1, GRAY, label)?;
        self.draw_mah_as_ah(x + 7, y + 30, value_mah)
    }

    fn draw_cell_stats_card(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        snapshot: &DashboardSnapshot,
        color: u16,
    ) -> Result<(), SpiError> {
        self.fill_rounded_rect(x, y, width, 78, 5, color)?;
        self.draw_cell_stat_row(x + 8, y + 8, "MAX", snapshot.max_cell_voltage_mv)?;
        self.draw_cell_stat_row(x + 8, y + 24, "MIN", snapshot.min_cell_voltage_mv)?;
        self.draw_cell_stat_row(x + 8, y + 40, "DIF", snapshot.delta_cell_voltage_mv)?;
        self.draw_cell_stat_row(x + 8, y + 56, "AVG", snapshot.average_cell_voltage_mv)
    }

    fn draw_cell_stat_row(
        &mut self,
        x: u16,
        y: u16,
        label: &str,
        value_mv: Option<u16>,
    ) -> Result<(), SpiError> {
        self.draw_text(x, y, 1, GRAY, label)?;
        self.draw_mv_as_v3(x + 38, y, value_mv)
    }

    fn draw_temperature_grid(
        &mut self,
        x: u16,
        y: u16,
        snapshot: &DashboardSnapshot,
    ) -> Result<(), SpiError> {
        self.draw_temperature_line(x, y, "T1", snapshot.temperature_celsius[0])?;
        self.draw_temperature_line(x + 78, y, "T2", snapshot.temperature_celsius[1])?;
        self.draw_temperature_line(x, y + 20, "T3", snapshot.temperature_celsius[2])?;
        self.draw_temperature_line(x + 78, y + 20, "T4", snapshot.temperature_celsius[3])
    }

    fn draw_temperature_line(
        &mut self,
        x: u16,
        y: u16,
        label: &str,
        temperature: Option<i16>,
    ) -> Result<(), SpiError> {
        self.draw_text(x, y, 1, GRAY, label)?;
        self.draw_text(x + text_width(label, 1) + 4, y, 1, WHITE, ":")?;
        self.draw_temperature_value(x + text_width(label, 1) + 12, y, temperature)
    }

    fn draw_dashboard_pull_hint(&mut self, width: u16) -> Result<(), SpiError> {
        let center_x = width / 2;
        self.fill_rect(center_x.saturating_sub(24), 5, 48, 3, DARK_GRAY)?;
        self.fill_rect(center_x.saturating_sub(14), 11, 28, 2, GRAY)
    }

    fn fill_rounded_rect(
        &mut self,
        x: u16,
        y: u16,
        width: u16,
        height: u16,
        radius: u16,
        color: u16,
    ) -> Result<(), SpiError> {
        let radius = radius.min(width / 2).min(height / 2);
        if radius == 0 {
            return self.fill_rect(x, y, width, height, color);
        }

        self.fill_rect(
            x + radius,
            y,
            width.saturating_sub(radius * 2),
            height,
            color,
        )?;
        self.fill_rect(
            x,
            y + radius,
            width,
            height.saturating_sub(radius * 2),
            color,
        )?;
        self.fill_circle(
            ScreenPoint {
                x: x + radius,
                y: y + radius,
            },
            radius,
            color,
        )?;
        self.fill_circle(
            ScreenPoint {
                x: x + width.saturating_sub(radius + 1),
                y: y + radius,
            },
            radius,
            color,
        )?;
        self.fill_circle(
            ScreenPoint {
                x: x + radius,
                y: y + height.saturating_sub(radius + 1),
            },
            radius,
            color,
        )?;
        self.fill_circle(
            ScreenPoint {
                x: x + width.saturating_sub(radius + 1),
                y: y + height.saturating_sub(radius + 1),
            },
            radius,
            color,
        )
    }

    fn draw_round_button(
        &mut self,
        center: ScreenPoint,
        radius: u16,
        accent: u16,
    ) -> Result<(), SpiError> {
        self.fill_circle(center, radius.saturating_add(3), accent)?;
        self.fill_circle(center, radius, BLACK)
    }

    fn draw_settings_icon(&mut self, center: ScreenPoint, color: u16) -> Result<(), SpiError> {
        self.fill_rect(
            center.x.saturating_sub(3),
            center.y.saturating_sub(18),
            6,
            8,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_sub(3),
            center.y.saturating_add(10),
            6,
            8,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_sub(18),
            center.y.saturating_sub(3),
            8,
            6,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_add(10),
            center.y.saturating_sub(3),
            8,
            6,
            color,
        )?;
        self.fill_circle(center, 12, color)?;
        self.fill_circle(center, 6, BLACK)
    }

    fn draw_rotate_icon(&mut self, center: ScreenPoint, color: u16) -> Result<(), SpiError> {
        self.fill_rect(
            center.x.saturating_sub(13),
            center.y.saturating_sub(12),
            22,
            4,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_add(5),
            center.y.saturating_sub(12),
            4,
            16,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_add(7),
            center.y.saturating_sub(18),
            4,
            10,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_add(11),
            center.y.saturating_sub(14),
            4,
            6,
            color,
        )?;

        self.fill_rect(
            center.x.saturating_sub(9),
            center.y.saturating_add(8),
            22,
            4,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_sub(9),
            center.y.saturating_sub(4),
            4,
            16,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_sub(11),
            center.y.saturating_add(8),
            4,
            10,
            color,
        )?;
        self.fill_rect(
            center.x.saturating_sub(15),
            center.y.saturating_add(8),
            4,
            6,
            color,
        )
    }

    fn fill_circle(
        &mut self,
        center: ScreenPoint,
        radius: u16,
        color: u16,
    ) -> Result<(), SpiError> {
        let radius = radius as i32;
        let radius_squared = radius * radius;
        let mut dy = -radius;
        while dy <= radius {
            let mut dx = radius;
            while dx > 0 && dx * dx + dy * dy > radius_squared {
                dx -= 1;
            }
            self.fill_hline_i32(
                center.x as i32 - dx,
                center.y as i32 + dy,
                dx * 2 + 1,
                color,
            )?;
            dy += 1;
        }
        Ok(())
    }

    fn fill_hline_i32(&mut self, x: i32, y: i32, width: i32, color: u16) -> Result<(), SpiError> {
        let (screen_width, screen_height) = self.rotation.logical_size();
        if width <= 0 || y < 0 || y >= screen_height as i32 {
            return Ok(());
        }

        let x0 = x.max(0);
        let x1 = (x + width - 1).min(screen_width as i32 - 1);
        if x1 < x0 {
            return Ok(());
        }

        self.fill_rect(x0 as u16, y as u16, (x1 - x0 + 1) as u16, 1, color)
    }

    fn draw_centered_text(
        &mut self,
        center_x: u16,
        y: u16,
        scale: u16,
        color: u16,
        text: &str,
    ) -> Result<(), SpiError> {
        let width = text_width(text, scale);
        self.draw_text(center_x.saturating_sub(width / 2), y, scale, color, text)
    }

    fn draw_menu_row_value(
        &mut self,
        index: u16,
        label: &str,
        value: &str,
    ) -> Result<(), SpiError> {
        let y = 48 + index * 28;
        self.fill_rect(8, y - 4, 304, 24, DARK_GRAY)?;
        self.draw_text_zh(18, y, 1, WHITE, label)?;
        self.draw_text_zh(128, y, 1, YELLOW, value)
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
        self.draw_text_zh(18, y, 1, WHITE, label)?;
        self.draw_u16(128, y, 1, YELLOW, value as u16, suffix)
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

    fn draw_mah_as_ah(&mut self, x: u16, y: u16, value_mah: Option<u32>) -> Result<(), SpiError> {
        let mut buf = [0_u8; 16];
        let text = match value_mah {
            Some(value) => {
                let len = write_mah_as_ah(&mut buf, value);
                core::str::from_utf8(&buf[..len]).unwrap_or("--.--AH")
            }
            None => "--.--AH",
        };
        self.draw_text(x, y, 1, WHITE, text)
    }

    fn draw_mv_as_v3(&mut self, x: u16, y: u16, value_mv: Option<u16>) -> Result<(), SpiError> {
        let mut buf = [0_u8; 12];
        let text = match value_mv {
            Some(value) => {
                let len = write_mv_as_v3(&mut buf, value);
                core::str::from_utf8(&buf[..len]).unwrap_or("-.---V")
            }
            None => "-.---V",
        };
        self.draw_text(x, y, 1, WHITE, text)
    }

    fn draw_temperature_value(
        &mut self,
        x: u16,
        y: u16,
        temperature: Option<i16>,
    ) -> Result<(), SpiError> {
        let mut buf = [0_u8; 8];
        let text = match temperature {
            Some(value) => {
                let len = write_i16_suffix(&mut buf, value, "C");
                core::str::from_utf8(&buf[..len]).unwrap_or("--C")
            }
            None => "--C",
        };
        self.draw_text(x, y, 1, WHITE, text)
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

    fn draw_cjk_char(
        &mut self,
        x: u16,
        y: u16,
        scale: u16,
        color: u16,
        rows: &[u16; 16],
    ) -> Result<(), SpiError> {
        for (row, bits) in rows.iter().copied().enumerate() {
            for col in 0..16 {
                if bits & (1 << (15 - col)) != 0 {
                    self.fill_rect(
                        x + col as u16 * scale,
                        y + row as u16 * scale,
                        scale,
                        scale,
                        color,
                    )?;
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

pub fn controller_from_rddid(id: [u8; 4]) -> Option<board::tft::Controller> {
    if id[2] == 0x93 && id[3] == 0x41 {
        return Some(board::tft::Controller::Ili9341);
    }
    if (id[1] == 0x85 && id[2] == 0x52) || (id[1] == 0x77 && id[2] == 0x89) {
        return Some(board::tft::Controller::St7789);
    }
    None
}

fn speed_unit_label(unit: SpeedUnit) -> &'static str {
    match unit {
        SpeedUnit::Kmh => "KM/H",
        SpeedUnit::Mph => "MPH",
    }
}

fn language_label(language: Language) -> &'static str {
    match language {
        Language::Chinese => "中文",
        Language::English => "英文",
    }
}

fn rotation_label(rotation: DisplayRotation) -> &'static str {
    match rotation {
        DisplayRotation::Portrait => "竖屏",
        DisplayRotation::Landscape => "横屏",
        DisplayRotation::InvertedPortrait => "反向竖屏",
        DisplayRotation::InvertedLandscape => "反向横屏",
    }
}

fn setup_ap_label(settings: &DeviceSettings) -> &'static str {
    match settings.wifi.setup_ap_state {
        esp32_bms_gps::settings::SetupApState::FirstBoot => "首启",
        esp32_bms_gps::settings::SetupApState::Disabled => "关",
        esp32_bms_gps::settings::SetupApState::Reprovisioning => "开启",
    }
}

fn bms_label(settings: &DeviceSettings) -> &'static str {
    if settings.bms.bound_mac.is_some() {
        "已绑定"
    } else {
        "扫码绑定"
    }
}

fn text_width(text: &str, scale: u16) -> u16 {
    text.len() as u16 * 6 * scale
}

fn write_mah_as_ah(buf: &mut [u8], value_mah: u32) -> usize {
    let whole = value_mah / 1000;
    let frac = (value_mah % 1000) / 10;
    let len = write_u32(buf, whole);
    if len + 5 <= buf.len() {
        buf[len] = b'.';
        buf[len + 1] = b'0' + (frac / 10) as u8;
        buf[len + 2] = b'0' + (frac % 10) as u8;
        buf[len + 3] = b'A';
        buf[len + 4] = b'H';
        len + 5
    } else {
        len
    }
}

fn write_mv_as_v3(buf: &mut [u8], value_mv: u16) -> usize {
    let whole = value_mv / 1000;
    let frac = value_mv % 1000;
    let len = write_u32(buf, whole as u32);
    if len + 5 <= buf.len() {
        buf[len] = b'.';
        buf[len + 1] = b'0' + (frac / 100) as u8;
        buf[len + 2] = b'0' + ((frac / 10) % 10) as u8;
        buf[len + 3] = b'0' + (frac % 10) as u8;
        buf[len + 4] = b'V';
        len + 5
    } else {
        len
    }
}

fn write_i16_suffix(buf: &mut [u8], value: i16, suffix: &str) -> usize {
    let mut cursor = 0;
    let magnitude = if value < 0 {
        buf[0] = b'-';
        cursor = 1;
        value.saturating_abs() as u16
    } else {
        value as u16
    };
    cursor += write_u32(&mut buf[cursor..], magnitude as u32);
    for byte in suffix.bytes() {
        if cursor >= buf.len() {
            break;
        }
        buf[cursor] = byte;
        cursor += 1;
    }
    cursor
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
