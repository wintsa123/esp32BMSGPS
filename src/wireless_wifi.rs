#![cfg(all(target_arch = "xtensa", feature = "wireless"))]

#[cfg(feature = "net")]
use alloc::boxed::Box;
#[cfg(feature = "net")]
use core::{
    future::Future,
    pin::pin,
    task::{Context, Poll, RawWaker, RawWakerVTable, Waker},
};
#[cfg(feature = "net")]
use embassy_net::{
    Config as NetConfig, IpAddress, IpEndpoint, Ipv4Address, Ipv4Cidr, Runner, Stack,
    StackResources, StaticConfigV4,
    tcp::{State as TcpState, TcpSocket},
    udp::{PacketMetadata, UdpSocket},
};
use esp_hal::peripherals::WIFI;
#[cfg(feature = "net")]
use esp_radio::wifi::Interface;
#[cfg(not(feature = "net"))]
use esp_radio::wifi::Interfaces;
use esp_radio::wifi::{
    self, AuthenticationMethod, Config as WifiConfig, ControllerConfig, WifiController, WifiError,
    ap::AccessPointConfig, sta::StationConfig,
};
#[cfg(feature = "net")]
use heapless::Vec;

use esp32_bms_gps::{
    app_state::AppState,
    dhcp_server::{
        DHCP_CLIENT_PORT, DHCP_SERVER_PORT, SETUP_AP_CLIENT_IP, SETUP_AP_SERVER_IP, parse_request,
        write_reply,
    },
    http_api::{EmbeddedAssets, HttpParseError, ResponseBody, body_len, parse_http_request},
    http_server::handle_connection_with_effects,
    runtime_effects::RuntimeActions,
    wifi_control::{DesiredWifiMode, WifiRuntimeConfig},
};

use crate::{assets, build_config};

#[cfg(feature = "net")]
const DHCP_PACKET_BUFFER_SIZE: usize = 576;
#[cfg(feature = "net")]
const DHCP_REPLY_BUFFER_SIZE: usize = 320;
#[cfg(feature = "net")]
const HTTP_PORT: u16 = 80;
#[cfg(feature = "net")]
const HTTP_TCP_RX_BUFFER_SIZE: usize = 2048;
#[cfg(feature = "net")]
const HTTP_TCP_TX_BUFFER_SIZE: usize = 16 * 1024;
#[cfg(feature = "net")]
const HTTP_REQUEST_BUFFER_SIZE: usize = 2048;
#[cfg(feature = "net")]
const HTTP_HEADER_BUFFER_SIZE: usize = 768;
#[cfg(feature = "net")]
const HTTP_JSON_BUFFER_SIZE: usize = 1024;

pub struct WifiRuntime<'d> {
    controller: WifiController<'d>,
    #[cfg(not(feature = "net"))]
    _interfaces: Interfaces<'d>,
    #[cfg(feature = "net")]
    _station_interface: Interface<'d>,
    #[cfg(feature = "net")]
    ap_network: Option<SetupApNetwork<'d>>,
}

#[cfg(feature = "net")]
struct SetupApNetwork<'d> {
    stack: Stack<'d>,
    runner: Runner<'d, Interface<'d>>,
    dhcp_socket: UdpSocket<'d>,
    http_socket: TcpSocket<'d>,
    http_request_buffer: &'d mut [u8; HTTP_REQUEST_BUFFER_SIZE],
    http_header_buffer: &'d mut [u8; HTTP_HEADER_BUFFER_SIZE],
    http_json_buffer: &'d mut [u8; HTTP_JSON_BUFFER_SIZE],
    http_request_len: usize,
    ap_link_up: bool,
    associated_stations: ApStationSnapshot,
}

#[cfg(feature = "net")]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct ApStationSnapshot {
    count: u8,
    first_mac: [u8; 6],
    first_rssi: i8,
}

#[cfg(feature = "net")]
#[repr(C, align(4))]
#[derive(Clone, Copy)]
struct RawWifiStaInfo {
    mac: [u8; 6],
    rssi: i8,
    flags: [u8; 4],
}

