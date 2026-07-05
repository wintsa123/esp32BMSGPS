use core::{
    ffi::{c_char, c_void},
    fmt::Write,
    ptr, slice,
    sync::atomic::{AtomicBool, AtomicPtr, AtomicU8, AtomicU32, Ordering},
};

use esp32_bms_gps::{
    app_state::{AppState, DashboardSnapshot, WifiLinkState},
    gps_nmea::SpeedUnit,
    settings::{DeviceSettings, DisplayRotation, ScreenPoint, SetupApState},
    touch_ui::{UiAction, UiScreen},
};
use heapless::String;
use oxivgl_sys::*;

use crate::display::St7789;

const MAX_SCREEN_WIDTH: usize = 320;
const LVGL_BUF_LINES: usize = 32;
const LVGL_BUF_BYTES: usize = MAX_SCREEN_WIDTH * LVGL_BUF_LINES * 2;
const LVGL_TICK_MS: u32 = 25;
const QUICK_PANEL_HEIGHT: i32 = 120;
const PULL_DOWN_DELTA: u16 = 18;
const TOAST_MS: u32 = 1_500;

const ACTION_NONE: u8 = 0;
const ACTION_SHOW_DASHBOARD: u8 = 1;
const ACTION_SHOW_QUICK_MENU: u8 = 2;
const ACTION_SHOW_SETTINGS: u8 = 3;
const ACTION_ENABLE_WIFI: u8 = 4;
const ACTION_BRIGHTNESS: u8 = 5;
const ACTION_ROTATE: u8 = 6;
const ACTION_SPEED_UNIT: u8 = 7;
const ACTION_LANGUAGE: u8 = 8;
const ACTION_BMS_BIND: u8 = 9;
const ACTION_RESTORE: u8 = 10;

#[repr(align(4))]
#[allow(dead_code)]
struct DrawBuffer([u8; LVGL_BUF_BYTES]);

static mut DRAW_BUF_1: DrawBuffer = DrawBuffer([0; LVGL_BUF_BYTES]);
static mut DRAW_BUF_2: DrawBuffer = DrawBuffer([0; LVGL_BUF_BYTES]);

static DISPLAY_PTR: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());
static LVGL_INITIALIZED: AtomicBool = AtomicBool::new(false);
static TOUCH_XY: AtomicU32 = AtomicU32::new(0);
static TOUCH_PRESSED: AtomicBool = AtomicBool::new(false);
static PENDING_ACTION: AtomicU8 = AtomicU8::new(ACTION_NONE);

#[derive(Clone, Copy, Default)]
struct DashboardWidgets {
    speed_arc: *mut lv_obj_t,
    speed_value: *mut lv_obj_t,
    speed_unit: *mut lv_obj_t,
    bms_state: *mut lv_obj_t,
    soc: *mut lv_obj_t,
    voltage: *mut lv_obj_t,
    current: *mut lv_obj_t,
    capacity: *mut lv_obj_t,
    cells: *mut lv_obj_t,
    local_battery: *mut lv_obj_t,
    wifi: *mut lv_obj_t,
}

#[derive(Clone, Copy, Default)]
struct SettingsWidgets {
    setup_ap: *mut lv_obj_t,
    brightness: *mut lv_obj_t,
    rotation: *mut lv_obj_t,
    speed_unit: *mut lv_obj_t,
    language: *mut lv_obj_t,
    bms: *mut lv_obj_t,
}

pub struct LvglUi {
    display: *mut lv_display_t,
    _pointer: *mut lv_indev_t,
    screen: UiScreen,
    width: i32,
    height: i32,
    dashboard: *mut lv_obj_t,
    quick_panel: *mut lv_obj_t,
    settings_page: *mut lv_obj_t,
    toast: *mut lv_obj_t,
    toast_started: u32,
    toast_duration: u32,
    last_touch: Option<ScreenPoint>,
    gesture_start: Option<ScreenPoint>,
    dashboard_widgets: DashboardWidgets,
    settings_widgets: SettingsWidgets,
    last_dashboard: Option<DashboardSnapshot>,
    last_settings: Option<DeviceSettings>,
}

