use crate::{
    app_state::AppState,
    http_api::{
        CONTENT_TYPE_TEXT, EmbeddedAssets, HttpEffects, HttpResponse, ResponseBody, body_len,
        handle_request, parse_http_request, write_response_headers,
    },
    local_api::ApiError,
    runtime_effects::{self, RuntimeActions},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HttpConnectionResponse<'a> {
    pub status: u16,
    pub header_len: usize,
    pub body: ResponseBody<'a>,
    pub effects: HttpEffects,
}

pub fn handle_connection<'a>(
    request_bytes: &[u8],
    app_state: &mut AppState,
    firmware_version: &str,
    assets: EmbeddedAssets,
    json_buffer: &'a mut [u8],
    header_buffer: &mut [u8],
) -> Result<HttpConnectionResponse<'a>, ApiError> {
    let response = match parse_http_request(request_bytes) {
        Ok(request) => handle_request(request, app_state, firmware_version, assets, json_buffer),
        Err(_) => bad_request(),
    };
    let content_len = body_len(response.body);
    let header_len = write_response_headers(&response, content_len, header_buffer)?;

    Ok(HttpConnectionResponse {
        status: response.status,
        header_len,
        body: response.body,
        effects: response.effects,
    })
}

pub fn handle_connection_with_effects<'a>(
    request_bytes: &[u8],
    app_state: &mut AppState,
    firmware_version: &str,
    assets: EmbeddedAssets,
    json_buffer: &'a mut [u8],
    header_buffer: &mut [u8],
) -> Result<(HttpConnectionResponse<'a>, RuntimeActions), ApiError> {
    let response = handle_connection(
        request_bytes,
        app_state,
        firmware_version,
        assets,
        json_buffer,
        header_buffer,
    )?;
    let actions = runtime_effects::apply_http_effects(app_state, response.effects);
    Ok((response, actions))
}

fn bad_request() -> HttpResponse<'static> {
    HttpResponse {
        status: 400,
        content_type: CONTENT_TYPE_TEXT,
        body: ResponseBody::Static(b"bad request"),
        effects: HttpEffects::none(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        app_state::{OtaState, WifiLinkState},
        http_api::CONTENT_TYPE_JSON,
    };

    const INDEX: &[u8] = b"<html>ok</html>";
    const ASSETS: EmbeddedAssets = EmbeddedAssets {
        index_html: INDEX,
        index_content_type: "text/html; charset=utf-8",
    };

    #[test]
    fn handles_raw_get_index_request() {
        let mut state = AppState::default();
        let mut json = [0_u8; 512];
        let mut headers = [0_u8; 768];

        let response = handle_connection(
            b"GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        let header_text = core::str::from_utf8(&headers[..response.header_len]).unwrap();
        assert_eq!(response.status, 200);
        assert_eq!(response.effects, HttpEffects::none());
        assert!(header_text.contains("Content-Type: text/html; charset=utf-8\r\n"));
        assert!(header_text.contains("Content-Length: 15\r\n"));
        assert_eq!(response.body, ResponseBody::Static(INDEX));
    }

    #[test]
    fn handles_raw_api_post_and_mutates_state() {
        let mut state = AppState::default();
        let mut json = [0_u8; 512];
        let mut headers = [0_u8; 768];

        let response = handle_connection(
            b"POST /api/wifi HTTP/1.1\r\nContent-Length: 41\r\n\r\n{\"ssid\":\"garage\",\"password\":\"secretpass\"}",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        let header_text = core::str::from_utf8(&headers[..response.header_len]).unwrap();
        assert_eq!(response.status, 204);
        assert_eq!(state.settings.wifi.external_ssid.as_str(), "garage");
        assert_eq!(state.wifi, WifiLinkState::StationConnecting);
        assert_eq!(response.effects, HttpEffects::wifi_reconnect_requested());
        assert!(header_text.contains("Content-Length: 0\r\n"));
        assert_eq!(response.body, ResponseBody::Empty);
    }

    #[test]
    fn handles_raw_status_json_request() {
        let mut state = AppState::default();
        let mut json = [0_u8; 512];
        let mut headers = [0_u8; 768];

        let response = handle_connection(
            b"GET /api/status HTTP/1.1\r\n\r\n",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        let header_text = core::str::from_utf8(&headers[..response.header_len]).unwrap();
        let ResponseBody::Buffer(body) = response.body else {
            panic!("expected JSON body");
        };
        let body_text = core::str::from_utf8(body).unwrap();

        assert_eq!(response.status, 200);
        assert_eq!(response.effects, HttpEffects::none());
        assert!(header_text.contains(CONTENT_TYPE_JSON));
        assert!(body_text.contains(r#""version":"0.1.0""#));
    }

    #[test]
    fn maps_bad_raw_http_to_bad_request_response() {
        let mut state = AppState::default();
        let mut json = [0_u8; 128];
        let mut headers = [0_u8; 768];

        let response = handle_connection(
            b"GET /api/status HTTP/1.1\r\n",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        let header_text = core::str::from_utf8(&headers[..response.header_len]).unwrap();
        assert_eq!(response.status, 400);
        assert_eq!(response.effects, HttpEffects::none());
        assert!(header_text.starts_with("HTTP/1.1 400 Bad Request\r\n"));
        assert_eq!(response.body, ResponseBody::Static(b"bad request"));
    }

    #[test]
    fn raw_ota_start_sets_downloading_state() {
        let mut state = AppState::default();
        let mut json = [0_u8; 128];
        let mut headers = [0_u8; 768];

        let response = handle_connection(
            b"POST /api/ota/start HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        assert_eq!(response.status, 204);
        assert_eq!(state.ota, OtaState::Downloading);
        assert_eq!(response.effects, HttpEffects::ota_start_requested());
    }

    #[test]
    fn connection_effects_report_persist_and_reconnect_actions() {
        let mut state = AppState::default();
        let mut json = [0_u8; 128];
        let mut headers = [0_u8; 768];

        let (response, actions) = handle_connection_with_effects(
            b"POST /api/wifi HTTP/1.1\r\nContent-Length: 41\r\n\r\n{\"ssid\":\"garage\",\"password\":\"secretpass\"}",
            &mut state,
            "0.1.0",
            ASSETS,
            &mut json,
            &mut headers,
        )
        .unwrap();

        assert_eq!(response.status, 204);
        assert!(actions.persist_settings);
        assert!(actions.reconnect_wifi);
        assert_eq!(state.wifi, WifiLinkState::StationConnecting);
    }
}
