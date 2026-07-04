use crate::{
    ant_bms::{
        CHARACTERISTIC_UUID_16, DeviceInfo, FrameAssembler, FrameError, SERVICE_UUID_16,
        build_device_info_request_frame, build_status_request_frame, parse_device_info_frame,
        parse_status_frame,
    },
    app_state::AppState,
    settings::MacAddress,
};

pub const DEFAULT_STATUS_POLL_INTERVAL_MS: u32 = 5_000;
pub const DEVICE_INFO_RETRY_INTERVAL_MS: u32 = 30_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BmsBlePhase {
    Idle,
    Scanning,
    Connecting,
    Discovering,
    Subscribing,
    Online,
    Backoff,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BmsBleError {
    Frame(FrameError),
    UnexpectedService,
    UnexpectedCharacteristic,
}

impl From<FrameError> for BmsBleError {
    fn from(value: FrameError) -> Self {
        Self::Frame(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BmsAdvertisement<'a> {
    pub mac: MacAddress,
    pub name: Option<&'a str>,
    pub rssi: i8,
}

impl BmsAdvertisement<'_> {
    pub fn looks_like_ant_bms(self) -> bool {
        self.name
            .map(|name| name.as_bytes().starts_with(b"ANT-"))
            .unwrap_or(false)
    }

    pub fn matches_binding(self, binding: Option<MacAddress>) -> bool {
        match binding {
            Some(mac) => self.mac == mac,
            None => false,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BmsBleCommand {
    Scan,
    Connect(MacAddress),
    DiscoverService {
        service_uuid_16: u16,
        characteristic_uuid_16: u16,
    },
    SubscribeNotifications,
    WriteStatusRequest([u8; 10]),
    WriteDeviceInfoRequest([u8; 10]),
    Disconnect,
    Wait(u32),
    None,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BmsBleEvent<'a> {
    Start,
    Advertisement(BmsAdvertisement<'a>),
    Connected,
    ServiceDiscovered {
        service_uuid_16: u16,
        characteristic_uuid_16: u16,
    },
    NotificationsSubscribed,
    Notification(&'a [u8]),
    PollElapsed,
    Timeout,
    Disconnected,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BmsBleRuntime {
    pub phase: BmsBlePhase,
    pub target_mac: Option<MacAddress>,
    pub device_info: Option<DeviceInfo>,
    pub status_poll_interval_ms: u32,
    pub device_info_known: bool,
}

impl BmsBleRuntime {
    pub const fn new() -> Self {
        Self {
            phase: BmsBlePhase::Idle,
            target_mac: None,
            device_info: None,
            status_poll_interval_ms: DEFAULT_STATUS_POLL_INTERVAL_MS,
            device_info_known: false,
        }
    }

    pub fn start_scan(&mut self) -> BmsBleCommand {
        self.phase = BmsBlePhase::Scanning;
        BmsBleCommand::Scan
    }

    pub fn on_event(
        &mut self,
        app_state: &mut AppState,
        assembler: &mut FrameAssembler,
        event: BmsBleEvent<'_>,
    ) -> Result<BmsBleCommand, BmsBleError> {
        match event {
            BmsBleEvent::Start => {
                app_state.clear_bms_scan_candidates();
                Ok(self.start_scan())
            }
            BmsBleEvent::Advertisement(advertisement) => {
                if self.phase != BmsBlePhase::Scanning {
                    return Ok(BmsBleCommand::None);
                }
                let matches_binding =
                    advertisement.matches_binding(app_state.settings.bms.bound_mac);
                if matches_binding || advertisement.looks_like_ant_bms() {
                    app_state.bms_scan_candidates.upsert(
                        advertisement.mac,
                        advertisement.name,
                        advertisement.rssi,
                    );
                }
                if !matches_binding {
                    return Ok(BmsBleCommand::None);
                }
                self.target_mac = Some(advertisement.mac);
                self.phase = BmsBlePhase::Connecting;
                Ok(BmsBleCommand::Connect(advertisement.mac))
            }
            BmsBleEvent::Connected => {
                self.phase = BmsBlePhase::Discovering;
                Ok(BmsBleCommand::DiscoverService {
                    service_uuid_16: SERVICE_UUID_16,
                    characteristic_uuid_16: CHARACTERISTIC_UUID_16,
                })
            }
            BmsBleEvent::ServiceDiscovered {
                service_uuid_16,
                characteristic_uuid_16,
            } => {
                if service_uuid_16 != SERVICE_UUID_16 {
                    self.phase = BmsBlePhase::Backoff;
                    return Err(BmsBleError::UnexpectedService);
                }
                if characteristic_uuid_16 != CHARACTERISTIC_UUID_16 {
                    self.phase = BmsBlePhase::Backoff;
                    return Err(BmsBleError::UnexpectedCharacteristic);
                }
                self.phase = BmsBlePhase::Subscribing;
                Ok(BmsBleCommand::SubscribeNotifications)
            }
            BmsBleEvent::NotificationsSubscribed => {
                self.phase = BmsBlePhase::Online;
                Ok(BmsBleCommand::WriteStatusRequest(
                    build_status_request_frame(),
                ))
            }
            BmsBleEvent::Notification(chunk) => {
                if let Some(frame) = assembler.push(chunk)? {
                    match frame.function() {
                        crate::ant_bms::FRAME_TYPE_STATUS => {
                            let telemetry = parse_status_frame(frame.as_slice())?;
                            app_state.bms.update_telemetry(telemetry);
                        }
                        crate::ant_bms::FRAME_TYPE_DEVICE_INFO => {
                            let info = parse_device_info_frame(frame.as_slice())?;
                            self.device_info = Some(info);
                            self.device_info_known = true;
                        }
                        _ => {}
                    }
                }
                Ok(BmsBleCommand::None)
            }
            BmsBleEvent::PollElapsed => {
                if self.phase != BmsBlePhase::Online {
                    return Ok(BmsBleCommand::None);
                }
                if !self.device_info_known {
                    return Ok(BmsBleCommand::WriteDeviceInfoRequest(
                        build_device_info_request_frame(),
                    ));
                }
                Ok(BmsBleCommand::WriteStatusRequest(
                    build_status_request_frame(),
                ))
            }
            BmsBleEvent::Timeout | BmsBleEvent::Disconnected => {
                assembler.reset();
                app_state.bms.mark_offline();
                self.phase = BmsBlePhase::Backoff;
                Ok(BmsBleCommand::Wait(self.status_poll_interval_ms))
            }
        }
    }
}

impl Default for BmsBleRuntime {
    fn default() -> Self {
        Self::new()
    }
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

    fn advertisement(name: Option<&str>) -> BmsAdvertisement<'_> {
        BmsAdvertisement {
            mac: MacAddress::new([1, 2, 3, 4, 5, 6]),
            name,
            rssi: -50,
        }
    }

    #[test]
    fn filters_ant_advertisements_and_binding() {
        assert!(advertisement(Some("ANT-BMS")).looks_like_ant_bms());
        assert!(!advertisement(Some("OTHER")).looks_like_ant_bms());
        assert!(!advertisement(Some("ANT-BMS")).matches_binding(None));
        assert!(
            advertisement(Some("OTHER")).matches_binding(Some(MacAddress::new([1, 2, 3, 4, 5, 6])))
        );
    }

    #[test]
    fn scanning_records_ant_candidates_without_binding() {
        let mut runtime = BmsBleRuntime::new();
        let mut app_state = AppState::default();
        let mut assembler = FrameAssembler::new();

        app_state
            .bms_scan_candidates
            .upsert(MacAddress::new([9, 9, 9, 9, 9, 9]), Some("OLD"), -90);
        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::Start),
            Ok(BmsBleCommand::Scan)
        );
        assert!(app_state.bms_scan_candidates.is_empty());
        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::Advertisement(advertisement(Some("ANT-24S")))
            ),
            Ok(BmsBleCommand::None)
        );

        assert_eq!(runtime.phase, BmsBlePhase::Scanning);
        assert_eq!(app_state.bms_scan_candidates.len(), 1);
        assert_eq!(
            app_state.bms_scan_candidates.as_slice()[0].mac,
            MacAddress::new([1, 2, 3, 4, 5, 6])
        );
    }

    #[test]
    fn walks_scan_connect_discover_subscribe_flow() {
        let mut runtime = BmsBleRuntime::new();
        let mut app_state = AppState::default();
        let mut assembler = FrameAssembler::new();
        app_state.settings.bms.bound_mac = Some(MacAddress::new([1, 2, 3, 4, 5, 6]));

        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::Start),
            Ok(BmsBleCommand::Scan)
        );
        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::Advertisement(advertisement(Some("ANT-24S")))
            ),
            Ok(BmsBleCommand::Connect(MacAddress::new([1, 2, 3, 4, 5, 6])))
        );
        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::Connected),
            Ok(BmsBleCommand::DiscoverService {
                service_uuid_16: SERVICE_UUID_16,
                characteristic_uuid_16: CHARACTERISTIC_UUID_16,
            })
        );
        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::ServiceDiscovered {
                    service_uuid_16: SERVICE_UUID_16,
                    characteristic_uuid_16: CHARACTERISTIC_UUID_16,
                }
            ),
            Ok(BmsBleCommand::SubscribeNotifications)
        );
        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::NotificationsSubscribed
            ),
            Ok(BmsBleCommand::WriteStatusRequest(
                build_status_request_frame()
            ))
        );
        assert_eq!(runtime.phase, BmsBlePhase::Online);
    }

    #[test]
    fn notification_updates_app_state_telemetry() {
        let mut runtime = BmsBleRuntime {
            phase: BmsBlePhase::Online,
            ..BmsBleRuntime::new()
        };
        let mut app_state = AppState::default();
        let mut assembler = FrameAssembler::new();

        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::Notification(&STATUS_FRAME_16S[..40])
            ),
            Ok(BmsBleCommand::None)
        );
        assert_eq!(
            runtime.on_event(
                &mut app_state,
                &mut assembler,
                BmsBleEvent::Notification(&STATUS_FRAME_16S[40..])
            ),
            Ok(BmsBleCommand::None)
        );

        assert!(app_state.bms.online);
        assert_eq!(app_state.bms.telemetry.unwrap().pack_voltage_mv, 52_840);
    }

    #[test]
    fn poll_requests_device_info_until_known_then_status() {
        let mut runtime = BmsBleRuntime {
            phase: BmsBlePhase::Online,
            ..BmsBleRuntime::new()
        };
        let mut app_state = AppState::default();
        let mut assembler = FrameAssembler::new();

        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::PollElapsed),
            Ok(BmsBleCommand::WriteDeviceInfoRequest(
                build_device_info_request_frame()
            ))
        );
        runtime.device_info_known = true;
        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::PollElapsed),
            Ok(BmsBleCommand::WriteStatusRequest(
                build_status_request_frame()
            ))
        );
    }

    #[test]
    fn disconnect_marks_bms_offline_and_backoff() {
        let mut runtime = BmsBleRuntime {
            phase: BmsBlePhase::Online,
            ..BmsBleRuntime::new()
        };
        let mut app_state = AppState::default();
        let mut assembler = FrameAssembler::new();
        app_state.bms.online = true;

        assert_eq!(
            runtime.on_event(&mut app_state, &mut assembler, BmsBleEvent::Disconnected),
            Ok(BmsBleCommand::Wait(DEFAULT_STATUS_POLL_INTERVAL_MS))
        );

        assert_eq!(runtime.phase, BmsBlePhase::Backoff);
        assert!(!app_state.bms.online);
    }
}