#[cfg(feature = "net")]
#[repr(C)]
struct RawWifiStaList {
    sta: [RawWifiStaInfo; 15],
    num: i32,
}

#[cfg(feature = "net")]
const _: [(); 12] = [(); core::mem::size_of::<RawWifiStaInfo>()];
#[cfg(feature = "net")]
const _: [(); 184] = [(); core::mem::size_of::<RawWifiStaList>()];

#[cfg(feature = "net")]
unsafe extern "C" {
    fn esp_wifi_ap_get_sta_list(sta: *mut RawWifiStaList) -> i32;
}

impl WifiRuntime<'static> {
    pub fn start(
        wifi: WIFI<'static>,
        config: WifiRuntimeConfig,
    ) -> Result<Option<Self>, WifiError> {
        log_runtime_config("start requested", config);
        let Some(initial_config) = esp_radio_config(config) else {
            esp_println::println!("[wifi] start skipped: desired mode is off");
            return Ok(None);
        };
        let controller_config = ControllerConfig::default().with_initial_config(initial_config);
        let (controller, interfaces) = match wifi::new(wifi, controller_config) {
            Ok(parts) => {
                esp_println::println!("[wifi] controller initialized and initial config accepted");
                parts
            }
            Err(error) => {
                esp_println::println!("[wifi] controller init/config failed: {:?}", error);
                return Err(error);
            }
        };
        log_station_connect_limit(config);

        #[cfg(not(feature = "net"))]
        {
            Ok(Some(Self {
                controller,
                _interfaces: interfaces,
            }))
        }
        #[cfg(feature = "net")]
        {
            Ok(Some(Self {
                controller,
                _station_interface: interfaces.station,
                ap_network: start_setup_ap_network(interfaces.access_point),
            }))
        }
    }

    pub fn apply_config(&mut self, config: WifiRuntimeConfig) -> Result<bool, WifiError> {
        log_runtime_config("apply requested", config);
        let Some(radio_config) = esp_radio_config(config) else {
            esp_println::println!("[wifi] apply skipped: desired mode is off");
            return Ok(false);
        };
        if let Err(error) = self.controller.set_config(&radio_config) {
            esp_println::println!("[wifi] set_config failed: {:?}", error);
            return Err(error);
        }
        esp_println::println!("[wifi] set_config ok");
        log_station_connect_limit(config);
        Ok(true)
    }

    pub fn poll(&mut self, app_state: &mut AppState) -> Option<RuntimeActions> {
        #[cfg(feature = "net")]
        if let Some(ap_network) = self.ap_network.as_mut() {
            return ap_network.poll(app_state);
        }
        #[allow(unreachable_code)]
        None
    }
}

pub fn esp_radio_config(config: WifiRuntimeConfig) -> Option<WifiConfig> {
    match config.mode {
        DesiredWifiMode::Off => None,
        DesiredWifiMode::SetupAp => Some(WifiConfig::AccessPoint(setup_ap_config(config))),
        DesiredWifiMode::Station => Some(WifiConfig::Station(station_config(config))),
        DesiredWifiMode::SetupApAndStation => Some(WifiConfig::AccessPointStation(
            station_config(config),
            setup_ap_config(config),
        )),
    }
}

fn setup_ap_config(config: WifiRuntimeConfig) -> AccessPointConfig {
    AccessPointConfig::default()
        .with_ssid(config.setup_ap_ssid.as_str())
        .with_auth_method(AuthenticationMethod::Wpa2Personal)
        .with_password(config.setup_ap_password.as_str().into())
        .with_max_connections(2)
}

fn station_config(config: WifiRuntimeConfig) -> StationConfig {
    StationConfig::default()
        .with_ssid(config.external_ssid.as_str())
        .with_password(config.external_password.as_str().into())
}

fn log_runtime_config(action: &str, config: WifiRuntimeConfig) {
    esp_println::println!(
        "[wifi] {}: mode={} ap_ssid='{}' ap_pw_len={} sta_ssid='{}' sta_pw_len={}",
        action,
        mode_label(config.mode),
        config.setup_ap_ssid.as_str(),
        config.setup_ap_password.len(),
        config.external_ssid.as_str(),
        config.external_password.len()
    );
}

