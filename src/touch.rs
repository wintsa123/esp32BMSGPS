use esp_hal::{
    delay::Delay,
    gpio::{Input, Output},
};
use esp32_bms_gps::settings::RawTouchPoint;

const READ_X: u8 = 0xD0;
const READ_Y: u8 = 0x90;
const READ_Z1: u8 = 0xB0;
const READ_Z2: u8 = 0xC0;
const SAMPLE_COUNT: u16 = 8;
const MIN_SAMPLES: u16 = 2;
const ADC_MAX: u16 = 4095;
const RAW_MIN: u16 = 64;
const RAW_MAX: u16 = 4031;
const RAW_FALLBACK_MIN: u16 = 180;
const RAW_FALLBACK_MAX: u16 = 3915;
const PRESSURE_MIN_Z1: u16 = 16;
const PRESSURE_MIN_DELTA: u16 = 8;

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
        loop {
            if let Some(point) = self.read_raw_average() {
                return point;
            }
            self.delay.delay_millis(5);
        }
    }

    pub fn wait_for_release(&mut self) {
        while self.read_touch_sample().is_some() {
            self.delay.delay_millis(10);
        }
        self.delay.delay_millis(60);
    }

    pub fn read_raw_average(&mut self) -> Option<RawTouchPoint> {
        let mut sum_x = 0_u32;
        let mut sum_y = 0_u32;
        let mut samples = 0_u16;

        for _ in 0..SAMPLE_COUNT {
            let Some(point) = self.read_touch_sample() else {
                if samples == 0 {
                    return None;
                }
                break;
            };
            sum_x += point.x as u32;
            sum_y += point.y as u32;
            samples += 1;
            self.delay.delay_millis(2);
        }

        if samples < MIN_SAMPLES {
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

    fn read_touch_sample(&mut self) -> Option<RawTouchPoint> {
        let irq_low = self.is_touched();
        let point = self.read_raw_once();
        let z1 = self.read_axis(READ_Z1);
        let z2 = self.read_axis(READ_Z2);

        if raw_point_valid(point)
            && (irq_low || pressure_valid(z1, z2) || raw_fallback_valid(point))
        {
            Some(point)
        } else {
            None
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

fn raw_point_valid(point: RawTouchPoint) -> bool {
    (RAW_MIN..=RAW_MAX).contains(&point.x) && (RAW_MIN..=RAW_MAX).contains(&point.y)
}

fn raw_fallback_valid(point: RawTouchPoint) -> bool {
    (RAW_FALLBACK_MIN..=RAW_FALLBACK_MAX).contains(&point.x)
        && (RAW_FALLBACK_MIN..=RAW_FALLBACK_MAX).contains(&point.y)
}

fn pressure_valid(z1: u16, z2: u16) -> bool {
    z1 >= PRESSURE_MIN_Z1 && z2 > z1.saturating_add(PRESSURE_MIN_DELTA) && z2 < ADC_MAX
}
