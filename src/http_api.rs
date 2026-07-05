use crate::{
    app_state::{AppState, OtaState},
    local_api::{self, ApiError, ApiRoute, HttpMethod},
};

pub const CONTENT_TYPE_JSON: &str = "application/json";
pub const CONTENT_TYPE_TEXT: &str = "text/plain; charset=utf-8";
pub const HEADER_END: &[u8; 4] = b"\r\n\r\n";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ResponseBody<'a> {
    Static(&'static [u8]),
    Buffer(&'a [u8]),
    Empty,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HttpResponse<'a> {
    pub status: u16,
    pub content_type: &'static str,
    pub body: ResponseBody<'a>,
    pub effects: HttpEffects,
}

impl<'a> HttpResponse<'a> {
    pub const fn no_content() -> Self {
        Self {
            status: 204,
            content_type: CONTENT_TYPE_TEXT,
            body: ResponseBody::Empty,
            effects: HttpEffects::none(),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HttpEffects {
    pub settings_changed: bool,
    pub wifi_reconnect_requested: bool,
    pub bms_scan_requested: bool,
    pub ota_check_requested: bool,
    pub ota_start_requested: bool,
}

impl HttpEffects {
    pub const fn none() -> Self {
        Self {
            settings_changed: false,
            wifi_reconnect_requested: false,
            bms_scan_requested: false,
            ota_check_requested: false,
            ota_start_requested: false,
        }
    }

    pub const fn settings_changed() -> Self {
        Self {
            settings_changed: true,
            ..Self::none()
        }
    }

    pub const fn wifi_reconnect_requested() -> Self {
        Self {
            settings_changed: true,
            wifi_reconnect_requested: true,
            ..Self::none()
        }
    }

    pub const fn ota_check_requested() -> Self {
        Self {
            ota_check_requested: true,
            ..Self::none()
        }
    }

    pub const fn ota_start_requested() -> Self {
        Self {
            ota_start_requested: true,
            ..Self::none()
        }
    }

    pub const fn bms_scan_requested() -> Self {
        Self {
            bms_scan_requested: true,
            ..Self::none()
        }
    }

    pub const fn settings_changed_and_bms_scan_requested() -> Self {
        Self {
            settings_changed: true,
            bms_scan_requested: true,
            ..Self::none()
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HttpRequest<'a> {
    pub method: HttpMethod,
    pub path: &'a str,
    pub body: &'a str,
    pub setup_password: Option<&'a str>,
    pub authorization: Option<&'a str>,
    pub cross_origin: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EmbeddedAssets {
    pub index_html: &'static [u8],
    pub index_content_type: &'static str,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HttpParseError {
    InvalidUtf8,
    InvalidRequestLine,
    UnsupportedMethod,
    MissingHeadersEnd,
    BodyIncomplete,
    InvalidContentLength,
}

pub fn handle_request<'a>(
    request: HttpRequest<'_>,
    app_state: &mut AppState,
    firmware_version: &str,
    assets: EmbeddedAssets,
    response_buffer: &'a mut [u8],
) -> HttpResponse<'a> {
    if request.method == HttpMethod::Options {
        return HttpResponse::no_content();
    }

    match local_api::route(request.method, request.path) {
        Ok(ApiRoute::Index) => HttpResponse {
            status: 200,
            content_type: assets.index_content_type,
            body: ResponseBody::Static(assets.index_html),
            effects: HttpEffects::none(),
        },
        Ok(_) if !request_has_setup_password(&request, app_state) => unauthorized_response(),
        Ok(ApiRoute::Status) => json_result(local_api::write_status_json(
            response_buffer,
            app_state,
            firmware_version,
        )),
        Ok(ApiRoute::ConfigRead) => json_result(local_api::write_config_json(
            response_buffer,
            &app_state.settings,
        )),
        Ok(ApiRoute::BmsCandidates) => json_result(local_api::write_bms_candidates_json(
            response_buffer,
            app_state,
        )),
        Ok(ApiRoute::ConfigWrite) => match local_api::parse_config_update_json(request.body)
            .and_then(|update| local_api::apply_config_update(&mut app_state.settings, update))
        {
            Ok(()) => HttpResponse {
                effects: HttpEffects::settings_changed(),
                ..HttpResponse::no_content()
            },
            Err(error) => error_response(error),
        },
        Ok(ApiRoute::WifiUpdate) => match local_api::parse_wifi_update_json(request.body)
            .and_then(|update| local_api::apply_wifi_update(&mut app_state.settings, update))
        {
            Ok(()) => {
                app_state.mark_external_wifi_connecting();
                HttpResponse {
                    effects: HttpEffects::wifi_reconnect_requested(),
                    ..HttpResponse::no_content()
                }
            }
            Err(error) => error_response(error),
        },
        Ok(ApiRoute::ApPasswordUpdate) => match local_api::parse_ap_password_json(request.body)
            .and_then(|password| {
                local_api::apply_ap_password_update(&mut app_state.settings, password)
            }) {
            Ok(()) => HttpResponse {
                effects: HttpEffects::settings_changed(),
                ..HttpResponse::no_content()
            },
            Err(error) => error_response(error),
        },
        Ok(ApiRoute::BmsBind) => match local_api::parse_bms_bind_json(request.body)
            .and_then(|mac| local_api::apply_bms_bind(&mut app_state.settings, mac))
        {
            Ok(()) => HttpResponse {
                effects: HttpEffects::settings_changed_and_bms_scan_requested(),
                ..HttpResponse::no_content()
            },
            Err(error) => error_response(error),
        },
        Ok(ApiRoute::BmsScan) => HttpResponse {
            effects: HttpEffects::bms_scan_requested(),
            ..HttpResponse::no_content()
        },
        Ok(ApiRoute::OtaCheck) => {
            app_state.ota = OtaState::Checking;
            HttpResponse {
                effects: HttpEffects::ota_check_requested(),
                ..HttpResponse::no_content()
            }
        }
        Ok(ApiRoute::OtaStart) => {
            app_state.ota = OtaState::Downloading;
            HttpResponse {
                effects: HttpEffects::ota_start_requested(),
                ..HttpResponse::no_content()
            }
        }
        Err(error) => error_response(error),
    }
}

pub fn method_from_bytes(bytes: &[u8]) -> Option<HttpMethod> {
    match bytes {
        b"GET" => Some(HttpMethod::Get),
        b"POST" => Some(HttpMethod::Post),
        b"OPTIONS" => Some(HttpMethod::Options),
        _ => None,
    }
}

pub fn parse_http_request(bytes: &[u8]) -> Result<HttpRequest<'_>, HttpParseError> {
    let header_end = find_subslice(bytes, HEADER_END).ok_or(HttpParseError::MissingHeadersEnd)?;
    let headers =
        core::str::from_utf8(&bytes[..header_end]).map_err(|_| HttpParseError::InvalidUtf8)?;
    let mut lines = headers.split("\r\n");
    let request_line = lines.next().ok_or(HttpParseError::InvalidRequestLine)?;
    let mut parts = request_line.split_ascii_whitespace();
    let method = parts
        .next()
        .and_then(|value| method_from_bytes(value.as_bytes()))
        .ok_or(HttpParseError::UnsupportedMethod)?;
    let path = parts.next().ok_or(HttpParseError::InvalidRequestLine)?;
    let version = parts.next().ok_or(HttpParseError::InvalidRequestLine)?;
    if !version.starts_with("HTTP/1.") || parts.next().is_some() {
        return Err(HttpParseError::InvalidRequestLine);
    }

    let mut content_length = 0;
    let mut setup_password = None;
    let mut authorization = None;
    let mut cross_origin = false;
    for line in lines {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        let value = value.trim();
        if name.eq_ignore_ascii_case("content-length") {
            content_length = value
                .parse::<usize>()
                .map_err(|_| HttpParseError::InvalidContentLength)?;
        } else if name.eq_ignore_ascii_case("x-setup-password") {
            setup_password = Some(value);
        } else if name.eq_ignore_ascii_case("authorization") {
            authorization = Some(value);
        } else if name.eq_ignore_ascii_case("origin") {
            cross_origin = true;
        }
    }
    let body_start = header_end + HEADER_END.len();
    let body_end = body_start
        .checked_add(content_length)
        .ok_or(HttpParseError::InvalidContentLength)?;
    if body_end > bytes.len() {
        return Err(HttpParseError::BodyIncomplete);
    }
    let body = core::str::from_utf8(&bytes[body_start..body_end])
        .map_err(|_| HttpParseError::InvalidUtf8)?;

    Ok(HttpRequest {
        method,
        path,
        body,
        setup_password,
        authorization,
        cross_origin,
    })
}

pub fn write_response_headers(
    response: &HttpResponse<'_>,
    content_len: usize,
    output: &mut [u8],
) -> Result<usize, ApiError> {
    let mut writer = HeaderWriter::new(output);
    writer.push("HTTP/1.1 ")?;
    writer.push(status_text(response.status))?;
    writer.push("\r\nContent-Type: ")?;
    writer.push(response.content_type)?;
    writer.push("\r\nContent-Length: ")?;
    writer.push_usize(content_len)?;
    writer.push("\r\nConnection: close")?;
    if response.status == 401 {
        writer.push("\r\nWWW-Authenticate: Basic realm=\"esp32-bms-gps\", charset=\"UTF-8\"")?;
    }
    writer.push("\r\nAccess-Control-Allow-Origin: *")?;
    writer.push("\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS")?;
    writer
        .push("\r\nAccess-Control-Allow-Headers: Content-Type, X-Setup-Password, Authorization")?;
    writer.push("\r\nAccess-Control-Max-Age: 600")?;
    writer.push("\r\nAccess-Control-Allow-Private-Network: true")?;
    writer.push("\r\nPrivate-Network-Access-Name: \"esp32-bms-gps\"")?;
    writer.push("\r\nPrivate-Network-Access-ID: \"02:00:00:00:00:01\"\r\n\r\n")?;
    Ok(writer.len())
}

pub fn body_len(body: ResponseBody<'_>) -> usize {
    match body {
        ResponseBody::Static(bytes) | ResponseBody::Buffer(bytes) => bytes.len(),
        ResponseBody::Empty => 0,
    }
}

fn find_subslice(bytes: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || needle.len() > bytes.len() {
        return None;
    }
    bytes
        .windows(needle.len())
        .position(|window| window == needle)
}

fn status_text(status: u16) -> &'static str {
    match status {
        200 => "200 OK",
        204 => "204 No Content",
        400 => "400 Bad Request",
        401 => "401 Unauthorized",
        404 => "404 Not Found",
        405 => "405 Method Not Allowed",
        500 => "500 Internal Server Error",
        _ => "500 Internal Server Error",
    }
}

fn request_has_setup_password(request: &HttpRequest<'_>, app_state: &AppState) -> bool {
    let expected = app_state.settings.wifi.setup_ap_password.as_bytes();
    if expected.is_empty() {
        return false;
    }
    if request
        .setup_password
        .is_some_and(|password| constant_time_eq(password.as_bytes(), expected))
    {
        return true;
    }
    request
        .authorization
        .is_some_and(|value| basic_auth_password_matches(value, expected))
}

fn basic_auth_password_matches(value: &str, expected_password: &[u8]) -> bool {
    let mut parts = value.split_ascii_whitespace();
    let Some(scheme) = parts.next() else {
        return false;
    };
    let Some(encoded) = parts.next() else {
        return false;
    };
    if parts.next().is_some() || !scheme.eq_ignore_ascii_case("basic") {
        return false;
    }

    let mut decoded = [0_u8; 96];
    let Some(len) = decode_base64(encoded.as_bytes(), &mut decoded) else {
        return false;
    };
    let Some(colon) = decoded[..len].iter().position(|byte| *byte == b':') else {
        return false;
    };
    constant_time_eq(&decoded[colon + 1..len], expected_password)
}

fn decode_base64(input: &[u8], output: &mut [u8]) -> Option<usize> {
    let mut out_len = 0;
    let mut chunk = [0_u8; 4];
    let mut chunk_len = 0;
    let mut saw_padding = false;

    for &byte in input {
        let value = match byte {
            b'A'..=b'Z' => byte - b'A',
            b'a'..=b'z' => byte - b'a' + 26,
            b'0'..=b'9' => byte - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            b'=' => {
                saw_padding = true;
                64
            }
            _ => return None,
        };
        if saw_padding && value != 64 {
            return None;
        }
        chunk[chunk_len] = value;
        chunk_len += 1;
        if chunk_len == 4 {
            let pad = usize::from(chunk[2] == 64) + usize::from(chunk[3] == 64);
            if chunk[0] == 64 || chunk[1] == 64 || (chunk[2] == 64 && chunk[3] != 64) {
                return None;
            }
            if out_len + 3 - pad > output.len() {
                return None;
            }
            output[out_len] = (chunk[0] << 2) | (chunk[1] >> 4);
            out_len += 1;
            if pad < 2 {
                output[out_len] = (chunk[1] << 4) | (chunk[2] >> 2);
                out_len += 1;
            }
            if pad == 0 {
                output[out_len] = (chunk[2] << 6) | chunk[3];
                out_len += 1;
            }
            chunk_len = 0;
        }
    }

    if chunk_len == 0 { Some(out_len) } else { None }
}

fn constant_time_eq(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() {
        return false;
    }
    let mut diff = 0_u8;
    for index in 0..left.len() {
        diff |= left[index] ^ right[index];
    }
    diff == 0
}

fn unauthorized_response() -> HttpResponse<'static> {
    HttpResponse {
        status: 401,
        content_type: CONTENT_TYPE_TEXT,
        body: ResponseBody::Static(b"unauthorized"),
        effects: HttpEffects::none(),
    }
}

fn json_result<'a>(result: Result<&'a str, ApiError>) -> HttpResponse<'a> {
    match result {
        Ok(json) => HttpResponse {
            status: 200,
            content_type: CONTENT_TYPE_JSON,
            body: ResponseBody::Buffer(json.as_bytes()),
            effects: HttpEffects::none(),
        },
        Err(error) => error_response(error),
    }
}

