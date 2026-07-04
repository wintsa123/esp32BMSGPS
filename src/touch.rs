use esp_hal::{
    delay::Delay,
    gpio::{Input, Output},
};
use esp32_bms_gps::settings::RawTouchPoint;

const READ_X: u8 = 0xD0;
const READ_Y: u8 = 0x90;
const SAMPLE_COUNT: u16 = 8;

pub struct Xpt2046<'d> {
    clk: Output<'d>,
    mosi: Output<'d>,
    miso: Input<'d>,
    cs: Output<'d>,
    irq: Input<'d>,
    delay: Delay,
}

impl<'d> Xpt2046<'d> {
    pub fn new(
        mut clk: Output<'d>,
        mosi: Output<'d>,
        miso: Input<'d>,
        mut cs: Output<'d>,
        irq: Input<'d>,
        delay: Delay,
    ) -> Self {
        clk.set_low();
        cs.set_high();
        Self {
            clk,
            mosi,
            miso,
            cs,
            irq,
            delay,
        }
    }

    pub fn is_touched(&self) -> bool {
        self.irq.is_low()
    }

    pub fn wait_for_touch(&mut self) -> RawTouchPoint {
        while !self.is_touched() {
            self.delay.delay_millis(5);
        }
        self.delay.delay_millis(20);
        self.read_raw_average()
            .unwrap_or_else(|| self.read_raw_once())
    }

    pub fn wait_for_release(&mut self) {
        while self.is_touched() {
            self.delay.delay_millis(10);
        }
        self.delay.delay_millis(60);
    }

    pub fn read_raw_average(&mut self) -> Option<RawTouchPoint> {
        if !self.is_touched() {
            return None;
        }

        let mut sum_x = 0_u32;
        let mut sum_y = 0_u32;
        let mut samples = 0_u16;

        for _ in 0..SAMPLE_COUNT {
            if !self.is_touched() {
                break;
            }

            let point = self.read_raw_once();
            sum_x += point.x as u32;
            sum_y += point.y as u32;
            samples += 1;
            self.delay.delay_millis(2);
        }

        if samples == 0 {
            return None;
        }

        Some(RawTouchPoint {
            x: (sum_x / samples as u32) as u16,
            y: (sum_y / samples as u32) as u16,
        })
    }

    fn read_raw_once(&mut self) -> RawTouchPoint {
        RawTouchPoint {
            x: self.read_axis(READ_X),
            y: self.read_axis(READ_Y),
        }
    }

    fn read_axis(&mut self, command: u8) -> u16 {
        self.cs.set_low();
        self.transfer_byte(command);
        let high = self.transfer_byte(0);
        let low = self.transfer_byte(0);
        self.cs.set_high();

        (((high as u16) << 8) | low as u16) >> 3
    }

    fn transfer_byte(&mut self, mut byte: u8) -> u8 {
        let mut received = 0_u8;

        for _ in 0..8 {
            if byte & 0x80 != 0 {
                self.mosi.set_high();
            } else {
                self.mosi.set_low();
            }

            self.delay.delay_micros(1);
            self.clk.set_high();
            self.delay.delay_micros(1);
            received <<= 1;
            if self.miso.is_high() {
                received |= 1;
            }
            self.clk.set_low();
            self.delay.delay_micros(1);
            byte <<= 1;
        }

        received
    }
}
