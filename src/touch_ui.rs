use crate::{
    gps_nmea::SpeedUnit,
    settings::{DeviceSettings, DisplayRotation, Language, ScreenPoint, SetupApState},
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UiScreen {
    Dashboard,
    QuickMenu,
    Settings,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UiAction {
    None,
    ShowDashboard,
    ShowQuickMenu,
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
pub enum QuickMenuButton {
    Settings,
    RotateDisplay,
}

pub const QUICK_MENU_PANEL_HEIGHT: u16 = 120;
pub const QUICK_MENU_BUTTON_RADIUS: u16 = 28;

const QUICK_MENU_BUTTON_Y: u16 = 68;
const DASHBOARD_QUICK_MENU_START_MAX_Y: u16 = 96;
const PULL_DOWN_MIN_DELTA_Y: u16 = 22;
const TAP_MAX_DELTA: u16 = 20;

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

    pub fn handle_gesture(
        &mut self,
        start: ScreenPoint,
        end: ScreenPoint,
        screen_width: u16,
        screen_height: u16,
    ) -> UiAction {
        if matches!(self.screen, UiScreen::Dashboard) && is_pull_down(start, end) {
            self.screen = UiScreen::QuickMenu;
            return UiAction::ShowQuickMenu;
        }

        if !is_tap(start, end) {
            return UiAction::None;
        }

        self.handle_tap(end, screen_width, screen_height)
    }

    pub fn handle_touch_start(&mut self, point: ScreenPoint) -> UiAction {
        if matches!(self.screen, UiScreen::Dashboard) && point.y <= DASHBOARD_QUICK_MENU_START_MAX_Y
        {
            self.screen = UiScreen::QuickMenu;
            UiAction::ShowQuickMenu
        } else {
            UiAction::None
        }
    }

    pub fn handle_tap(
        &mut self,
        point: ScreenPoint,
        screen_width: u16,
        _screen_height: u16,
    ) -> UiAction {
        match self.screen {
            UiScreen::Dashboard => {
                if point.y <= DASHBOARD_QUICK_MENU_START_MAX_Y {
                    self.screen = UiScreen::QuickMenu;
                    UiAction::ShowQuickMenu
                } else {
                    UiAction::None
                }
            }
            UiScreen::QuickMenu => self.handle_quick_menu_tap(point, screen_width),
            UiScreen::Settings => self.handle_settings_tap(point),
        }
    }

    fn handle_quick_menu_tap(&mut self, point: ScreenPoint, screen_width: u16) -> UiAction {
        match quick_menu_button_at(point, screen_width) {
            Some(QuickMenuButton::Settings) => {
                self.screen = UiScreen::Settings;
                UiAction::ShowSettings
            }
            Some(QuickMenuButton::RotateDisplay) => UiAction::RotateDisplay,
            None if point.y > QUICK_MENU_PANEL_HEIGHT => {
                self.screen = UiScreen::Dashboard;
                UiAction::ShowDashboard
            }
            None => UiAction::None,
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
        | UiAction::ShowQuickMenu
        | UiAction::ShowDashboard
        | UiAction::ShowSettings
        | UiAction::StartBmsBind => false,
    }
}

pub fn quick_menu_button_centers(screen_width: u16) -> (ScreenPoint, ScreenPoint) {
    let center_x = screen_width / 2;
    let distance = (screen_width / 3).max(80);
    (
        ScreenPoint {
            x: center_x.saturating_sub(distance / 2),
            y: QUICK_MENU_BUTTON_Y,
        },
        ScreenPoint {
            x: center_x.saturating_add(distance / 2),
            y: QUICK_MENU_BUTTON_Y,
        },
    )
}

pub fn quick_menu_button_at(point: ScreenPoint, screen_width: u16) -> Option<QuickMenuButton> {
    let (settings, rotate) = quick_menu_button_centers(screen_width);
    if point_in_circle(point, settings, QUICK_MENU_BUTTON_RADIUS) {
        Some(QuickMenuButton::Settings)
    } else if point_in_circle(point, rotate, QUICK_MENU_BUTTON_RADIUS) {
        Some(QuickMenuButton::RotateDisplay)
    } else {
        None
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

fn is_pull_down(start: ScreenPoint, end: ScreenPoint) -> bool {
    end.y >= start.y.saturating_add(PULL_DOWN_MIN_DELTA_Y)
}

fn is_tap(start: ScreenPoint, end: ScreenPoint) -> bool {
    abs_diff(start.x, end.x) <= TAP_MAX_DELTA && abs_diff(start.y, end.y) <= TAP_MAX_DELTA
}

fn point_in_circle(point: ScreenPoint, center: ScreenPoint, radius: u16) -> bool {
    let dx = abs_diff(point.x, center.x) as u32;
    let dy = abs_diff(point.y, center.y) as u32;
    dx * dx + dy * dy <= radius as u32 * radius as u32
}

fn abs_diff(a: u16, b: u16) -> u16 {
    a.max(b) - a.min(b)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dashboard_pull_down_opens_quick_menu() {
        let mut ui = TouchUi::new();

        let action = ui.handle_gesture(
            ScreenPoint { x: 120, y: 10 },
            ScreenPoint { x: 122, y: 86 },
            240,
            320,
        );

        assert_eq!(action, UiAction::ShowQuickMenu);
        assert_eq!(ui.screen, UiScreen::QuickMenu);
    }

    #[test]
    fn dashboard_middle_pull_down_opens_quick_menu() {
        let mut ui = TouchUi::new();

        let action = ui.handle_gesture(
            ScreenPoint { x: 120, y: 160 },
            ScreenPoint { x: 120, y: 190 },
            240,
            320,
        );

        assert_eq!(action, UiAction::ShowQuickMenu);
        assert_eq!(ui.screen, UiScreen::QuickMenu);
    }

    #[test]
    fn dashboard_tap_does_not_open_settings() {
        let mut ui = TouchUi::new();

        let action = ui.handle_tap(ScreenPoint { x: 20, y: 220 }, 240, 320);

        assert_eq!(action, UiAction::None);
        assert_eq!(ui.screen, UiScreen::Dashboard);
    }

    #[test]
    fn dashboard_top_edge_tap_opens_quick_menu() {
        let mut ui = TouchUi::new();

        let action = ui.handle_tap(ScreenPoint { x: 120, y: 20 }, 240, 320);

        assert_eq!(action, UiAction::ShowQuickMenu);
        assert_eq!(ui.screen, UiScreen::QuickMenu);
    }

    #[test]
    fn dashboard_top_region_touch_start_opens_quick_menu() {
        let mut ui = TouchUi::new();

        let action = ui.handle_touch_start(ScreenPoint { x: 120, y: 88 });

        assert_eq!(action, UiAction::ShowQuickMenu);
        assert_eq!(ui.screen, UiScreen::QuickMenu);
    }

    #[test]
    fn quick_menu_buttons_map_to_actions() {
        let mut ui = TouchUi {
            screen: UiScreen::QuickMenu,
        };
        let (settings, rotate) = quick_menu_button_centers(240);

        let action = ui.handle_tap(settings, 240, 320);
        assert_eq!(action, UiAction::ShowSettings);
        assert_eq!(ui.screen, UiScreen::Settings);

        ui.screen = UiScreen::QuickMenu;
        let action = ui.handle_tap(rotate, 240, 320);
        assert_eq!(action, UiAction::RotateDisplay);
        assert_eq!(ui.screen, UiScreen::QuickMenu);
    }

    #[test]
    fn quick_menu_outside_tap_returns_to_dashboard() {
        let mut ui = TouchUi {
            screen: UiScreen::QuickMenu,
        };

        let action = ui.handle_tap(ScreenPoint { x: 120, y: 180 }, 240, 320);

        assert_eq!(action, UiAction::ShowDashboard);
        assert_eq!(ui.screen, UiScreen::Dashboard);
    }

    #[test]
    fn settings_rows_map_to_actions() {
        let mut ui = TouchUi {
            screen: UiScreen::Settings,
        };

        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 54 }, 320, 240),
            UiAction::EnableWifiReprovisioning
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 84 }, 320, 240),
            UiAction::CycleBrightness
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 112 }, 320, 240),
            UiAction::RotateDisplay
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 140 }, 320, 240),
            UiAction::ToggleSpeedUnit
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 170 }, 320, 240),
            UiAction::ToggleLanguage
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 198 }, 320, 240),
            UiAction::StartBmsBind
        );
        assert_eq!(
            ui.handle_tap(ScreenPoint { x: 20, y: 226 }, 320, 240),
            UiAction::RestoreDefaults
        );
    }

    #[test]
    fn top_settings_tap_returns_to_dashboard() {
        let mut ui = TouchUi {
            screen: UiScreen::Settings,
        };

        let action = ui.handle_tap(ScreenPoint { x: 20, y: 10 }, 320, 240);

        assert_eq!(action, UiAction::ShowDashboard);
        assert_eq!(ui.screen, UiScreen::Dashboard);
    }

    #[test]
    fn settings_pull_down_does_not_open_quick_menu() {
        let mut ui = TouchUi {
            screen: UiScreen::Settings,
        };

        let action = ui.handle_gesture(
            ScreenPoint { x: 120, y: 120 },
            ScreenPoint { x: 120, y: 170 },
            240,
            320,
        );

        assert_eq!(action, UiAction::None);
        assert_eq!(ui.screen, UiScreen::Settings);
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
