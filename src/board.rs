#![allow(dead_code)]

pub const FLASH_SIZE_BYTES: u32 = 4 * 1024 * 1024;

pub mod battery {
    pub const BAT_ADC: u8 = 34;
}

pub mod rgb_led {
    pub const R: u8 = 17;
    pub const G: u8 = 22;
    pub const B: u8 = 16;
}

pub mod audio {
    pub const AUDIO_IN: u8 = 26;
    pub const AUDIO_EN: u8 = 4;
}

pub mod expansion_spi {
    pub const SPI_CS: u8 = 27;
}

pub mod sd_spi {
    pub const MOSI: u8 = 23;
    pub const MISO: u8 = 19;
    pub const SCK: u8 = 18;
    pub const CS: u8 = 5;
}

pub mod tft {
    use esp_hal::gpio::Level;

    pub const WIDTH: u16 = 240;
    pub const HEIGHT: u16 = 320;
    pub const COLOR_ORDER: &str = "BGR";

    pub const MISO: u8 = 12;
    pub const MOSI: u8 = 13;
    pub const SCLK: u8 = 14;
    pub const CS: u8 = 15;
    pub const DC: u8 = 2;
    pub const RST: Option<u8> = None;
    pub const BL: u8 = 21;
    pub const BACKLIGHT_ON_LEVEL: Level = Level::High;
    pub const SPI_FREQUENCY_MHZ: u32 = 1;
    pub const INVERT_COLORS: bool = true;
}

pub mod touch {
    use esp32_bms_gps::settings::TouchCalibration;

    pub const IRQ: u8 = 36;
    pub const MISO: u8 = 39;
    pub const MOSI: u8 = 32;
    pub const CS: u8 = 33;
    pub const CLK: u8 = 25;

    // First boot calibrates and persists to flash. Turn this on only when you
    // want to force a fresh calibration on every boot during bring-up.
    pub const CALIBRATE_ON_BOOT: bool = false;
    pub const FACTORY_CALIBRATION: Option<TouchCalibration> = Some(TouchCalibration {
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
}

pub mod storage {
    // Reserve the final 64 KiB of the 4 MiB flash for app-managed settings.
    // This stays outside the current partition table so espflash can continue to
    // parse partitions.csv without custom subtype issues.
    pub const SETTINGS_OFFSET: u32 = 0x3F0000;
    pub const SETTINGS_SIZE: u32 = 0x10000;
    pub const DEVICE_SETTINGS_SLOT_SIZE: u32 = 0x1000;
    pub const DEVICE_SETTINGS_SLOT_A_OFFSET: u32 = SETTINGS_OFFSET;
    pub const DEVICE_SETTINGS_SLOT_B_OFFSET: u32 = SETTINGS_OFFSET + DEVICE_SETTINGS_SLOT_SIZE;
    pub const TOUCH_CALIBRATION_OFFSET: u32 = SETTINGS_OFFSET;
}

pub mod gps {
    pub const BAUD: u32 = 9_600;
    pub const UART: &str = "UART0";
    pub const TXD0: u8 = 1;
    pub const RXD0: u8 = 3;
}

pub mod boot_risk {
    pub const STRAPPING_PINS_IN_USE: &[u8] = &[2, 4, 5, 12, 15];
    pub const UART0_PINS: &[u8] = &[1, 3];
}
