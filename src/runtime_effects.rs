use crate::{
    app_state::{AppState, OtaState},
    http_api::HttpEffects,
    ota_job::{OtaJob, OtaJobCommand, OtaJobError},
    touch_ui::UiAction,
    wifi_control::{self, WifiEvent},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct RuntimeActions {
    pub persist_settings: bool,
    pub reconnect_wifi: bool,
    pub start_bms_scan: bool,
    pub start_ota_check: bool,
    pub start_ota_download: bool,
}

pub fn apply_http_effects(app_state: &mut AppState, effects: HttpEffects) -> RuntimeActions {
    let mut actions = RuntimeActions {
        persist_settings: effects.settings_changed,
        reconnect_wifi: effects.wifi_reconnect_requested,
        start_bms_scan: effects.bms_scan_requested,
        start_ota_check: effects.ota_check_requested,
        start_ota_download: effects.ota_start_requested,
    };

    if effects.wifi_reconnect_requested {
        wifi_control::apply_wifi_event(app_state, WifiEvent::StationConnectRequested);
    }
    if effects.ota_check_requested {
        app_state.ota = OtaState::Checking;
    }
    if effects.ota_start_requested {
        app_state.ota = OtaState::Downloading;
    }

    if actions.start_ota_download {
        actions.start_ota_check = false;
    }

    actions
}

pub fn apply_wifi_event(app_state: &mut AppState, event: WifiEvent) -> RuntimeActions {
    let outcome = wifi_control::apply_wifi_event(app_state, event);
    RuntimeActions {
        persist_settings: outcome.settings_changed,
        ..RuntimeActions::default()
    }
}

pub fn apply_touch_action(action: UiAction) -> RuntimeActions {
    RuntimeActions {
        start_bms_scan: matches!(action, UiAction::StartBmsBind),
        ..RuntimeActions::default()
    }
}

pub fn apply_ota_actions(
    app_state: &mut AppState,
    ota_job: &mut OtaJob,
    actions: RuntimeActions,
    manifest_url: &str,
) -> Result<OtaJobCommand, OtaJobError> {
    if actions.start_ota_download {
        ota_job.request_start(app_state, manifest_url)
    } else if actions.start_ota_check {
        ota_job.request_check(app_state, manifest_url)
    } else {
        Ok(OtaJobCommand::None)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        app_state::WifiLinkState,
        http_api::HttpEffects,
        settings::{FixedAscii, SetupApState},
    };

    #[test]
    fn http_wifi_effect_requests_persist_and_reconnect() {
        let mut app_state = AppState::default();

        let actions = apply_http_effects(&mut app_state, HttpEffects::wifi_reconnect_requested());

        assert!(actions.persist_settings);
        assert!(actions.reconnect_wifi);
        assert_eq!(app_state.wifi, WifiLinkState::StationConnecting);
    }

    #[test]
    fn wifi_connected_effect_closes_setup_ap_and_persists_state() {
        let mut app_state = AppState::default();
        app_state.settings.wifi.external_ssid = FixedAscii::try_from_str("garage").unwrap();
        app_state.settings.wifi.external_password = FixedAscii::try_from_str("secretpass").unwrap();
        app_state.settings.wifi.external_wifi_saved = true;

        let actions = apply_wifi_event(&mut app_state, WifiEvent::StationConnected);

        assert!(actions.persist_settings);
        assert_eq!(app_state.wifi, WifiLinkState::StationConnected);
        assert_eq!(
            app_state.settings.wifi.setup_ap_state,
            SetupApState::Disabled
        );
    }

    #[test]
    fn ota_effects_request_one_runtime_action() {
        let mut app_state = AppState::default();

        let check = apply_http_effects(&mut app_state, HttpEffects::ota_check_requested());
        assert!(check.start_ota_check);
        assert!(!check.start_ota_download);
        assert_eq!(app_state.ota, OtaState::Checking);

        let start = apply_http_effects(&mut app_state, HttpEffects::ota_start_requested());
        assert!(!start.start_ota_check);
        assert!(start.start_ota_download);
        assert_eq!(app_state.ota, OtaState::Downloading);
    }

    #[test]
    fn http_bms_scan_effect_requests_runtime_scan_only() {
        let mut app_state = AppState::default();

        let actions = apply_http_effects(&mut app_state, HttpEffects::bms_scan_requested());

        assert!(actions.start_bms_scan);
        assert!(!actions.persist_settings);
        assert!(!actions.reconnect_wifi);
        assert!(!actions.start_ota_check);
        assert!(!actions.start_ota_download);
    }

    #[test]
    fn http_bms_bind_effect_requests_persist_and_scan() {
        let mut app_state = AppState::default();

        let actions = apply_http_effects(
            &mut app_state,
            HttpEffects::settings_changed_and_bms_scan_requested(),
        );

        assert!(actions.persist_settings);
        assert!(actions.start_bms_scan);
        assert!(!actions.reconnect_wifi);
        assert!(!actions.start_ota_check);
        assert!(!actions.start_ota_download);
    }

    #[test]
    fn ota_actions_drive_job_commands() {
        let mut app_state = AppState::default();
        let mut ota_job = OtaJob::new();
        let actions = apply_http_effects(&mut app_state, HttpEffects::ota_check_requested());

        assert!(matches!(
            apply_ota_actions(
                &mut app_state,
                &mut ota_job,
                actions,
                "https://example.com/manifest.json"
            ),
            Ok(OtaJobCommand::FetchManifest { .. })
        ));
        assert_eq!(app_state.ota, OtaState::Checking);

        let actions = apply_http_effects(&mut app_state, HttpEffects::ota_start_requested());
        assert!(matches!(
            apply_ota_actions(
                &mut app_state,
                &mut ota_job,
                actions,
                "https://example.com/manifest.json"
            ),
            Ok(OtaJobCommand::FetchManifest { .. })
        ));
        assert_eq!(app_state.ota, OtaState::Checking);
    }

    #[test]
    fn touch_bms_bind_requests_ble_scan() {
        assert!(apply_touch_action(UiAction::StartBmsBind).start_bms_scan);
        assert!(!apply_touch_action(UiAction::RotateDisplay).start_bms_scan);
    }
}