impl LvglUi {
    pub fn new(display_driver: &mut St7789<'_>, rotation: DisplayRotation) -> Self {
        let (width, height) = rotation.logical_size();
        DISPLAY_PTR.store(
            display_driver as *mut St7789<'_> as *mut c_void,
            Ordering::Release,
        );

        assert!(
            !LVGL_INITIALIZED.swap(true, Ordering::SeqCst),
            "lv_init called twice"
        );
        unsafe {
            lv_init();
        }
        let display = unsafe { init_display(width as i32, height as i32) };
        let pointer = unsafe { init_pointer(display) };

        let mut ui = Self {
            display,
            _pointer: pointer,
            screen: UiScreen::Dashboard,
            width: width as i32,
            height: height as i32,
            dashboard: ptr::null_mut(),
            quick_panel: ptr::null_mut(),
            settings_page: ptr::null_mut(),
            toast: ptr::null_mut(),
            toast_started: 0,
            toast_duration: 0,
            last_touch: None,
            gesture_start: None,
            dashboard_widgets: DashboardWidgets::default(),
            settings_widgets: SettingsWidgets::default(),
            last_dashboard: None,
            last_settings: None,
        };
        ui.rebuild(rotation);
        ui
    }

    pub fn feed_touch(&mut self, point: Option<ScreenPoint>) {
        match point {
            Some(point) => {
                if self.last_touch.is_none() {
                    self.gesture_start = Some(point);
                }
                self.last_touch = Some(point);
                TOUCH_XY.store(((point.x as u32) << 16) | point.y as u32, Ordering::Relaxed);
                TOUCH_PRESSED.store(true, Ordering::Release);

                if matches!(self.screen, UiScreen::Dashboard)
                    && let Some(start) = self.gesture_start
                    && point.y >= start.y.saturating_add(PULL_DOWN_DELTA)
                {
                    post_action(UiAction::ShowQuickMenu);
                    self.gesture_start = None;
                }
            }
            None => {
                TOUCH_PRESSED.store(false, Ordering::Release);
                self.last_touch = None;
                self.gesture_start = None;
            }
        }
    }

    pub fn tick(&mut self) {
        unsafe {
            lv_tick_inc(LVGL_TICK_MS);
            let _ = lv_timer_handler();
        }
        self.expire_toast();
    }

    pub fn take_action(&mut self) -> UiAction {
        code_to_action(PENDING_ACTION.swap(ACTION_NONE, Ordering::AcqRel))
    }

    pub fn apply_action(&mut self, action: UiAction, app_state: &AppState) {
        match action {
            UiAction::ShowQuickMenu => self.show_quick_menu(),
            UiAction::ShowSettings => self.show_settings(&app_state.settings),
            UiAction::ShowDashboard => {
                self.show_dashboard();
                self.show_bms_notice_if_needed(app_state);
            }
            UiAction::RotateDisplay => {
                self.update(app_state);
            }
            UiAction::EnableWifiReprovisioning
            | UiAction::CycleBrightness
            | UiAction::ToggleSpeedUnit
            | UiAction::ToggleLanguage
            | UiAction::StartBmsBind
            | UiAction::RestoreDefaults => {
                self.update(app_state);
                if matches!(action, UiAction::StartBmsBind) {
                    self.show_toast("BMS SCAN", "SEARCHING");
                }
            }
            UiAction::None => {}
        }
    }

    pub fn set_rotation(&mut self, rotation: DisplayRotation, app_state: &AppState) {
        let (width, height) = rotation.logical_size();
        self.width = width as i32;
        self.height = height as i32;
        unsafe {
            lv_display_set_resolution(self.display, self.width, self.height);
        }
        self.last_dashboard = None;
        self.last_settings = None;
        self.rebuild(rotation);
        self.update(app_state);
    }

    pub fn update(&mut self, app_state: &AppState) {
        let dashboard = app_state.dashboard_snapshot();
        if self.last_dashboard != Some(dashboard) {
            self.update_dashboard(&dashboard);
            self.last_dashboard = Some(dashboard);
        }

        if self.last_settings != Some(app_state.settings) {
            self.update_settings(&app_state.settings);
            self.last_settings = Some(app_state.settings);
        }
    }

    fn rebuild(&mut self, _rotation: DisplayRotation) {
        unsafe {
            let screen = lv_screen_active();
            lv_obj_clean(screen);
            lv_obj_set_style_bg_color(screen, color(0x050708), 0);
            lv_obj_set_style_bg_opa(screen, _lv_opacity_level_t_LV_OPA_COVER as u8, 0);
            lv_obj_set_scrollbar_mode(screen, lv_scrollbar_mode_t_LV_SCROLLBAR_MODE_OFF);

            self.dashboard_widgets = DashboardWidgets::default();
            self.settings_widgets = SettingsWidgets::default();
            self.dashboard = self.create_dashboard(screen);
            self.quick_panel = self.create_quick_panel(screen);
            self.settings_page = self.create_settings_page(screen);
            self.toast = ptr::null_mut();
            self.toast_duration = 0;

            match self.screen {
                UiScreen::Dashboard => {
                    lv_obj_set_y(self.quick_panel, -QUICK_PANEL_HEIGHT);
                    lv_obj_set_y(self.settings_page, -self.height);
                }
                UiScreen::QuickMenu => {
                    lv_obj_set_y(self.quick_panel, 0);
                    lv_obj_set_y(self.settings_page, -self.height);
                }
                UiScreen::Settings => {
                    lv_obj_set_y(self.quick_panel, -QUICK_PANEL_HEIGHT);
                    lv_obj_set_y(self.settings_page, 0);
                    lv_obj_move_foreground(self.settings_page);
                }
            }
        }
    }

