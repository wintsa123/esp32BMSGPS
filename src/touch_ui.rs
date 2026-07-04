use crate::{
    gps_nmea::SpeedUnit,
    settings::{DeviceSettings, DisplayRotation, Language, ScreenPoint, SetupApState},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UiScreen {
    Dashboard,
    Settings,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UiAction {
    None,
    ShowDashboard,
    ShowSettings,
    EnableWifiReprovisioning,
    CycleBrightness,
    RotateDisplay,
    ToggleSpeedUnit,
    ToggleLanguage,
    StartBmsBind,
    RestoreDefaults,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TouchUi {
    pub screen: UiScreen,
}

impl TouchUi {
    pub const fn new() -> Self {
        Self {
            screen: UiScreen::Dashboard,
        }
    }

    pub fn handle_tap(&mut self, point: ScreenPoint, screen_height: u16) -> UiAction {
        match self.screen {
            UiScreen::Dashboard => {
                if point.y >= screen_height.saturating_sub(48) {
                    self.screen = UiScreen::Settings;
                    UiAction::ShowSettings
                } else {
                    UiAction::None
                }
            }
            UiScreen::Settings => self.handle_settings_tap(point),
        }
    }

    fn handle_settings_tap(&mut self, point: ScreenPoint) -> UiAction {
        if point.y < 36 {
            self.screen = UiScreen::Dashboard;
            return UiAction::ShowDashboard;
        }

        match point.y {
            44..=71 => UiAction::EnableWifiReprovisioning,
            72..=99 => UiAction::CycleBrightness,
            100..=127 => UiAction::RotateDisplay,
            128..=155 => UiAction::ToggleSpeedUnit,
            156..=183 => UiAction::ToggleLanguage,
            184..=211 => UiAction::StartBmsBind,
            212..=239 => UiAction::RestoreDefaults,
            _ => UiAction::None,
        }
    }
}

impl Default for TouchUi {
    fn default() -> Self {
        Self::new()
    }
}

pub fn apply_action(settings: &mut DeviceSettings, action: UiAction) -> bool {
    match action {
        UiAction::EnableWifiReprovisioning => {
            settings.wifi.setup_ap_state = SetupApState::Reprovisioning;
            true
        }
        UiAction::CycleBrightness => {
            settings.brightness_percent = match settings.brightness_percent {
                0..=40 => 60,
                41..=70 => 85,
                _ => 30,
            };
            true
        }
        UiAction::RotateDisplay => {
            settings.display_rotation = next_rotation(settings.display_rotation);
            true
        }
        UiAction::ToggleSpeedUnit => {
            settings.speed_unit = match settings.speed_unit {
                SpeedUnit::Kmh => SpeedUnit::Mph,
                SpeedUnit::Mph => SpeedUnit::Kmh,
            };
            true
        }
        UiAction::ToggleLanguage => {
            settings.language = match settings.language {
                Language::Chinese => Language::English,
                Language::English => Language::Chinese,
            };
            true
        }
        UiAction::RestoreDefaults => {
            let calibration = settings.touch_calibration;
            *settings = DeviceSettings {
                touch_calibration: calibration,
                ..DeviceSettings::default()
            };
            true
        }
        UiAction::None
        | UiAction::ShowDashboard
        | UiAction::ShowSettings
        | UiAction::StartBmsBind => false,
    }
}

fn next_rotation(rotation: DisplayRotation) -> DisplayRotation {
    match rotation {
        DisplayRotation::Portrait => DisplayRotation::Landscape,
        DisplayRotation::Landscape => DisplayRotation::InvertedPortrait,
        DisplayRotation::InvertedPortrait => DisplayRotation::InvertedLandscape,
        DisplayRotation::InvertedLandscape => DisplayRotation::Portrait,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bottom_dashboard_tap_opens_settings() {
        let mut ui = TouchUi::new();

        let action = ui.handle_tap(ScreenPoint { x: 20, y: 220 }, 240);

        assert_eq!(action, UiAction::ShowSettings);
        assert_eq!(ui.screen, UiScreen::Settings);
    }

    #[test]
    fn settings_rows_map_to_actions() {
        let mut ui = TouchUi {
            screen: UiScreen::Settings,
        };

        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 54 }, 240),
            UiAction::EnableWifiReprovisioning
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 84 }, 240),
            UiAction::CycleBrightness
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 112 }, 240),
            UiAction::RotateDisplay
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 140 }, 240),
            UiAction::ToggleSpeedUnit
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 170 }, 240),
            UiAction::ToggleLanguage
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 198 }, 240),
            UiAction::StartBmsBind
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 226 }, 240),
            UiAction::RestoreDefaults
        );
    }

    #[test]
    fn top_settings_tap_returns_to_dashboard() {
        let mut ui = TouchUi {
            screen: UiScreen::Settings,
        };

        let action = ui.handle_tap(ScreenPoint { x: 20, y: 10 }, 240);

        assert_eq!(action, UiAction::ShowDashboard);
        assert_eq!(ui.screen, UiScreen::Dashboard);
    }

    #[test]
    fn apply_actions_mutates_settings() {
        let mut settings = DeviceSettings::default();

        assert!(apply_action(
            &mut settings,
            UiAction::EnableWifiReprovisioning
        ));
        assert_eq!(settings.wifi.setup_ap_state, SetupApState::Reprovisioning);

        assert!(apply_action(&mut settings, UiAction::RotateDisplay));
        assert_eq!(settings.display_rotation, DisplayRotation::InvertedPortrait);

        assert!(apply_action(&mut settings, UiAction::ToggleSpeedUnit));
        assert_eq!(settings.speed_unit, SpeedUnit::Mph);

        assert!(apply_action(&mut settings, UiAction::ToggleLanguage));
        assert_eq!(settings.language, Language::English);
    }
}