struct HeaderWriter<'a> {
    bytes: &'a mut [u8],
    len: usize,
}

impl<'a> HeaderWriter<'a> {
    fn new(bytes: &'a mut [u8]) -> Self {
        Self { bytes, len: 0 }
    }

    fn len(&self) -> usize {
        self.len
    }

    fn push(&mut self, value: &str) -> Result<(), ApiError> {
        if self.len + value.len() > self.bytes.len() {
            return Err(ApiError::ResponseTooLarge);
        }
        self.bytes[self.len..self.len + value.len()].copy_from_slice(value.as_bytes());
        self.len += value.len();
        Ok(())
    }

    fn push_usize(&mut self, mut value: usize) -> Result<(), ApiError> {
        if value == 0 {
            return self.push("0");
        }
        let mut scratch = [0_u8; 10];
        let mut len = 0;
        while value > 0 {
            if len >= scratch.len() {
                return Err(ApiError::ResponseTooLarge);
            }
            scratch[len] = b'0' + (value % 10) as u8;
            value /= 10;
            len += 1;
        }
        while len > 0 {
            len -= 1;
            self.push(core::str::from_utf8(&scratch[len..len + 1]).unwrap_or(""))?;
        }
        Ok(())
    }
}

fn error_response(error: ApiError) -> HttpResponse<'static> {
    let (status, text) = match error {
        ApiError::NotFound => (404, b"not found".as_slice()),
        ApiError::MethodNotAllowed => (405, b"method not allowed".as_slice()),
        ApiError::ResponseTooLarge => (500, b"response too large".as_slice()),
        ApiError::InvalidSsid
        | ApiError::InvalidPassword
        | ApiError::InvalidBrightness
        | ApiError::InvalidLanguage
        | ApiError::InvalidRotation
        | ApiError::InvalidSpeedUnit
        | ApiError::InvalidMac
        | ApiError::InvalidJson
        | ApiError::Text(_) => (400, b"bad request".as_slice()),
    };

    HttpResponse {
        status,
        content_type: CONTENT_TYPE_TEXT,
        body: ResponseBody::Static(text),
        effects: HttpEffects::none(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::settings::SetupApState;

    const INDEX: &[u8] = b"<html></html>";
    const ASSETS: EmbeddedAssets = EmbeddedAssets {
        index_html: INDEX,
        index_content_type: "text/html",
    };

    fn app_state_with_password() -> AppState {
        let mut state = AppState::default();
        state
            .settings
            .wifi
            .set_setup_ap_password("12345678")
            .unwrap();
        state
    }

    fn get(path: &'static str) -> HttpRequest<'static> {
        HttpRequest {
            method: HttpMethod::Get,
            path,
            body: "",
            setup_password: Some("12345678"),
            authorization: None,
            cross_origin: false,
        }
    }

    fn post(path: &'static str, body: &'static str) -> HttpRequest<'static> {
        HttpRequest {
            method: HttpMethod::Post,
            path,
            body,
            setup_password: Some("12345678"),
            authorization: None,
            cross_origin: false,
        }
    }

    #[test]
    fn serves_index_and_status_json() {
        let mut app_state = app_state_with_password();
        let mut out = [0_u8; 512];

        let index = handle_request(get("/"), &mut app_state, "0.1.0", ASSETS, &mut out);
        assert_eq!(index.status, 200);
        assert_eq!(index.body, ResponseBody::Static(INDEX));
        assert_eq!(index.effects, HttpEffects::none());

        let status = handle_request(
            get("/api/status"),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(status.status, 200);
        assert_eq!(status.content_type, CONTENT_TYPE_JSON);
        assert_eq!(status.effects, HttpEffects::none());
        let ResponseBody::Buffer(body) = status.body else {
            panic!("expected buffered JSON");
        };
        let json = core::str::from_utf8(body).unwrap();
        assert!(json.contains(r#""version":"0.1.0""#));
    }

    #[test]
    fn applies_post_routes_to_app_state() {
        let mut app_state = app_state_with_password();
        let mut out = [0_u8; 512];

        let response = handle_request(
            post("/api/wifi", r#"{"ssid":"garage","password":"secretpass"}"#),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(response.status, 204);
        assert_eq!(app_state.settings.wifi.external_ssid.as_str(), "garage");
        assert_eq!(response.effects, HttpEffects::wifi_reconnect_requested());

        let response = handle_request(
            post(
                "/api/config",
                r#"{"brightness":42,"display_rotation":"portrait","speed_unit":"mph"}"#,
            ),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(response.status, 204);
        assert_eq!(app_state.settings.brightness_percent, 42);
        assert_eq!(response.effects, HttpEffects::settings_changed());
    }

    #[test]
    fn serves_bms_candidates_and_scan_effect() {
        let mut app_state = app_state_with_password();
        app_state.bms_scan_candidates.upsert(
            crate::settings::MacAddress::new([1, 2, 3, 4, 5, 6]),
            Some("ANT-24S"),
            -44,
        );
        let mut out = [0_u8; 512];

        let response = handle_request(
            get("/api/bms/candidates"),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(response.status, 200);
        assert_eq!(response.content_type, CONTENT_TYPE_JSON);
        assert_eq!(response.effects, HttpEffects::none());
        let ResponseBody::Buffer(body) = response.body else {
            panic!("expected candidates JSON");
        };
        let json = core::str::from_utf8(body).unwrap();
        assert!(json.contains(r#""mac":"01:02:03:04:05:06""#));
        assert!(json.contains(r#""name":"ANT-24S""#));
        assert!(json.contains(r#""rssi":-44"#));

        let response = handle_request(
            post("/api/bms/scan", ""),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(response.status, 204);
        assert_eq!(response.effects, HttpEffects::bms_scan_requested());
    }

    #[test]
    fn bms_bind_persists_mac_and_requests_scan() {
        let mut app_state = app_state_with_password();
        let mut out = [0_u8; 128];

        let response = handle_request(
            post("/api/bms/bind", r#"{"mac":"AA:BB:CC:DD:EE:FF"}"#),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );

        assert_eq!(response.status, 204);
        assert_eq!(
            app_state.settings.bms.bound_mac,
            Some(crate::settings::MacAddress::new([
                0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
            ]))
        );
        assert_eq!(
            response.effects,
            HttpEffects::settings_changed_and_bms_scan_requested()
        );
    }

    #[test]
    fn reports_http_errors_without_mutating_settings() {
        let mut app_state = app_state_with_password();
        let mut out = [0_u8; 128];

        let response = handle_request(get("/api/wifi"), &mut app_state, "0.1.0", ASSETS, &mut out);
        assert_eq!(response.status, 405);
        assert_eq!(response.effects, HttpEffects::none());

        let response = handle_request(
            post("/api/ap-password", r#"{"password":"short"}"#),
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(response.status, 400);
        assert_eq!(response.effects, HttpEffects::none());
        assert_eq!(
            app_state.settings.wifi.setup_ap_state,
            SetupApState::FirstBoot
        );
    }

    #[test]
    fn maps_http_method_bytes() {
        assert_eq!(method_from_bytes(b"GET"), Some(HttpMethod::Get));
        assert_eq!(method_from_bytes(b"POST"), Some(HttpMethod::Post));
        assert_eq!(method_from_bytes(b"OPTIONS"), Some(HttpMethod::Options));
        assert_eq!(method_from_bytes(b"PUT"), None);
    }

    #[test]
    fn parses_raw_http_get_and_post_requests() {
        let get =
            parse_http_request(b"GET /api/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n").unwrap();
        assert_eq!(get.method, HttpMethod::Get);
        assert_eq!(get.path, "/api/status");
        assert_eq!(get.body, "");
        assert_eq!(get.authorization, None);
        assert!(!get.cross_origin);

        let post = parse_http_request(
            b"POST /api/wifi HTTP/1.1\r\nOrigin: https://example.vercel.app\r\nX-Setup-Password: 12345678\r\nAuthorization: Basic ZXNwMzI6MTIzNDU2Nzg=\r\nContent-Length: 41\r\n\r\n{\"ssid\":\"garage\",\"password\":\"secretpass\"}tail",
        )
        .unwrap();
        assert_eq!(post.method, HttpMethod::Post);
        assert_eq!(post.path, "/api/wifi");
        assert_eq!(post.body, r#"{"ssid":"garage","password":"secretpass"}"#);
        assert_eq!(post.setup_password, Some("12345678"));
        assert_eq!(post.authorization, Some("Basic ZXNwMzI6MTIzNDU2Nzg="));
        assert!(post.cross_origin);
    }

    #[test]
    fn api_requests_require_setup_password() {
        let mut app_state = app_state_with_password();
        let mut out = [0_u8; 512];

        let denied = handle_request(
            HttpRequest {
                method: HttpMethod::Get,
                path: "/api/status",
                body: "",
                setup_password: Some("bad"),
                authorization: None,
                cross_origin: true,
            },
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(denied.status, 401);

        let allowed = handle_request(
            HttpRequest {
                method: HttpMethod::Get,
                path: "/api/status",
                body: "",
                setup_password: Some("12345678"),
                authorization: None,
                cross_origin: true,
            },
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(allowed.status, 200);

        let basic = handle_request(
            HttpRequest {
                method: HttpMethod::Get,
                path: "/api/status",
                body: "",
                setup_password: None,
                authorization: Some("Basic ZXNwMzI6MTIzNDU2Nzg="),
                cross_origin: true,
            },
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(basic.status, 200);

        let preflight = handle_request(
            HttpRequest {
                method: HttpMethod::Options,
                path: "/api/status",
                body: "",
                setup_password: None,
                authorization: None,
                cross_origin: true,
            },
            &mut app_state,
            "0.1.0",
            ASSETS,
            &mut out,
        );
        assert_eq!(preflight.status, 204);
    }

    #[test]
    fn rejects_incomplete_or_unsupported_raw_http_requests() {
        assert_eq!(
            parse_http_request(b"PUT /api/status HTTP/1.1\r\n\r\n"),
            Err(HttpParseError::UnsupportedMethod)
        );
        assert_eq!(
            parse_http_request(b"POST /api/wifi HTTP/1.1\r\nContent-Length: 2\r\n\r\n{"),
            Err(HttpParseError::BodyIncomplete)
        );
        assert_eq!(
            parse_http_request(b"GET /api/status HTTP/1.1\r\n"),
            Err(HttpParseError::MissingHeadersEnd)
        );
    }

    #[test]
    fn writes_http_response_headers() {
        let response = HttpResponse {
            status: 200,
            content_type: CONTENT_TYPE_JSON,
            body: ResponseBody::Static(b"{}"),
            effects: HttpEffects::none(),
        };
        let mut out = [0_u8; 512];

        let len = write_response_headers(&response, body_len(response.body), &mut out).unwrap();
        let text = core::str::from_utf8(&out[..len]).unwrap();

        assert!(text.starts_with("HTTP/1.1 200 OK\r\n"));
        assert!(text.contains("Content-Type: application/json\r\n"));
        assert!(text.contains("Content-Length: 2\r\n"));
        assert!(text.contains("Connection: close\r\n"));
        assert!(text.contains("Access-Control-Allow-Origin: *\r\n"));
        assert!(text.contains(
            "Access-Control-Allow-Headers: Content-Type, X-Setup-Password, Authorization\r\n"
        ));
        assert!(text.contains("Access-Control-Max-Age: 600\r\n"));
        assert!(text.contains("Access-Control-Allow-Private-Network: true\r\n"));
        assert!(text.ends_with("Private-Network-Access-ID: \"02:00:00:00:00:01\"\r\n\r\n"));
    }

    #[test]
    fn unauthorized_response_requests_basic_auth() {
        let response = unauthorized_response();
        let mut out = [0_u8; 512];

        let len = write_response_headers(&response, body_len(response.body), &mut out).unwrap();
        let text = core::str::from_utf8(&out[..len]).unwrap();

        assert!(text.starts_with("HTTP/1.1 401 Unauthorized\r\n"));
        assert!(
            text.contains("WWW-Authenticate: Basic realm=\"esp32-bms-gps\", charset=\"UTF-8\"\r\n")
        );
    }
}