    unsafe fn create_dashboard(&mut self, parent: *mut lv_obj_t) -> *mut lv_obj_t {
        let root = unsafe { lv_obj_create(parent) };
        base_obj(root, 0, 0, self.width, self.height, 0x050708, 0);
        unsafe {
            lv_obj_add_flag(root, lv_obj_flag_t_LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(
                root,
                Some(dashboard_event_cb),
                lv_event_code_t_LV_EVENT_GESTURE,
                ptr::null_mut(),
            );
        }

        let hint = unsafe { lv_obj_create(root) };
        base_obj(hint, self.width / 2 - 24, 7, 48, 4, 0x65717a, 2);

        if self.width >= self.height {
            self.create_landscape_dashboard(root);
        } else {
            self.create_portrait_dashboard(root);
        }

        root
    }

    fn create_landscape_dashboard(&mut self, root: *mut lv_obj_t) {
        let arc_size = 150;
        let arc = unsafe { lv_arc_create(root) };
        base_obj(arc, 12, 28, arc_size, arc_size, 0x050708, 0);
        unsafe {
            lv_arc_set_range(arc, 0, 160);
            lv_arc_set_bg_angles(arc, 135.0, 45.0);
            lv_arc_set_rotation(arc, 90);
            lv_obj_remove_flag(arc, lv_obj_flag_t_LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(arc, 11, lv_part_t_LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 13, lv_part_t_LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(arc, color(0x24313a), lv_part_t_LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, color(0x20c997), lv_part_t_LV_PART_INDICATOR);
            lv_obj_set_style_opa(arc, 0, lv_part_t_LV_PART_KNOB);
        }
        self.dashboard_widgets.speed_arc = arc;
        self.dashboard_widgets.speed_value =
            label(root, 45, 84, 88, 38, unsafe { &lv_font_montserrat_32 });
        self.dashboard_widgets.speed_unit =
            label(root, 70, 132, 60, 22, unsafe { &lv_font_montserrat_16 });

        let panel = card(root, 170, 28, self.width - 180, 160, 0x10181f);
        self.dashboard_widgets.bms_state =
            label(panel, 10, 10, 104, 22, unsafe { &lv_font_montserrat_16 });
        self.dashboard_widgets.soc =
            label(panel, 10, 38, 104, 26, unsafe { &lv_font_montserrat_24 });
        self.dashboard_widgets.voltage =
            label(panel, 10, 72, 118, 20, unsafe { &lv_font_montserrat_16 });
        self.dashboard_widgets.current =
            label(panel, 10, 98, 118, 20, unsafe { &lv_font_montserrat_16 });
        self.dashboard_widgets.capacity =
            label(panel, 10, 124, 118, 20, unsafe { &lv_font_montserrat_16 });

        self.dashboard_widgets.cells =
            label(root, 14, 194, 190, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.local_battery =
            label(root, 14, 216, 130, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.wifi = label(root, self.width - 128, 216, 116, 18, unsafe {
            &lv_font_montserrat_14
        });
    }

    fn create_portrait_dashboard(&mut self, root: *mut lv_obj_t) {
        let arc_size = 198;
        let arc_x = (self.width - arc_size) / 2;
        let arc = unsafe { lv_arc_create(root) };
        base_obj(arc, arc_x, 28, arc_size, arc_size, 0x050708, 0);
        unsafe {
            lv_arc_set_range(arc, 0, 160);
            lv_arc_set_bg_angles(arc, 135.0, 45.0);
            lv_arc_set_rotation(arc, 90);
            lv_obj_remove_flag(arc, lv_obj_flag_t_LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(arc, 12, lv_part_t_LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 15, lv_part_t_LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(arc, color(0x24313a), lv_part_t_LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, color(0x20c997), lv_part_t_LV_PART_INDICATOR);
            lv_obj_set_style_opa(arc, 0, lv_part_t_LV_PART_KNOB);
        }
        self.dashboard_widgets.speed_arc = arc;
        self.dashboard_widgets.speed_value = label(root, arc_x + 60, 102, 84, 38, unsafe {
            &lv_font_montserrat_32
        });
        self.dashboard_widgets.speed_unit = label(root, arc_x + 78, 150, 70, 22, unsafe {
            &lv_font_montserrat_16
        });

        let panel = card(root, 12, 226, self.width - 24, 76, 0x10181f);
        self.dashboard_widgets.bms_state =
            label(panel, 10, 8, 90, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.soc =
            label(panel, 110, 6, 84, 24, unsafe { &lv_font_montserrat_20 });
        self.dashboard_widgets.voltage =
            label(panel, 10, 34, 84, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.current =
            label(panel, 110, 34, 84, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.capacity =
            label(panel, 10, 54, 160, 18, unsafe { &lv_font_montserrat_14 });

        self.dashboard_widgets.cells = label(root, 12, 306, self.width - 24, 18, unsafe {
            &lv_font_montserrat_14
        });
        self.dashboard_widgets.local_battery =
            label(root, 12, 206, 112, 18, unsafe { &lv_font_montserrat_14 });
        self.dashboard_widgets.wifi = label(root, self.width - 118, 206, 106, 18, unsafe {
            &lv_font_montserrat_14
        });
    }

    unsafe fn create_quick_panel(&mut self, parent: *mut lv_obj_t) -> *mut lv_obj_t {
        let panel = unsafe { lv_obj_create(parent) };
        base_obj(
            panel,
            0,
            -QUICK_PANEL_HEIGHT,
            self.width,
            QUICK_PANEL_HEIGHT,
            0x111a20,
            0,
        );
        unsafe {
            lv_obj_move_foreground(panel);
            lv_obj_set_style_bg_opa(panel, 245, 0);
        }

        let center_y = 63;
        let gap = (self.width / 3).max(82);
        let left = self.width / 2 - gap / 2 - 31;
        let right = self.width / 2 + gap / 2 - 31;
        let settings = circle_button(
            panel,
            left,
            center_y - 31,
            62,
            0x22b8cf,
            UiAction::ShowSettings,
        );
        let rotate = circle_button(
            panel,
            right,
            center_y - 31,
            62,
            0xf5c542,
            UiAction::RotateDisplay,
        );
        symbol_label(settings, LV_SYMBOL_SETTINGS.as_ptr());
        symbol_label(rotate, LV_SYMBOL_REFRESH.as_ptr());

        let grip = unsafe { lv_obj_create(panel) };
        base_obj(
            grip,
            self.width / 2 - 24,
            QUICK_PANEL_HEIGHT - 16,
            48,
            5,
            0x7a858c,
            2,
        );
        panel
    }

    unsafe fn create_settings_page(&mut self, parent: *mut lv_obj_t) -> *mut lv_obj_t {
        let page = unsafe { lv_obj_create(parent) };
        base_obj(page, 0, -self.height, self.width, self.height, 0x081015, 0);
        unsafe {
            lv_obj_move_foreground(page);
        }

        let back = action_button(page, 8, 8, 64, 32, 0x26323a, UiAction::ShowDashboard);
        let back_label = label(back, 12, 7, 42, 18, unsafe { &lv_font_montserrat_14 });
        set_label_text(back_label, "BACK");

        let title = label(page, 82, 14, 120, 24, unsafe { &lv_font_montserrat_20 });
        set_label_text(title, "SETTINGS");

        self.settings_widgets.setup_ap =
            self.settings_row(page, 0, "SETUP AP", UiAction::EnableWifiReprovisioning);
        self.settings_widgets.brightness =
            self.settings_row(page, 1, "BRIGHT", UiAction::CycleBrightness);
        self.settings_widgets.rotation =
            self.settings_row(page, 2, "ROTATE", UiAction::RotateDisplay);
        self.settings_widgets.speed_unit =
            self.settings_row(page, 3, "SPEED", UiAction::ToggleSpeedUnit);
        self.settings_widgets.language =
            self.settings_row(page, 4, "LANG", UiAction::ToggleLanguage);
        self.settings_widgets.bms = self.settings_row(page, 5, "BMS", UiAction::StartBmsBind);
        let _restore = self.settings_row(page, 6, "RESTORE", UiAction::RestoreDefaults);

        page
    }

    fn settings_row(
        &mut self,
        parent: *mut lv_obj_t,
        index: i32,
        title: &str,
        action: UiAction,
    ) -> *mut lv_obj_t {
        let y = 50 + index * 34;
        let row = action_button(parent, 10, y, self.width - 20, 29, 0x121d24, action);
        let title_label = label(row, 10, 6, 86, 18, unsafe { &lv_font_montserrat_14 });
        set_label_text(title_label, title);
        label(row, 104, 6, self.width - 138, 18, unsafe {
            &lv_font_montserrat_14
        })
    }

    fn show_quick_menu(&mut self) {
        if matches!(self.screen, UiScreen::Settings) {
            return;
        }
        self.screen = UiScreen::QuickMenu;
        unsafe {
            lv_obj_move_foreground(self.quick_panel);
            animate_y(self.quick_panel, -QUICK_PANEL_HEIGHT, 0, 170);
        }
    }

    fn show_dashboard(&mut self) {
        match self.screen {
            UiScreen::QuickMenu => unsafe {
                animate_y(self.quick_panel, 0, -QUICK_PANEL_HEIGHT, 150);
            },
            UiScreen::Settings => unsafe {
                animate_y(self.settings_page, 0, -self.height, 190);
            },
            UiScreen::Dashboard => {}
        }
        self.screen = UiScreen::Dashboard;
    }

    fn show_settings(&mut self, settings: &DeviceSettings) {
        self.screen = UiScreen::Settings;
        self.update_settings(settings);
        unsafe {
            lv_obj_set_y(self.settings_page, -self.height);
            lv_obj_move_foreground(self.settings_page);
            animate_y(self.settings_page, -self.height, 0, 190);
        }
    }

    fn show_bms_notice_if_needed(&mut self, app_state: &AppState) {
        if !app_state.dashboard_snapshot().bms_online {
            self.show_toast("BMS OFFLINE", "CHECK BIND");
        }
    }

    fn show_toast(&mut self, title: &str, body: &str) {
        unsafe {
            if !self.toast.is_null() {
                lv_obj_delete(self.toast);
            }
            let w = self.width.min(190);
            let h = 60;
            let x = (self.width - w) / 2;
            let y = (self.height - h) / 2;
            let toast = card(lv_layer_top(), x, y, w, h, 0x26323a);
            lv_obj_move_foreground(toast);
            let title_label = label(toast, 12, 10, w - 24, 20, &lv_font_montserrat_16);
            let body_label = label(toast, 12, 34, w - 24, 18, &lv_font_montserrat_14);
            set_label_text(title_label, title);
            set_label_text(body_label, body);
            self.toast = toast;
            self.toast_started = lv_tick_get();
            self.toast_duration = TOAST_MS;
        }
    }

    fn expire_toast(&mut self) {
        if self.toast.is_null() || self.toast_duration == 0 {
            return;
        }
        let elapsed = unsafe { lv_tick_elaps(self.toast_started) };
        if elapsed >= self.toast_duration {
            unsafe {
                lv_obj_delete(self.toast);
            }
            self.toast = ptr::null_mut();
            self.toast_duration = 0;
        }
    }

    fn update_dashboard(&mut self, snapshot: &DashboardSnapshot) {
        let widgets = self.dashboard_widgets;
        let speed = snapshot.speed_deci_units.unwrap_or(0) / 10;
        unsafe {
            if !widgets.speed_arc.is_null() {
                lv_arc_set_value(widgets.speed_arc, speed.min(160) as i32);
            }
        }

        let mut text: String<48> = String::new();
        if let Some(value) = snapshot.speed_deci_units {
            let _ = write!(text, "{}", value / 10);
        } else {
            let _ = write!(text, "--");
        }
        set_label_text(widgets.speed_value, text.as_str());
        set_label_text(widgets.speed_unit, speed_unit_label(snapshot.speed_unit));
        set_label_text(
            widgets.bms_state,
            if snapshot.bms_online {
                "BMS ONLINE"
            } else {
                "BMS OFF"
            },
        );

        text.clear();
        match snapshot.soc_percent {
            Some(value) => {
                let _ = write!(text, "SOC {}%", value.min(100));
            }
            None => {
                let _ = write!(text, "SOC --%");
            }
        }
        set_label_text(widgets.soc, text.as_str());

        text.clear();
        write_mv_deci(&mut text, "V ", snapshot.pack_voltage_mv);
        set_label_text(widgets.voltage, text.as_str());

        text.clear();
        write_deci_i16(&mut text, "A ", snapshot.current_deci_amps);
        set_label_text(widgets.current, text.as_str());

        text.clear();
        match (snapshot.capacity_remaining_mah, snapshot.total_capacity_mah) {
            (Some(left), Some(total)) => {
                let _ = write!(
                    text,
                    "CAP {}.{} / {}.{}AH",
                    left / 1000,
                    (left % 1000) / 100,
                    total / 1000,
                    (total % 1000) / 100
                );
            }
            _ => {
                let _ = write!(text, "CAP --.-AH");
            }
        }
        set_label_text(widgets.capacity, text.as_str());

        text.clear();
        let _ = write!(text, "CELL ");
        write_mv3_short(&mut text, "HI", snapshot.max_cell_voltage_mv);
        let _ = write!(text, " ");
        write_mv3_short(&mut text, "LO", snapshot.min_cell_voltage_mv);
        let _ = write!(text, " ");
        write_mv3_short(&mut text, "D", snapshot.delta_cell_voltage_mv);
        set_label_text(widgets.cells, text.as_str());

        text.clear();
        match snapshot.local_battery_voltage_mv {
            Some(value) => {
                let _ = write!(
                    text,
                    "LOCAL {}.{}{}V",
                    value / 1000,
                    (value % 1000) / 100,
                    (value % 100) / 10
                );
            }
            None => {
                let _ = write!(text, "LOCAL --.--V");
            }
        }
        set_label_text(widgets.local_battery, text.as_str());

        set_label_text(widgets.wifi, wifi_label(snapshot.wifi));
    }

    fn update_settings(&mut self, settings: &DeviceSettings) {
        set_label_text(
            self.settings_widgets.setup_ap,
            setup_ap_label(settings.wifi.setup_ap_state),
        );

        let mut text: String<24> = String::new();
        let _ = write!(text, "{}%", settings.brightness_percent);
        set_label_text(self.settings_widgets.brightness, text.as_str());

        set_label_text(
            self.settings_widgets.rotation,
            rotation_label(settings.display_rotation),
        );
        set_label_text(
            self.settings_widgets.speed_unit,
            speed_unit_label(settings.speed_unit),
        );
        set_label_text(
            self.settings_widgets.language,
            match settings.language {
                esp32_bms_gps::settings::Language::Chinese => "ZH",
                esp32_bms_gps::settings::Language::English => "EN",
            },
        );
        set_label_text(
            self.settings_widgets.bms,
            if settings.bms.bound_mac.is_some() {
                "BOUND"
            } else {
                "SCAN"
            },
        );
    }
}

unsafe fn init_display(width: i32, height: i32) -> *mut lv_display_t {
    let display = unsafe { lv_display_create(width, height) };
    assert!(!display.is_null(), "lv_display_create returned null");
    unsafe {
        lv_display_set_default(display);
        lv_display_set_color_format(display, lv_color_format_t_LV_COLOR_FORMAT_RGB565_SWAPPED);
        lv_display_set_buffers(
            display,
            ptr::addr_of_mut!(DRAW_BUF_1) as *mut c_void,
            ptr::addr_of_mut!(DRAW_BUF_2) as *mut c_void,
            LVGL_BUF_BYTES as u32,
            lv_display_render_mode_t_LV_DISPLAY_RENDER_MODE_PARTIAL,
        );
        lv_display_set_flush_cb(display, Some(flush_cb));
    }
    display
}

unsafe fn init_pointer(display: *mut lv_display_t) -> *mut lv_indev_t {
    let indev = unsafe { lv_indev_create() };
    assert!(!indev.is_null(), "lv_indev_create returned null");
    unsafe {
        lv_indev_set_type(indev, lv_indev_type_t_LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, display);
        lv_indev_set_read_cb(indev, Some(pointer_read_cb));
        lv_indev_set_gesture_min_distance(indev, PULL_DOWN_DELTA as u8);
    }
    indev
}

unsafe extern "C" fn flush_cb(disp: *mut lv_display_t, area: *const lv_area_t, px_map: *mut u8) {
    if disp.is_null() || area.is_null() || px_map.is_null() {
        if !disp.is_null() {
            unsafe { lv_display_flush_ready(disp) };
        }
        return;
    }

    let area = unsafe { &*area };
    if area.x1 < 0 || area.y1 < 0 || area.x2 < area.x1 || area.y2 < area.y1 {
        unsafe { lv_display_flush_ready(disp) };
        return;
    }

    let width = (area.x2 - area.x1 + 1) as u16;
    let height = (area.y2 - area.y1 + 1) as u16;
    let len = width as usize * height as usize * 2;
    let data = unsafe { slice::from_raw_parts(px_map as *const u8, len) };
    let ptr = DISPLAY_PTR.load(Ordering::Acquire);
    if !ptr.is_null() {
        let display = unsafe { &mut *(ptr as *mut St7789<'static>) };
        let _ = display.write_rgb565_window(area.x1 as u16, area.y1 as u16, width, height, data);
    }
    unsafe { lv_display_flush_ready(disp) };
}

unsafe extern "C" fn pointer_read_cb(_indev: *mut lv_indev_t, data: *mut lv_indev_data_t) {
    if data.is_null() {
        return;
    }

    let pressed = TOUCH_PRESSED.load(Ordering::Acquire);
    let xy = TOUCH_XY.load(Ordering::Relaxed);
    unsafe {
        (*data).point.x = ((xy >> 16) & 0xffff) as i32;
        (*data).point.y = (xy & 0xffff) as i32;
        (*data).state = if pressed {
            lv_indev_state_t_LV_INDEV_STATE_PRESSED
        } else {
            lv_indev_state_t_LV_INDEV_STATE_RELEASED
        };
        (*data).continue_reading = false;
    }
}

unsafe extern "C" fn action_event_cb(event: *mut lv_event_t) {
    if event.is_null() {
        return;
    }
    let action = unsafe { lv_event_get_user_data(event) } as usize as u8;
    PENDING_ACTION.store(action, Ordering::Release);
}

unsafe extern "C" fn dashboard_event_cb(event: *mut lv_event_t) {
    if event.is_null() {
        return;
    }
    let indev = unsafe { lv_event_get_indev(event) };
    if indev.is_null() {
        return;
    }
    let direction = unsafe { lv_indev_get_gesture_dir(indev) };
    if direction == lv_dir_t_LV_DIR_BOTTOM {
        post_action(UiAction::ShowQuickMenu);
    }
}

unsafe extern "C" fn set_y_exec(obj: *mut c_void, value: i32) {
    if !obj.is_null() {
        unsafe { lv_obj_set_y(obj as *mut lv_obj_t, value) };
    }
}

unsafe fn animate_y(obj: *mut lv_obj_t, from: i32, to: i32, duration: u32) {
    if obj.is_null() {
        return;
    }
    let mut anim = lv_anim_t::default();
    unsafe {
        lv_anim_init(&mut anim);
        lv_anim_set_var(&mut anim, obj as *mut c_void);
        lv_anim_set_exec_cb(&mut anim, Some(set_y_exec));
        lv_anim_set_values(&mut anim, from, to);
        lv_anim_set_duration(&mut anim, duration);
        lv_anim_set_path_cb(&mut anim, Some(lv_anim_path_ease_out));
        lv_anim_start(&anim);
    }
}

fn post_action(action: UiAction) {
    let code = action_code(action);
    if code != ACTION_NONE {
        PENDING_ACTION.store(code, Ordering::Release);
    }
}

fn action_code(action: UiAction) -> u8 {
    match action {
        UiAction::None => ACTION_NONE,
        UiAction::ShowDashboard => ACTION_SHOW_DASHBOARD,
        UiAction::ShowQuickMenu => ACTION_SHOW_QUICK_MENU,
        UiAction::ShowSettings => ACTION_SHOW_SETTINGS,
        UiAction::EnableWifiReprovisioning => ACTION_ENABLE_WIFI,
        UiAction::CycleBrightness => ACTION_BRIGHTNESS,
        UiAction::RotateDisplay => ACTION_ROTATE,
        UiAction::ToggleSpeedUnit => ACTION_SPEED_UNIT,
        UiAction::ToggleLanguage => ACTION_LANGUAGE,
        UiAction::StartBmsBind => ACTION_BMS_BIND,
        UiAction::RestoreDefaults => ACTION_RESTORE,
    }
}

fn code_to_action(code: u8) -> UiAction {
    match code {
        ACTION_SHOW_DASHBOARD => UiAction::ShowDashboard,
        ACTION_SHOW_QUICK_MENU => UiAction::ShowQuickMenu,
        ACTION_SHOW_SETTINGS => UiAction::ShowSettings,
        ACTION_ENABLE_WIFI => UiAction::EnableWifiReprovisioning,
        ACTION_BRIGHTNESS => UiAction::CycleBrightness,
        ACTION_ROTATE => UiAction::RotateDisplay,
        ACTION_SPEED_UNIT => UiAction::ToggleSpeedUnit,
        ACTION_LANGUAGE => UiAction::ToggleLanguage,
        ACTION_BMS_BIND => UiAction::StartBmsBind,
        ACTION_RESTORE => UiAction::RestoreDefaults,
        _ => UiAction::None,
    }
}

fn base_obj(obj: *mut lv_obj_t, x: i32, y: i32, w: i32, h: i32, bg: u32, radius: i32) {
    if obj.is_null() {
        return;
    }
    unsafe {
        lv_obj_set_pos(obj, x, y);
        lv_obj_set_size(obj, w, h);
        lv_obj_remove_flag(obj, lv_obj_flag_t_LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, lv_scrollbar_mode_t_LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_bg_color(obj, color(bg), 0);
        lv_obj_set_style_bg_opa(obj, _lv_opacity_level_t_LV_OPA_COVER as u8, 0);
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
    }
}

fn card(parent: *mut lv_obj_t, x: i32, y: i32, w: i32, h: i32, bg: u32) -> *mut lv_obj_t {
    let obj = unsafe { lv_obj_create(parent) };
    base_obj(obj, x, y, w, h, bg, 8);
    obj
}

fn action_button(
    parent: *mut lv_obj_t,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    bg: u32,
    action: UiAction,
) -> *mut lv_obj_t {
    let obj = unsafe { lv_button_create(parent) };
    base_obj(obj, x, y, w, h, bg, 8);
    unsafe {
        lv_obj_add_event_cb(
            obj,
            Some(action_event_cb),
            lv_event_code_t_LV_EVENT_CLICKED,
            action_code(action) as usize as *mut c_void,
        );
    }
    obj
}

fn circle_button(
    parent: *mut lv_obj_t,
    x: i32,
    y: i32,
    size: i32,
    bg: u32,
    action: UiAction,
) -> *mut lv_obj_t {
    let obj = action_button(parent, x, y, size, size, bg, action);
    unsafe {
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE as i32, 0);
        lv_obj_set_ext_click_area(obj, 10);
    }
    obj
}

fn label(
    parent: *mut lv_obj_t,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    font: *const lv_font_t,
) -> *mut lv_obj_t {
    let obj = unsafe { lv_label_create(parent) };
    base_obj(obj, x, y, w, h, 0x000000, 0);
    unsafe {
        lv_obj_set_style_bg_opa(obj, _lv_opacity_level_t_LV_OPA_TRANSP as u8, 0);
        lv_obj_set_style_text_color(obj, color(0xeaf2f7), 0);
        lv_obj_set_style_text_font(obj, font, 0);
        lv_label_set_long_mode(obj, lv_label_long_mode_t_LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text(obj, c"".as_ptr());
    }
    obj
}

fn symbol_label(parent: *mut lv_obj_t, symbol: *const u8) {
    let obj = label(parent, 0, 10, 62, 38, unsafe { &lv_font_montserrat_32 });
    unsafe {
        lv_obj_set_style_text_color(obj, color(0xffffff), 0);
        lv_obj_align(obj, lv_align_t_LV_ALIGN_CENTER, 0, 1);
        lv_label_set_text(obj, symbol as *const c_char);
    }
}

fn set_label_text(label: *mut lv_obj_t, text: &str) {
    if label.is_null() {
        return;
    }

    let mut buf = [0 as c_char; 72];
    for (index, byte) in text.bytes().take(buf.len() - 1).enumerate() {
        buf[index] = byte as c_char;
    }
    unsafe {
        lv_label_set_text(label, buf.as_ptr());
    }
}

fn color(hex: u32) -> lv_color_t {
    unsafe { lv_color_hex(hex) }
}

fn speed_unit_label(unit: SpeedUnit) -> &'static str {
    match unit {
        SpeedUnit::Kmh => "KM/H",
        SpeedUnit::Mph => "MPH",
    }
}

fn setup_ap_label(state: SetupApState) -> &'static str {
    match state {
        SetupApState::FirstBoot => "FIRST BOOT",
        SetupApState::Disabled => "OFF",
        SetupApState::Reprovisioning => "SETUP ON",
    }
}

fn rotation_label(rotation: DisplayRotation) -> &'static str {
    match rotation {
        DisplayRotation::Portrait => "PORTRAIT",
        DisplayRotation::Landscape => "LAND",
        DisplayRotation::InvertedPortrait => "INV PORT",
        DisplayRotation::InvertedLandscape => "INV LAND",
    }
}

fn wifi_label(state: WifiLinkState) -> &'static str {
    match state {
        WifiLinkState::SetupApOnly => "WIFI AP",
        WifiLinkState::StationConnecting => "WIFI LINK",
        WifiLinkState::StationConnected => "WIFI OK",
        WifiLinkState::Offline => "WIFI OFF",
    }
}

fn write_mv_deci(out: &mut String<48>, prefix: &str, value_mv: Option<u32>) {
    let _ = out.push_str(prefix);
    match value_mv {
        Some(value) => {
            let deci_v = value / 100;
            let _ = write!(out, "{}.{}V", deci_v / 10, deci_v % 10);
        }
        None => {
            let _ = out.push_str("--.-V");
        }
    }
}

fn write_deci_i16(out: &mut String<48>, prefix: &str, value: Option<i16>) {
    let _ = out.push_str(prefix);
    match value {
        Some(value) => {
            let magnitude = value.saturating_abs() as u16;
            if value < 0 {
                let _ = out.push('-');
            }
            let _ = write!(out, "{}.{}A", magnitude / 10, magnitude % 10);
        }
        None => {
            let _ = out.push_str("--.-A");
        }
    }
}

fn write_mv3_short(out: &mut String<48>, prefix: &str, value: Option<u16>) {
    let _ = out.push_str(prefix);
    match value {
        Some(value) => {
            let _ = write!(
                out,
                "{}.{}{}{}V",
                value / 1000,
                (value % 1000) / 100,
                (value % 100) / 10,
                value % 10
            );
        }
        None => {
            let _ = out.push_str("-.---V");
        }
    }
}
