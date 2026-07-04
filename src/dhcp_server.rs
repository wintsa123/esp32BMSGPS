pub const DHCP_SERVER_PORT: u16 = 67;
pub const DHCP_CLIENT_PORT: u16 = 68;
pub const SETUP_AP_SERVER_IP: [u8; 4] = [192, 168, 4, 1];
pub const SETUP_AP_CLIENT_IP: [u8; 4] = [192, 168, 4, 2];
pub const SETUP_AP_SUBNET_MASK: [u8; 4] = [255, 255, 255, 0];
pub const SETUP_AP_LEASE_SECONDS: u32 = 3600;

const BOOT_REQUEST: u8 = 1;
const BOOT_REPLY: u8 = 2;
const ETHERNET_HTYPE: u8 = 1;
const ETHERNET_HLEN: u8 = 6;
const FIXED_HEADER_LEN: usize = 236;
const MAGIC_COOKIE_OFFSET: usize = FIXED_HEADER_LEN;
const OPTIONS_OFFSET: usize = MAGIC_COOKIE_OFFSET + 4;
const MAGIC_COOKIE: [u8; 4] = [99, 130, 83, 99];
const OPTION_PAD: u8 = 0;
const OPTION_SUBNET_MASK: u8 = 1;
const OPTION_ROUTER: u8 = 3;
const OPTION_DNS: u8 = 6;
const OPTION_REQUESTED_IP: u8 = 50;
const OPTION_LEASE_TIME: u8 = 51;
const OPTION_MESSAGE_TYPE: u8 = 53;
const OPTION_SERVER_ID: u8 = 54;
const OPTION_END: u8 = 255;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DhcpMessageType {
    Discover,
    Offer,
    Request,
    Ack,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DhcpRequest {
    xid: [u8; 4],
    flags: [u8; 2],
    giaddr: [u8; 4],
    client_mac: [u8; 6],
    message_type: DhcpMessageType,
    requested_ip: Option<[u8; 4]>,
}

impl DhcpRequest {
    pub fn message_type(&self) -> DhcpMessageType {
        self.message_type
    }

    pub fn client_mac(&self) -> [u8; 6] {
        self.client_mac
    }

    pub fn requested_ip(&self) -> Option<[u8; 4]> {
        self.requested_ip
    }
}

pub fn parse_request(packet: &[u8]) -> Option<DhcpRequest> {
    if packet.len() < OPTIONS_OFFSET {
        return None;
    }
    if packet[0] != BOOT_REQUEST
        || packet[1] != ETHERNET_HTYPE
        || packet[2] != ETHERNET_HLEN
        || packet[MAGIC_COOKIE_OFFSET..OPTIONS_OFFSET] != MAGIC_COOKIE
    {
        return None;
    }

    let mut message_type = None;
    let mut requested_ip = None;
    let mut cursor = OPTIONS_OFFSET;
    while cursor < packet.len() {
        let code = packet[cursor];
        cursor += 1;
        match code {
            OPTION_PAD => {}
            OPTION_END => break,
            _ => {
                if cursor >= packet.len() {
                    return None;
                }
                let len = packet[cursor] as usize;
                cursor += 1;
                if cursor + len > packet.len() {
                    return None;
                }
                let value = &packet[cursor..cursor + len];
                cursor += len;

                match code {
                    OPTION_MESSAGE_TYPE if len == 1 => {
                        message_type = DhcpMessageType::from_option(value[0]);
                    }
                    OPTION_REQUESTED_IP if len == 4 => {
                        requested_ip = Some([value[0], value[1], value[2], value[3]]);
                    }
                    _ => {}
                }
            }
        }
    }

    let message_type = message_type?;
    if !matches!(
        message_type,
        DhcpMessageType::Discover | DhcpMessageType::Request
    ) {
        return None;
    }

    Some(DhcpRequest {
        xid: [packet[4], packet[5], packet[6], packet[7]],
        flags: [packet[10], packet[11]],
        giaddr: [packet[24], packet[25], packet[26], packet[27]],
        client_mac: [
            packet[28], packet[29], packet[30], packet[31], packet[32], packet[33],
        ],
        message_type,
        requested_ip,
    })
}

pub fn write_reply(request: &DhcpRequest, output: &mut [u8]) -> Option<usize> {
    let reply_type = match request.message_type {
        DhcpMessageType::Discover => DhcpMessageType::Offer,
        DhcpMessageType::Request => DhcpMessageType::Ack,
        DhcpMessageType::Offer | DhcpMessageType::Ack => return None,
    };

    write_response(request, reply_type, output)
}

fn write_response(
    request: &DhcpRequest,
    message_type: DhcpMessageType,
    output: &mut [u8],
) -> Option<usize> {
    let min_len = OPTIONS_OFFSET + 3 + 6 + 6 + 6 + 6 + 6 + 1;
    if output.len() < min_len {
        return None;
    }

    output[..OPTIONS_OFFSET].fill(0);
    output[0] = BOOT_REPLY;
    output[1] = ETHERNET_HTYPE;
    output[2] = ETHERNET_HLEN;
    output[4..8].copy_from_slice(&request.xid);
    output[10..12].copy_from_slice(&request.flags);
    output[16..20].copy_from_slice(&SETUP_AP_CLIENT_IP);
    output[20..24].copy_from_slice(&SETUP_AP_SERVER_IP);
    output[24..28].copy_from_slice(&request.giaddr);
    output[28..34].copy_from_slice(&request.client_mac);
    output[MAGIC_COOKIE_OFFSET..OPTIONS_OFFSET].copy_from_slice(&MAGIC_COOKIE);

    let mut cursor = OPTIONS_OFFSET;
    cursor = write_option(
        output,
        cursor,
        OPTION_MESSAGE_TYPE,
        &[message_type.to_option()],
    )?;
    cursor = write_option(output, cursor, OPTION_SERVER_ID, &SETUP_AP_SERVER_IP)?;
    cursor = write_option(
        output,
        cursor,
        OPTION_LEASE_TIME,
        &SETUP_AP_LEASE_SECONDS.to_be_bytes(),
    )?;
    cursor = write_option(output, cursor, OPTION_SUBNET_MASK, &SETUP_AP_SUBNET_MASK)?;
    cursor = write_option(output, cursor, OPTION_ROUTER, &SETUP_AP_SERVER_IP)?;
    cursor = write_option(output, cursor, OPTION_DNS, &SETUP_AP_SERVER_IP)?;
    output[cursor] = OPTION_END;
    Some(cursor + 1)
}

fn write_option(output: &mut [u8], cursor: usize, code: u8, value: &[u8]) -> Option<usize> {
    let end = cursor.checked_add(2)?.checked_add(value.len())?;
    if end > output.len() || value.len() > u8::MAX as usize {
        return None;
    }
    output[cursor] = code;
    output[cursor + 1] = value.len() as u8;
    output[cursor + 2..end].copy_from_slice(value);
    Some(end)
}

impl DhcpMessageType {
    fn from_option(value: u8) -> Option<Self> {
        match value {
            1 => Some(Self::Discover),
            2 => Some(Self::Offer),
            3 => Some(Self::Request),
            5 => Some(Self::Ack),
            _ => None,
        }
    }

    fn to_option(self) -> u8 {
        match self {
            Self::Discover => 1,
            Self::Offer => 2,
            Self::Request => 3,
            Self::Ack => 5,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLIENT_MAC: [u8; 6] = [0x02, 0x12, 0x34, 0x56, 0x78, 0x9a];

    fn write_base_request(output: &mut [u8], message_type: u8) -> usize {
        output[..OPTIONS_OFFSET].fill(0);
        output[0] = BOOT_REQUEST;
        output[1] = ETHERNET_HTYPE;
        output[2] = ETHERNET_HLEN;
        output[4..8].copy_from_slice(&[0x01, 0x02, 0x03, 0x04]);
        output[10..12].copy_from_slice(&[0x80, 0x00]);
        output[28..34].copy_from_slice(&CLIENT_MAC);
        output[MAGIC_COOKIE_OFFSET..OPTIONS_OFFSET].copy_from_slice(&MAGIC_COOKIE);
        let cursor =
            write_option(output, OPTIONS_OFFSET, OPTION_MESSAGE_TYPE, &[message_type]).unwrap();
        output[cursor] = OPTION_END;
        cursor + 1
    }

    fn option_value<'a>(packet: &'a [u8], option: u8) -> Option<&'a [u8]> {
        let mut cursor = OPTIONS_OFFSET;
        while cursor < packet.len() {
            let code = packet[cursor];
            cursor += 1;
            match code {
                OPTION_PAD => {}
                OPTION_END => return None,
                _ => {
                    let len = packet[cursor] as usize;
                    cursor += 1;
                    let value = &packet[cursor..cursor + len];
                    cursor += len;
                    if code == option {
                        return Some(value);
                    }
                }
            }
        }
        None
    }

    #[test]
    fn discover_builds_offer() {
        let mut request_packet = [0_u8; 300];
        let request_len = write_base_request(&mut request_packet, 1);
        let request = parse_request(&request_packet[..request_len]).unwrap();
        let mut reply = [0_u8; 320];

        let len = write_reply(&request, &mut reply).unwrap();

        assert_eq!(request.message_type(), DhcpMessageType::Discover);
        assert_eq!(reply[0], BOOT_REPLY);
        assert_eq!(&reply[4..8], &[0x01, 0x02, 0x03, 0x04]);
        assert_eq!(&reply[16..20], &SETUP_AP_CLIENT_IP);
        assert_eq!(&reply[20..24], &SETUP_AP_SERVER_IP);
        assert_eq!(&reply[28..34], &CLIENT_MAC);
        assert_eq!(
            option_value(&reply[..len], OPTION_MESSAGE_TYPE),
            Some(&[2][..])
        );
        assert_eq!(
            option_value(&reply[..len], OPTION_SERVER_ID),
            Some(&SETUP_AP_SERVER_IP[..])
        );
        assert_eq!(
            option_value(&reply[..len], OPTION_SUBNET_MASK),
            Some(&SETUP_AP_SUBNET_MASK[..])
        );
    }

    #[test]
    fn request_builds_ack() {
        let mut request_packet = [0_u8; 320];
        let mut request_len = write_base_request(&mut request_packet, 3);
        request_len -= 1;
        request_len = write_option(
            &mut request_packet,
            request_len,
            OPTION_REQUESTED_IP,
            &SETUP_AP_CLIENT_IP,
        )
        .unwrap();
        request_packet[request_len] = OPTION_END;
        request_len += 1;

        let request = parse_request(&request_packet[..request_len]).unwrap();
        let mut reply = [0_u8; 320];
        let len = write_reply(&request, &mut reply).unwrap();

        assert_eq!(request.message_type(), DhcpMessageType::Request);
        assert_eq!(request.requested_ip(), Some(SETUP_AP_CLIENT_IP));
        assert_eq!(
            option_value(&reply[..len], OPTION_MESSAGE_TYPE),
            Some(&[5][..])
        );
        assert_eq!(
            option_value(&reply[..len], OPTION_LEASE_TIME),
            Some(&SETUP_AP_LEASE_SECONDS.to_be_bytes()[..])
        );
        assert_eq!(
            option_value(&reply[..len], OPTION_ROUTER),
            Some(&SETUP_AP_SERVER_IP[..])
        );
        assert_eq!(
            option_value(&reply[..len], OPTION_DNS),
            Some(&SETUP_AP_SERVER_IP[..])
        );
    }

    #[test]
    fn invalid_packets_are_ignored() {
        let mut packet = [0_u8; 300];
        let len = write_base_request(&mut packet, 8);
        assert_eq!(parse_request(&packet[..len]), None);

        let len = write_base_request(&mut packet, 1);
        packet[MAGIC_COOKIE_OFFSET] = 0;
        assert_eq!(parse_request(&packet[..len]), None);

        assert_eq!(parse_request(&packet[..OPTIONS_OFFSET - 1]), None);
    }

    #[test]
    fn short_output_buffer_is_rejected() {
        let mut request_packet = [0_u8; 300];
        let request_len = write_base_request(&mut request_packet, 1);
        let request = parse_request(&request_packet[..request_len]).unwrap();
        let mut reply = [0_u8; 16];

        assert_eq!(write_reply(&request, &mut reply), None);
    }
}