fn log_station_connect_limit(config: WifiRuntimeConfig) {
    if matches!(
        config.mode,
        DesiredWifiMode::Station | DesiredWifiMode::SetupApAndStation
    ) {
        esp_println::println!(
            "[wifi] station credentials configured; async connect task is not running yet"
        );
    }
}

fn mode_label(mode: DesiredWifiMode) -> &'static str {
    match mode {
        DesiredWifiMode::Off => "off",
        DesiredWifiMode::SetupAp => "setup-ap",
        DesiredWifiMode::Station => "station",
        DesiredWifiMode::SetupApAndStation => "setup-ap+station",
    }
}

#[cfg(feature = "net")]
fn start_setup_ap_network(interface: Interface<'static>) -> Option<SetupApNetwork<'static>> {
    let server_ip = Ipv4Address::new(
        SETUP_AP_SERVER_IP[0],
        SETUP_AP_SERVER_IP[1],
        SETUP_AP_SERVER_IP[2],
        SETUP_AP_SERVER_IP[3],
    );
    let mut dns_servers = Vec::new();
    let _ = dns_servers.push(server_ip);
    let net_config = NetConfig::ipv4_static(StaticConfigV4 {
        address: Ipv4Cidr::new(server_ip, 24),
        gateway: Some(server_ip),
        dns_servers,
    });
    let seed = seed_from_mac(interface.mac_address());
    let resources = Box::leak(Box::new(StackResources::<2>::new()));
    let (stack, runner) = embassy_net::new(interface, net_config, resources, seed);
    let rx_meta = Box::leak(Box::new([PacketMetadata::EMPTY; 1]));
    let tx_meta = Box::leak(Box::new([PacketMetadata::EMPTY; 1]));
    let rx_buffer = Box::leak(Box::new([0_u8; DHCP_PACKET_BUFFER_SIZE]));
    let tx_buffer = Box::leak(Box::new([0_u8; DHCP_PACKET_BUFFER_SIZE]));
    let mut dhcp_socket = UdpSocket::new(stack, rx_meta, rx_buffer, tx_meta, tx_buffer);

    if let Err(error) = dhcp_socket.bind(DHCP_SERVER_PORT) {
        esp_println::println!("[wifi] setup AP DHCP bind failed: {:?}", error);
        return None;
    }
    let tcp_rx_buffer = Box::leak(Box::new([0_u8; HTTP_TCP_RX_BUFFER_SIZE]));
    let tcp_tx_buffer = Box::leak(Box::new([0_u8; HTTP_TCP_TX_BUFFER_SIZE]));
    let http_socket = TcpSocket::new(stack, tcp_rx_buffer, tcp_tx_buffer);
    let http_request_buffer = Box::leak(Box::new([0_u8; HTTP_REQUEST_BUFFER_SIZE]));
    let http_header_buffer = Box::leak(Box::new([0_u8; HTTP_HEADER_BUFFER_SIZE]));
    let http_json_buffer = Box::leak(Box::new([0_u8; HTTP_JSON_BUFFER_SIZE]));

    esp_println::println!(
        "[wifi] setup AP IPv4 network ready: 192.168.4.1 lease=192.168.4.2 http=on"
    );
    Some(SetupApNetwork {
        stack,
        runner,
        dhcp_socket,
        http_socket,
        http_request_buffer,
        http_header_buffer,
        http_json_buffer,
        http_request_len: 0,
        ap_link_up: false,
        associated_stations: ApStationSnapshot::empty(),
    })
}

#[cfg(feature = "net")]
impl SetupApNetwork<'_> {
    fn poll(&mut self, app_state: &mut AppState) -> Option<RuntimeActions> {
        self.poll_runner();
        self.log_ap_link_state();
        self.log_station_list_changes();
        if self.poll_dhcp_once() {
            self.poll_runner();
        }
        let actions = self.poll_http_once(app_state);
        if actions.is_some() {
            self.poll_runner();
        }
        actions
    }

    fn poll_runner(&mut self) {
        with_noop_context(|cx| {
            let future = self.runner.run();
            let mut future = pin!(future);
            let _ = Future::poll(future.as_mut(), cx);
        });
    }

    fn poll_dhcp_once(&mut self) -> bool {
        let mut request_packet = [0_u8; DHCP_PACKET_BUFFER_SIZE];
        let recv_result = poll_future_once(self.dhcp_socket.recv_from(&mut request_packet));
        let Some(Ok((request_len, _metadata))) = recv_result else {
            return false;
        };
        let Some(request) = parse_request(&request_packet[..request_len]) else {
            return false;
        };

        let mut reply = [0_u8; DHCP_REPLY_BUFFER_SIZE];
        let Some(reply_len) = write_reply(&request, &mut reply) else {
            return false;
        };
        let destination = IpEndpoint::new(
            IpAddress::Ipv4(Ipv4Address::new(255, 255, 255, 255)),
            DHCP_CLIENT_PORT,
        );
        match poll_future_once(self.dhcp_socket.send_to(&reply[..reply_len], destination)) {
            Some(Ok(())) => {
                esp_println::println!(
                    "[wifi] DHCP {:?} -> {:?} for {:02x?}",
                    request.message_type(),
                    SETUP_AP_CLIENT_IP,
                    request.client_mac()
                );
            }
            Some(Err(error)) => {
                esp_println::println!("[wifi] DHCP reply send failed: {:?}", error);
            }
            None => {
                esp_println::println!("[wifi] DHCP reply send pending");
            }
        }

        true
    }

    fn poll_http_once(&mut self, app_state: &mut AppState) -> Option<RuntimeActions> {
        match self.http_socket.state() {
            TcpState::Closed => {
                self.http_request_len = 0;
                let _ = poll_future_once(self.http_socket.accept(HTTP_PORT));
                None
            }
            TcpState::Listen | TcpState::SynReceived => None,
            TcpState::Established | TcpState::CloseWait => {
                while self.http_socket.can_recv()
                    && self.http_request_len < self.http_request_buffer.len()
                {
                    let target = &mut self.http_request_buffer[self.http_request_len..];
                    match poll_future_once(self.http_socket.read(target)) {
                        Some(Ok(0)) | Some(Err(_)) => break,
                        Some(Ok(len)) => self.http_request_len += len,
                        None => break,
                    }
                }

                if self.http_request_len == 0 {
                    return None;
                }
                if !http_request_ready(&self.http_request_buffer[..self.http_request_len])
                    && self.http_request_len < self.http_request_buffer.len()
                {
                    return None;
                }

                let result = handle_connection_with_effects(
                    &self.http_request_buffer[..self.http_request_len],
                    app_state,
                    build_config::FIRMWARE_VERSION,
                    EmbeddedAssets {
                        index_html: assets::INDEX_HTML,
                        index_content_type: assets::INDEX_HTML_CONTENT_TYPE,
                    },
                    self.http_json_buffer,
                    self.http_header_buffer,
                );
                self.http_request_len = 0;

                let actions = match result {
                    Ok((response, actions)) => {
                        let header = &self.http_header_buffer[..response.header_len];
                        let body = match response.body {
                            ResponseBody::Static(bytes) | ResponseBody::Buffer(bytes) => bytes,
                            ResponseBody::Empty => &[],
                        };
                        let socket = &mut self.http_socket;
                        let ok = write_http_bytes(socket, header) && write_http_bytes(socket, body);
                        esp_println::println!(
                            "[wifi] HTTP {} len={}{}",
                            response.status,
                            body_len(response.body),
                            if ok { "" } else { " truncated" }
                        );
                        Some(actions)
                    }
                    Err(error) => {
                        esp_println::println!("[wifi] HTTP handler failed: {:?}", error);
                        None
                    }
                };
                self.http_socket.close();
                actions
            }
            _ => None,
        }
    }

    fn log_ap_link_state(&mut self) {
        let link_up = self.stack.is_link_up();
        if link_up == self.ap_link_up {
            return;
        }

        self.ap_link_up = link_up;
        if link_up {
            esp_println::println!("[wifi] AP started: ip=192.168.4.1 dhcp=on");
        } else {
            esp_println::println!("[wifi] AP stopped");
        }
    }

    fn log_station_list_changes(&mut self) {
        let Some(snapshot) = read_ap_station_snapshot() else {
            return;
        };
        if snapshot == self.associated_stations {
            return;
        }

        self.associated_stations = snapshot;
        if snapshot.count == 0 {
            esp_println::println!("[wifi] AP client disconnected: clients=0");
        } else {
            esp_println::println!(
                "[wifi] AP client connected: clients={} first_mac={:02x?} rssi={}",
                snapshot.count,
                snapshot.first_mac,
                snapshot.first_rssi
            );
        }
    }
}

