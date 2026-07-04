pub const SERVICE_UUID_16: u16 = 0xFFE0;
pub const CHARACTERISTIC_UUID_16: u16 = 0xFFE1;

pub const MAX_FRAME_LEN: usize = 192;
pub const MIN_FRAME_LEN: usize = 10;
pub const MAX_CELLS: usize = 32;
pub const MAX_TEMPERATURES: usize = 6;

pub const PKT_START_1: u8 = 0x7E;
pub const PKT_START_2: u8 = 0xA1;
pub const PKT_END_1: u8 = 0xAA;
pub const PKT_END_2: u8 = 0x55;

pub const FRAME_TYPE_STATUS: u8 = 0x11;
pub const FRAME_TYPE_DEVICE_INFO: u8 = 0x12;

pub const COMMAND_STATUS: u8 = 0x01;
pub const COMMAND_DEVICE_INFO: u8 = 0x02;
pub const COMMAND_WRITE_REGISTER: u8 = 0x51;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FrameError {
    Empty,
    TooShort,
    TooLong,
    BadStart,
    BadEnd,
    BadLength,
    BadCrc,
    UnexpectedFunction,
    UnsupportedCount,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ValidatedFrame {
    bytes: [u8; MAX_FRAME_LEN],
    len: usize,
    protocol_len: usize,
    function: u8,
}

impl ValidatedFrame {
    pub fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    pub const fn function(&self) -> u8 {
        self.function
    }

    pub fn payload(&self) -> &[u8] {
        &self.bytes[6..self.protocol_len - 4]
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FrameAssembler {
    buffer: [u8; MAX_FRAME_LEN],
    len: usize,
}

impl FrameAssembler {
    pub const fn new() -> Self {
        Self {
            buffer: [0; MAX_FRAME_LEN],
            len: 0,
        }
    }

    pub fn reset(&mut self) {
        self.len = 0;
    }

    pub fn push(&mut self, chunk: &[u8]) -> Result<Option<ValidatedFrame>, FrameError> {
        if chunk.is_empty() {
            return Ok(None);
        }

        if chunk.len() >= 2 && chunk[0] == PKT_START_1 && chunk[1] == PKT_START_2 {
            self.reset();
        } else if self.len == 0 && chunk[0] != PKT_START_1 {
            return Err(FrameError::BadStart);
        }

        if self.len + chunk.len() > MAX_FRAME_LEN {
            self.reset();
            return Err(FrameError::TooLong);
        }

        self.buffer[self.len..self.len + chunk.len()].copy_from_slice(chunk);
        self.len += chunk.len();

        if self.len >= MIN_FRAME_LEN
            && self.buffer[self.len - 2] == PKT_END_1
            && self.buffer[self.len - 1] == PKT_END_2
        {
            let frame = match validate_frame(&self.buffer[..self.len]) {
                Ok(frame) => frame,
                Err(error) => {
                    self.reset();
                    return Err(error);
                }
            };
            self.reset();
            return Ok(Some(frame));
        }

        Ok(None)
    }
}

impl Default for FrameAssembler {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BatteryStatus {
    Unknown,
    Idle,
    Charge,
    Discharge,
    Standby,
    Error,
    Other(u8),
}

impl BatteryStatus {
    pub const fn from_code(code: u8) -> Self {
        match code {
            0 => Self::Unknown,
            1 => Self::Idle,
            2 => Self::Charge,
            3 => Self::Discharge,
            4 => Self::Standby,
            5 => Self::Error,
            other => Self::Other(other),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MosfetStatus {
    Off,
    On,
    Other(u8),
}

impl MosfetStatus {
    pub const fn from_code(code: u8) -> Self {
        match code {
            0 => Self::Off,
            1 => Self::On,
            other => Self::Other(other),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BalancerStatus {
    Off,
    On,
    Other(u8),
}

impl BalancerStatus {
    pub const fn from_code(code: u8) -> Self {
        match code {
            0 => Self::Off,
            4 => Self::On,
            other => Self::Other(other),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BmsTelemetry {
    pub permissions: u8,
    pub battery_status: BatteryStatus,
    pub temperature_sensor_count: u8,
    pub cell_count: u8,
    pub cell_voltage_mv: [u16; MAX_CELLS],
    pub temperature_celsius: [i16; MAX_TEMPERATURES],
    pub temperature_count: u8,
    pub pack_voltage_mv: u32,
    pub current_deci_amps: i16,
    pub soc_percent: u16,
    pub state_of_health_percent: u16,
    pub charge_mosfet: MosfetStatus,
    pub discharge_mosfet: MosfetStatus,
    pub balancer: BalancerStatus,
    pub total_capacity_mah: u32,
    pub capacity_remaining_mah: u32,
    pub cycle_capacity_mah: u32,
    pub power_watts: i32,
    pub total_runtime_seconds: u32,
    pub balanced_cell_bitmask: u32,
    pub max_cell_voltage_mv: u16,
    pub max_voltage_cell: u16,
    pub min_cell_voltage_mv: u16,
    pub min_voltage_cell: u16,
    pub delta_cell_voltage_mv: u16,
    pub average_cell_voltage_mv: u16,
}

impl BmsTelemetry {
    pub fn cell_voltages(&self) -> &[u16] {
        &self.cell_voltage_mv[..self.cell_count as usize]
    }

    pub fn temperatures(&self) -> &[i16] {
        &self.temperature_celsius[..self.temperature_count as usize]
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AsciiText<const N: usize> {
    bytes: [u8; N],
    len: usize,
}

impl<const N: usize> AsciiText<N> {
    fn from_zero_padded(bytes: &[u8]) -> Self {
        let mut out = Self {
            bytes: [0; N],
            len: 0,
        };
        let mut index = 0;
        while index < bytes.len() && index < N {
            let byte = bytes[index];
            if byte == 0 {
                break;
            }
            out.bytes[index] = byte;
            out.len += 1;
            index += 1;
        }
        out
    }

    pub fn as_str(&self) -> &str {
        core::str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceInfo {
    pub device_model: AsciiText<16>,
    pub software_version: AsciiText<16>,
}

pub fn build_command_frame(function: u8, address: u16, value: u8) -> [u8; 10] {
    let mut frame = [0_u8; 10];
    frame[0] = PKT_START_1;
    frame[1] = PKT_START_2;
    frame[2] = function;
    frame[3] = address as u8;
    frame[4] = (address >> 8) as u8;
    frame[5] = value;
    let crc = crc16_modbus(&frame[1..6]).to_le_bytes();
    frame[6] = crc[0];
    frame[7] = crc[1];
    frame[8] = PKT_END_1;
    frame[9] = PKT_END_2;
    frame
}

pub fn build_status_request_frame() -> [u8; 10] {
    build_command_frame(COMMAND_STATUS, 0x0000, 0xBE)
}

pub fn build_device_info_request_frame() -> [u8; 10] {
    build_command_frame(COMMAND_DEVICE_INFO, 0x026C, 0x20)
}

pub fn validate_frame(data: &[u8]) -> Result<ValidatedFrame, FrameError> {
    if data.is_empty() {
        return Err(FrameError::Empty);
    }
    if data.len() < MIN_FRAME_LEN {
        return Err(FrameError::TooShort);
    }
    if data.len() > MAX_FRAME_LEN {
        return Err(FrameError::TooLong);
    }
    if data[0] != PKT_START_1 || data[1] != PKT_START_2 {
        return Err(FrameError::BadStart);
    }
    if data[data.len() - 2] != PKT_END_1 || data[data.len() - 1] != PKT_END_2 {
        return Err(FrameError::BadEnd);
    }

    let function = data[2];
    let protocol_len = 6 + data[5] as usize + 4;
    if protocol_len > data.len() || protocol_len < MIN_FRAME_LEN {
        return Err(FrameError::BadLength);
    }
    if function != FRAME_TYPE_DEVICE_INFO && protocol_len != data.len() {
        return Err(FrameError::BadLength);
    }

    let crc_offset = protocol_len - 4;
    let expected_crc = crc16_modbus(&data[1..crc_offset]);
    let remote_crc = u16::from_le_bytes([data[crc_offset], data[crc_offset + 1]]);
    if expected_crc != remote_crc {
        return Err(FrameError::BadCrc);
    }

    let mut bytes = [0_u8; MAX_FRAME_LEN];
    bytes[..data.len()].copy_from_slice(data);
    Ok(ValidatedFrame {
        bytes,
        len: data.len(),
        protocol_len,
        function,
    })
}

pub fn parse_status_frame(data: &[u8]) -> Result<BmsTelemetry, FrameError> {
    let frame = validate_frame(data)?;
    if frame.function != FRAME_TYPE_STATUS {
        return Err(FrameError::UnexpectedFunction);
    }

    let raw = frame.as_slice();
    let temperature_sensor_count = raw[8];
    let cell_count = raw[9];
    if cell_count as usize > MAX_CELLS || temperature_sensor_count > 4 {
        return Err(FrameError::UnsupportedCount);
    }

    let mut cell_voltage_mv = [0_u16; MAX_CELLS];
    let mut index = 0;
    while index < cell_count as usize {
        cell_voltage_mv[index] = read_u16_le(raw, 34 + index * 2)?;
        index += 1;
    }

    let mut offset = cell_count as usize * 2;
    let mut temperature_celsius = [0_i16; MAX_TEMPERATURES];
    let mut temp_index = 0;
    while temp_index < temperature_sensor_count as usize {
        temperature_celsius[temp_index] = read_i16_le(raw, 34 + offset + temp_index * 2)?;
        temp_index += 1;
    }

    offset += temperature_sensor_count as usize * 2;
    temperature_celsius[temp_index] = read_i16_le(raw, 34 + offset)?;
    temp_index += 1;
    temperature_celsius[temp_index] = read_i16_le(raw, 36 + offset)?;
    temp_index += 1;

    Ok(BmsTelemetry {
        permissions: raw[6],
        battery_status: BatteryStatus::from_code(raw[7]),
        temperature_sensor_count,
        cell_count,
        cell_voltage_mv,
        temperature_celsius,
        temperature_count: temp_index as u8,
        pack_voltage_mv: read_u16_le(raw, 38 + offset)? as u32 * 10,
        current_deci_amps: read_i16_le(raw, 40 + offset)?,
        soc_percent: read_u16_le(raw, 42 + offset)?,
        state_of_health_percent: read_u16_le(raw, 44 + offset)?,
        charge_mosfet: MosfetStatus::from_code(raw[46 + offset]),
        discharge_mosfet: MosfetStatus::from_code(raw[47 + offset]),
        balancer: BalancerStatus::from_code(raw[48 + offset]),
        total_capacity_mah: read_u32_le(raw, 50 + offset)? / 1000,
        capacity_remaining_mah: read_u32_le(raw, 54 + offset)? / 1000,
        cycle_capacity_mah: read_u32_le(raw, 58 + offset)?,
        power_watts: read_u32_le(raw, 62 + offset)? as i32,
        total_runtime_seconds: read_u32_le(raw, 66 + offset)?,
        balanced_cell_bitmask: read_u32_le(raw, 70 + offset)?,
        max_cell_voltage_mv: read_u16_le(raw, 74 + offset)?,
        max_voltage_cell: read_u16_le(raw, 76 + offset)?,
        min_cell_voltage_mv: read_u16_le(raw, 78 + offset)?,
        min_voltage_cell: read_u16_le(raw, 80 + offset)?,
        delta_cell_voltage_mv: read_u16_le(raw, 82 + offset)?,
        average_cell_voltage_mv: read_u16_le(raw, 84 + offset)?,
    })
}

pub fn parse_device_info_frame(data: &[u8]) -> Result<DeviceInfo, FrameError> {
    let frame = validate_frame(data)?;
    if frame.function != FRAME_TYPE_DEVICE_INFO {
        return Err(FrameError::UnexpectedFunction);
    }
    let raw = frame.as_slice();
    if raw.len() < 38 {
        return Err(FrameError::BadLength);
    }

    Ok(DeviceInfo {
        device_model: AsciiText::from_zero_padded(&raw[6..22]),
        software_version: AsciiText::from_zero_padded(&raw[22..38]),
    })
}

pub fn crc16_modbus(bytes: &[u8]) -> u16 {
    let mut crc = 0xFFFF_u16;
    for &byte in bytes {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

fn read_u16_le(data: &[u8], index: usize) -> Result<u16, FrameError> {
    if index + 1 >= data.len() {
        return Err(FrameError::BadLength);
    }
    Ok(u16::from_le_bytes([data[index], data[index + 1]]))
}

fn read_i16_le(data: &[u8], index: usize) -> Result<i16, FrameError> {
    Ok(read_u16_le(data, index)? as i16)
}

fn read_u32_le(data: &[u8], index: usize) -> Result<u32, FrameError> {
    if index + 3 >= data.len() {
        return Err(FrameError::BadLength);
    }
    Ok(u32::from_le_bytes([
        data[index],
        data[index + 1],
        data[index + 2],
        data[index + 3],
    ]))
}

#[cfg(test)]
mod tests {
    use super::*;

    const STATUS_FRAME_16S: &[u8] = &[
        0x7E, 0xA1, 0x11, 0x00, 0x00, 0x8E, 0x05, 0x01, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xE4, 0x0C, 0xE4, 0x0C, 0xE5, 0x0C, 0xE5, 0x0C, 0xE8, 0x0C, 0xE7,
        0x0C, 0xE7, 0x0C, 0xE6, 0x0C, 0xE8, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C,
        0xE7, 0x0C, 0xE6, 0x0C, 0xE9, 0x0C, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x07, 0x00, 0xA4,
        0x14, 0x03, 0x00, 0x5B, 0x00, 0x64, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x76, 0xB0, 0x10,
        0xD5, 0x67, 0x0E, 0x0F, 0xBA, 0x32, 0x4A, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x10, 0x58, 0x2E,
        0x02, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x0C, 0x10, 0x00, 0xE4, 0x0C, 0x01, 0x00, 0x05, 0x00,
        0xE6, 0x0C, 0x00, 0x00, 0x80, 0x00, 0x7A, 0x00, 0x0F, 0x02, 0xF2, 0xFA, 0xB9, 0x8C, 0x3B,
        0x00, 0xBB, 0xD8, 0x58, 0x00, 0xDA, 0x2D, 0x43, 0x00, 0xE8, 0xB6, 0x49, 0x00, 0x05, 0x43,
        0xAA, 0x55,
    ];

    const DEVICE_INFO_FRAME_ISSUE_172: &[u8] = &[
        0x7E, 0xA1, 0x12, 0x6C, 0x02, 0x20, 0x32, 0x32, 0x50, 0x48, 0x42, 0x38, 0x54, 0x42, 0x31,
        0x33, 0x30, 0x41, 0x00, 0x00, 0x00, 0x00, 0x32, 0x32, 0x41, 0x41, 0x55, 0x42, 0x30, 0x30,
        0x2D, 0x32, 0x34, 0x31, 0x30, 0x30, 0x38, 0x41, 0xEF, 0x2F, 0xFF, 0x0B, 0x00, 0x00, 0x41,
        0xF2, 0xAA, 0x55,
    ];

    #[test]
    fn builds_known_command_frames() {
        assert_eq!(
            build_status_request_frame(),
            [0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55]
        );
        assert_eq!(
            build_device_info_request_frame(),
            [0x7E, 0xA1, 0x02, 0x6C, 0x02, 0x20, 0x58, 0xC4, 0xAA, 0x55]
        );
    }

    #[test]
    fn validates_crc_from_reference_status_frame() {
        let frame = validate_frame(STATUS_FRAME_16S).unwrap();

        assert_eq!(frame.function(), FRAME_TYPE_STATUS);
        assert_eq!(crc16_modbus(&STATUS_FRAME_16S[1..148]), 0x4305);
        assert_eq!(frame.payload().len(), 0x8E);
    }

    #[test]
    fn decodes_reference_status_frame() {
        let telemetry = parse_status_frame(STATUS_FRAME_16S).unwrap();

        assert_eq!(telemetry.cell_count, 16);
        assert_eq!(telemetry.cell_voltages()[0], 3300);
        assert_eq!(telemetry.cell_voltages()[15], 3305);
        assert_eq!(telemetry.temperatures(), &[1, 2, 2, 7]);
        assert_eq!(telemetry.pack_voltage_mv, 52_840);
        assert_eq!(telemetry.current_deci_amps, 3);
        assert_eq!(telemetry.soc_percent, 91);
        assert_eq!(telemetry.total_capacity_mah, 280_000);
        assert_eq!(telemetry.capacity_remaining_mah, 252_602);
        assert_eq!(telemetry.power_watts, 15);
        assert_eq!(telemetry.max_cell_voltage_mv, 3305);
        assert_eq!(telemetry.max_voltage_cell, 16);
        assert_eq!(telemetry.min_cell_voltage_mv, 3300);
        assert_eq!(telemetry.min_voltage_cell, 1);
        assert_eq!(telemetry.delta_cell_voltage_mv, 5);
        assert_eq!(telemetry.charge_mosfet, MosfetStatus::On);
        assert_eq!(telemetry.discharge_mosfet, MosfetStatus::On);
        assert_eq!(telemetry.balancer, BalancerStatus::Off);
    }

    #[test]
    fn rejects_bad_tail_and_bad_crc() {
        let mut bad_tail = [0_u8; 152];
        bad_tail.copy_from_slice(STATUS_FRAME_16S);
        bad_tail[151] = 0;
        assert_eq!(validate_frame(&bad_tail), Err(FrameError::BadEnd));

        let mut bad_crc = [0_u8; 152];
        bad_crc.copy_from_slice(STATUS_FRAME_16S);
        bad_crc[20] ^= 0x01;
        assert_eq!(validate_frame(&bad_crc), Err(FrameError::BadCrc));
    }

    #[test]
    fn assembler_waits_for_full_two_byte_tail() {
        let mut assembler = FrameAssembler::new();

        assert_eq!(
            assembler.push(&DEVICE_INFO_FRAME_ISSUE_172[..27]).unwrap(),
            None
        );
        let frame = assembler
            .push(&DEVICE_INFO_FRAME_ISSUE_172[27..])
            .unwrap()
            .unwrap();

        assert_eq!(frame.function(), FRAME_TYPE_DEVICE_INFO);
        let info = parse_device_info_frame(frame.as_slice()).unwrap();
        assert_eq!(info.device_model.as_str(), "22PHB8TB130A");
        assert_eq!(info.software_version.as_str(), "22AAUB00-241008A");
    }
}