#[cfg(feature = "net")]
impl ApStationSnapshot {
    const fn empty() -> Self {
        Self {
            count: 0,
            first_mac: [0; 6],
            first_rssi: 0,
        }
    }
}

#[cfg(feature = "net")]
fn read_ap_station_snapshot() -> Option<ApStationSnapshot> {
    let mut sta_list = RawWifiStaList {
        sta: [RawWifiStaInfo {
            mac: [0; 6],
            rssi: 0,
            flags: [0; 4],
        }; 15],
        num: 0,
    };

    let result = unsafe { esp_wifi_ap_get_sta_list(&mut sta_list) };
    if result != 0 {
        return None;
    }

    let count = sta_list.num.clamp(0, 15) as u8;
    let first = sta_list.sta[0];
    Some(ApStationSnapshot {
        count,
        first_mac: if count == 0 { [0; 6] } else { first.mac },
        first_rssi: if count == 0 { 0 } else { first.rssi },
    })
}

#[cfg(feature = "net")]
fn seed_from_mac(mac: [u8; 6]) -> u64 {
    u64::from_be_bytes([0, 0, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]])
}

#[cfg(feature = "net")]
fn http_request_ready(bytes: &[u8]) -> bool {
    !matches!(
        parse_http_request(bytes),
        Err(HttpParseError::MissingHeadersEnd | HttpParseError::BodyIncomplete)
    )
}

#[cfg(feature = "net")]
fn write_http_bytes(socket: &mut TcpSocket<'_>, mut bytes: &[u8]) -> bool {
    while !bytes.is_empty() {
        match poll_future_once(socket.write(bytes)) {
            Some(Ok(0)) | Some(Err(_)) | None => return false,
            Some(Ok(len)) => bytes = &bytes[len..],
        }
    }
    true
}

#[cfg(feature = "net")]
fn poll_future_once<F: Future>(future: F) -> Option<F::Output> {
    with_noop_context(|cx| {
        let mut future = pin!(future);
        match Future::poll(future.as_mut(), cx) {
            Poll::Ready(output) => Some(output),
            Poll::Pending => None,
        }
    })
}

#[cfg(feature = "net")]
fn with_noop_context<R>(f: impl FnOnce(&mut Context<'_>) -> R) -> R {
    let waker = unsafe { Waker::from_raw(noop_raw_waker()) };
    let mut context = Context::from_waker(&waker);
    f(&mut context)
}

#[cfg(feature = "net")]
fn noop_raw_waker() -> RawWaker {
    RawWaker::new(core::ptr::null(), &NOOP_WAKER_VTABLE)
}

#[cfg(feature = "net")]
static NOOP_WAKER_VTABLE: RawWakerVTable =
    RawWakerVTable::new(noop_clone, noop_wake, noop_wake, noop_drop);

#[cfg(feature = "net")]
unsafe fn noop_clone(_: *const ()) -> RawWaker {
    noop_raw_waker()
}

#[cfg(feature = "net")]
unsafe fn noop_wake(_: *const ()) {}

#[cfg(feature = "net")]
unsafe fn noop_drop(_: *const ()) {}
