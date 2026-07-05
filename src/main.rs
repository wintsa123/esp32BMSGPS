#![no_std]
#![no_main]

#[cfg(all(target_arch = "xtensa", feature = "net"))]
extern crate alloc;

mod assets;
mod battery_adc;
mod board;
mod build_config;
mod cjk_font;
mod display;
mod flash_settings;
mod lvgl_ui;
mod touch;
#[cfg(all(target_arch = "xtensa", feature = "wireless"))]
mod wireless_wifi;

use esp_backtrace as _;
use esp_hal::{
    delay::Delay,
    gpio::{Input, InputConfig, Level, Output, OutputConfig},
    main,
    rng::Rng,
    spi::{
        Mode,
        master::{Config as SpiConfig, Spi},
    },
    time::Rate,
    uart::{Config as UartConfig, Uart},
};
#[cfg(all(target_arch = "xtensa", feature = "wireless"))]
use esp_hal::{interrupt::software::SoftwareInterruptControl, ram, timer::timg::TimerGroup};
use esp32_bms_gps::{
    ant_bms::FrameAssembler,
    app_state::{AppState, WifiLinkState},
    bms_ble::{BmsBleCommand, BmsBleRuntime},
    gps_service::GpsService,
    provisioning::{
        generate_setup_ap_password, generate_setup_ap_ssid, setup_ap_password_matches_policy,
        setup_ap_ssid_matches_policy, wifi_qr_payload,
    },
    qr, runtime_effects,
    settings::{DeviceSettings, DisplayRotation, RawTouchPoint, ScreenPoint, TouchCalibration},
    touch_ui::{self, UiAction, UiScreen},
};

esp_bootloader_esp_idf::esp_app_desc!();

#[allow(dead_code)]
const DASHBOARD_BMS_NOTICE_MS: u32 = 1_500;

#[main]
fn main() -> ! {
    let peripherals = esp_hal::init(esp_hal::Config::default());
    esp_println::logger::init_logger_from_env();
    esp_println::println!("[boot] logger initialized");
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    {
        init_wireless_heap();
        let timg0 = TimerGroup::new(peripherals.TIMG0);
        let software_interrupt = SoftwareInterruptControl::new(peripherals.SW_INTERRUPT);
        esp_rtos::start(timg0.timer0, software_interrupt.software_interrupt0);
    }

    let mut settings_store = flash_settings::DeviceSettingsStore::new(peripherals.FLASH);
    let mut tft_backlight = Output::new(peripherals.GPIO21, Level::Low, OutputConfig::default());
    let mut status_r = Output::new(peripherals.GPIO17, Level::Low, OutputConfig::default());
    let mut status_g = Output::new(peripherals.GPIO22, Level::Low, OutputConfig::default());
    let mut status_b = Output::new(peripherals.GPIO16, Level::Low, OutputConfig::default());
    let tft_cs = Output::new(peripherals.GPIO15, Level::High, OutputConfig::default());
    let tft_dc = Output::new(peripherals.GPIO2, Level::Low, OutputConfig::default());
    let touch_clk = Output::new(peripherals.GPIO25, Level::Low, OutputConfig::default());
    let touch_mosi = Output::new(peripherals.GPIO32, Level::Low, OutputConfig::default());
    let touch_miso = Input::new(peripherals.GPIO39, InputConfig::default());
    let touch_cs = Output::new(peripherals.GPIO33, Level::High, OutputConfig::default());
    let touch_irq = Input::new(peripherals.GPIO36, InputConfig::default());
    let delay = Delay::new();
    let mut battery_adc = battery_adc::BatteryAdc::new(peripherals.ADC1, peripherals.GPIO34);
    let mut gps_uart = Uart::new(
        peripherals.UART0,
        UartConfig::default().with_baudrate(board::gps::BAUD),
    )
    .map(|uart| uart.with_rx(peripherals.GPIO3).with_tx(peripherals.GPIO1))
    .ok();
    let mut gps_service = GpsService::new();
    let mut settings = settings_store
        .load()
        .ok()
        .flatten()
        .map(|stored| stored.settings)
        .unwrap_or_default();
    settings.touch_calibration = settings
        .touch_calibration
        .or(board::touch::FACTORY_CALIBRATION);
    if ensure_first_boot_provisioning(&mut settings) {
        let _ = settings_store.save(&settings);
    }
    let mut app_state = AppState::new(settings);
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    let mut wifi_peripheral = Some(peripherals.WIFI);
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    let mut wifi_runtime = None;

    let _build_marker = (
        build_config::FIRMWARE_NAME,
        build_config::FIRMWARE_VERSION,
        build_config::ota_manifest_url(),
        assets::INDEX_HTML_LEN,
    );

    let spi = match Spi::new(
        peripherals.SPI2,
        SpiConfig::default()
            .with_frequency(Rate::from_mhz(board::tft::SPI_FREQUENCY_MHZ))
            .with_mode(Mode::_0),
    ) {
        Ok(spi) => spi
            .with_sck(peripherals.GPIO14)
            .with_mosi(peripherals.GPIO13)
            .with_miso(peripherals.GPIO12),
        Err(_) => fast_blink(tft_backlight, status_r, delay),
    };

    status_b.set_high();
    let mut display = display::St7789::new(spi, tft_cs, tft_dc, delay);
    display.wait_for_power();
    let detected_tft_controller = match display.read_id() {
        Ok(id) => {
            esp_println::println!("[tft] pre-init RDDID={:02x?}", id);
            display::controller_from_rddid(id)
        }
        Err(error) => {
            esp_println::println!("[tft] pre-init RDDID failed: {:?}", error);
            None
        }
    };
    let mut tft_controller = detected_tft_controller.unwrap_or(board::tft::CONTROLLER);
    let tft_auto_probe = board::tft::AUTO_PROBE_ON_RDDID_MISS && detected_tft_controller.is_none();
    let mut active_display_rotation = app_state.settings.display_rotation;
    let mut tft_rotation = active_display_rotation;
    let mut tft_invert_colors = board::tft::INVERT_COLORS;
    esp_println::println!(
        "[tft] controller={:?} rotation={:?} invert={} auto_probe={}",
        tft_controller,
        tft_rotation,
        tft_invert_colors,
        tft_auto_probe
    );
    match display.init(tft_controller, tft_rotation) {
        Ok(()) => esp_println::println!("[tft] init ok"),
        Err(error) => {
            esp_println::println!("[tft] init failed: {:?}", error);
            fast_blink(tft_backlight, status_r, delay);
        }
    }
    match display.read_id() {
        Ok(id) => esp_println::println!("[tft] RDDID={:02x?}", id),
        Err(error) => esp_println::println!("[tft] RDDID failed: {:?}", error),
    }
    tft_backlight.set_high();
    match display.write_raw_screen_color(display::RED) {
        Ok(()) => esp_println::println!("[tft] raw RAMWR red drawn"),
        Err(error) => esp_println::println!("[tft] raw RAMWR red failed: {:?}", error),
    }
    Delay::new().delay_millis(400);
    let _ = display.clear(display::BLACK);
    status_b.set_low();
    status_g.set_high();
    if board::tft::BOOT_DIAGNOSTIC_MS > 0 {
        match display.draw_boot_diagnostics() {
            Ok(()) => esp_println::println!("[tft] boot diagnostics drawn"),
            Err(error) => esp_println::println!("[tft] boot diagnostics failed: {:?}", error),
        }
        Delay::new().delay_millis(board::tft::BOOT_DIAGNOSTIC_MS);
    }
    match board::tft::BOOT_SETTINGS_PREVIEW_MS {
        0 => {}
        preview_ms => {
            match display.draw_settings_menu(&app_state.settings) {
                Ok(()) => esp_println::println!("[tft] settings preview drawn"),
                Err(error) => esp_println::println!("[tft] settings preview failed: {:?}", error),
            }
            Delay::new().delay_millis(preview_ms);
        }
    }

    let mut touch = touch::Xpt2046::new(
        touch_clk, touch_mosi, touch_miso, touch_cs, touch_irq, delay,
    );
    if board::touch::CALIBRATE_ON_BOOT || app_state.settings.touch_calibration.is_none() {
        match run_touch_calibration(&mut display, &mut touch, active_display_rotation) {
            Ok(calibration) => {
                app_state.settings.touch_calibration = Some(calibration);
                let _ = settings_store.save(&app_state.settings);
            }
            Err(_) => fast_blink(tft_backlight, status_r, delay),
        }
    }
    let mut ui = lvgl_ui::LvglUi::new(&mut display, active_display_rotation);
    ui.update(&app_state);
    let mut bms_ble = BmsBleRuntime::new();
    let mut bms_frame_assembler = FrameAssembler::new();
    let mut pending_bms_command = BmsBleCommand::None;
    let mut tft_auto_probe_tick = 0_u8;
    let mut tft_auto_probe_step = 0_u8;
    let mut touch_log_tick = 0_u8;
    let mut touch_diag_tick = 0_u8;
    let mut status_tick = 0_u8;
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    apply_target_wifi_config(&mut app_state, &mut wifi_peripheral, &mut wifi_runtime);

    let mut battery_sample_tick = 0_u16;
    loop {
        tft_backlight.set_high();
        #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
        {
            let runtime_actions = if let Some(runtime) = wifi_runtime.as_mut() {
                runtime.poll(&mut app_state)
            } else {
                None
            };
            if let Some(actions) = runtime_actions {
                let mut action_context = UiActionContext {
                    settings_store: &mut settings_store,
                    bms_ble: &mut bms_ble,
                    bms_frame_assembler: &mut bms_frame_assembler,
                    pending_bms_command: &mut pending_bms_command,
                    wifi_peripheral: &mut wifi_peripheral,
                    wifi_runtime: &mut wifi_runtime,
                };
                handle_runtime_actions(actions, &mut app_state, &mut action_context);
            }
        }
        poll_battery_adc(&mut battery_adc, &mut battery_sample_tick, &mut app_state);
        poll_gps_uart(&mut gps_uart, &mut gps_service, &mut app_state);
        if tft_auto_probe {
            tft_auto_probe_tick = tft_auto_probe_tick.saturating_add(1);
            if tft_auto_probe_tick >= 80 {
                tft_auto_probe_tick = 0;
                tft_auto_probe_step = tft_auto_probe_step.wrapping_add(1);
                let (probe_controller, probe_rotation, probe_invert_colors) =
                    tft_probe_target(tft_auto_probe_step);
                tft_controller = probe_controller;
                tft_rotation = probe_rotation;
                active_display_rotation = probe_rotation;
                tft_invert_colors = probe_invert_colors;
                esp_println::println!(
                    "[tft] auto probe controller={:?} rotation={:?} invert={}",
                    tft_controller,
                    tft_rotation,
                    tft_invert_colors
                );
                if display
                    .init_with_inversion(tft_controller, tft_rotation, tft_invert_colors)
                    .is_ok()
                {
                    let _ = display.write_raw_screen_color(display::RED);
                    Delay::new().delay_millis(300);
                    let _ = display.draw_boot_diagnostics();
                    Delay::new().delay_millis(400);
                    ui.update(&app_state);
                }
            }
        }
        touch_diag_tick = touch_diag_tick.wrapping_add(1);
        if touch_diag_tick >= 40 {
            touch_diag_tick = 0;
            let sample = touch.diagnostic_sample();
            esp_println::println!(
                "[touch] diag raw=({}, {}) z1={} z2={} irq_low={} pressure_active={} raw_active={}",
                sample.raw.x,
                sample.raw.y,
                sample.z1,
                sample.z2,
                sample.irq_low,
                sample.pressure_active,
                sample.raw_active
            );
        }
        let touch_point = if let Some(calibration) = app_state.settings.touch_calibration {
            if let Some(raw) = touch.read_raw_average() {
                let point = calibration.map_for_rotation(raw, active_display_rotation);
                if touch_log_tick == 0 {
                    esp_println::println!(
                        "[touch] raw=({}, {}) irq_low={} point=({}, {}) rotation={:?}",
                        raw.x,
                        raw.y,
                        touch.is_touched(),
                        point.x,
                        point.y,
                        active_display_rotation
                    );
                }
                touch_log_tick = touch_log_tick.wrapping_add(1);
                if touch_log_tick >= 4 {
                    touch_log_tick = 0;
                }
                Some(point)
            } else {
                None
            }
        } else {
            None
        };
        ui.feed_touch(touch_point);
        ui.update(&app_state);
        ui.tick();

        let action = ui.take_action();
        if !matches!(action, UiAction::None) {
            let previous_rotation = active_display_rotation;
            let mut action_context = UiActionContext {
                settings_store: &mut settings_store,
                bms_ble: &mut bms_ble,
                bms_frame_assembler: &mut bms_frame_assembler,
                pending_bms_command: &mut pending_bms_command,
                #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
                wifi_peripheral: &mut wifi_peripheral,
                #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
                wifi_runtime: &mut wifi_runtime,
            };
            if handle_ui_action(action, &mut app_state, &mut display, &mut action_context).is_err()
            {
                fast_blink(tft_backlight, status_r, delay);
            }
            active_display_rotation = app_state.settings.display_rotation;
            if active_display_rotation != previous_rotation {
                if !matches!(action, UiAction::RotateDisplay)
                    && display.set_rotation(active_display_rotation).is_err()
                {
                    fast_blink(tft_backlight, status_r, delay);
                }
                ui.set_rotation(active_display_rotation, &app_state);
            }
            ui.apply_action(action, &app_state);
            ui.tick();
        }
        status_tick = status_tick.wrapping_add(1);
        if status_tick >= 10 {
            status_tick = 0;
            status_r.toggle();
        }
        delay.delay_millis(25);
    }
}

struct UiActionContext<'a, 'd> {
    settings_store: &'a mut flash_settings::DeviceSettingsStore<'d>,
    bms_ble: &'a mut BmsBleRuntime,
    bms_frame_assembler: &'a mut FrameAssembler,
    pending_bms_command: &'a mut BmsBleCommand,
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    wifi_peripheral: &'a mut Option<esp_hal::peripherals::WIFI<'static>>,
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    wifi_runtime: &'a mut Option<wireless_wifi::WifiRuntime<'static>>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(dead_code)]
struct TouchGesture {
    start: ScreenPoint,
    end: ScreenPoint,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(dead_code)]
enum ScreenRefresh {
    None,
    Full,
    SettingsRow(u16),
}

#[allow(dead_code)]
fn refresh_for_action(action: UiAction) -> ScreenRefresh {
    match action {
        UiAction::None | UiAction::StartBmsBind => ScreenRefresh::None,
        UiAction::ShowDashboard
        | UiAction::ShowQuickMenu
        | UiAction::ShowSettings
        | UiAction::RotateDisplay
        | UiAction::RestoreDefaults => ScreenRefresh::Full,
        UiAction::EnableWifiReprovisioning => ScreenRefresh::SettingsRow(0),
        UiAction::CycleBrightness => ScreenRefresh::SettingsRow(1),
        UiAction::ToggleSpeedUnit => ScreenRefresh::SettingsRow(3),
        UiAction::ToggleLanguage => ScreenRefresh::SettingsRow(4),
    }
}

#[allow(dead_code)]
fn read_touch_gesture(
    touch: &mut touch::Xpt2046<'_>,
    calibration: TouchCalibration,
    rotation: DisplayRotation,
    initial_raw: RawTouchPoint,
    delay: &Delay,
) -> TouchGesture {
    let start = calibration.map_for_rotation(initial_raw, rotation);
    let mut gesture = TouchGesture { start, end: start };
    let mut samples = 0_u8;

    while samples < 16 {
        if let Some(raw) = touch.read_raw_average() {
            gesture.end = calibration.map_for_rotation(raw, rotation);
            samples = samples.saturating_add(1);
            delay.delay_millis(8);
        } else {
            break;
        }
    }

    touch.wait_for_release();
    gesture
}

#[cfg(all(target_arch = "xtensa", feature = "wireless"))]
fn init_wireless_heap() {
    esp_alloc::heap_allocator!(#[ram(reclaimed)] size: 64 * 1024);
    esp_alloc::heap_allocator!(size: 36 * 1024);
}

#[cfg(all(target_arch = "xtensa", feature = "wireless"))]
fn apply_target_wifi_config(
    app_state: &mut AppState,
    wifi_peripheral: &mut Option<esp_hal::peripherals::WIFI<'static>>,
    wifi_runtime: &mut Option<wireless_wifi::WifiRuntime<'static>>,
) {
    let config = esp32_bms_gps::wifi_control::desired_runtime_config(&app_state.settings);

    if wireless_wifi::esp_radio_config(config).is_none() {
        esp_println::println!("[wifi] desired mode is off; no Wi-Fi runtime started");
        return;
    }

    if let Some(runtime) = wifi_runtime.as_mut() {
        esp_println::println!("[wifi] applying config to existing runtime");
        if runtime.apply_config(config).is_err() {
            esp_println::println!("[wifi] existing runtime apply failed; app state -> offline");
            app_state.wifi = WifiLinkState::Offline;
        }
        return;
    }

    let Some(wifi) = wifi_peripheral.take() else {
        esp_println::println!("[wifi] WIFI peripheral unavailable; app state -> offline");
        app_state.wifi = WifiLinkState::Offline;
        return;
    };

    match wireless_wifi::WifiRuntime::start(wifi, config) {
        Ok(runtime) => {
            if runtime.is_some() {
                esp_println::println!("[wifi] runtime started");
            } else {
                esp_println::println!("[wifi] runtime start returned no controller");
            }
            *wifi_runtime = runtime;
        }
        Err(_) => {
            esp_println::println!("[wifi] runtime start failed; app state -> offline");
            app_state.wifi = WifiLinkState::Offline;
        }
    }
}

fn poll_battery_adc(
    battery_adc: &mut battery_adc::BatteryAdc<'_>,
    sample_tick: &mut u16,
    app_state: &mut AppState,
) {
    *sample_tick = sample_tick.saturating_add(1);
    if *sample_tick < battery_adc::DEFAULT_SAMPLE_PERIOD_TICKS as u16 * 10 {
        return;
    }

    *sample_tick = 0;
    let _ = battery_adc.sample_into_state(app_state, battery_adc::DEFAULT_SENSE_CONFIG);
}

fn poll_gps_uart(
    gps_uart: &mut Option<Uart<'_, esp_hal::Blocking>>,
    gps_service: &mut GpsService,
    app_state: &mut AppState,
) {
    let Some(uart) = gps_uart.as_mut() else {
        return;
    };
    if !uart.read_ready() {
        return;
    }

    let mut bytes = [0_u8; 96];
    if let Ok(len) = uart.read_buffered(&mut bytes) {
        gps_service.feed(&bytes[..len], app_state);
    }
}

#[cfg(all(target_arch = "xtensa", feature = "wireless"))]
fn handle_runtime_actions(
    actions: runtime_effects::RuntimeActions,
    app_state: &mut AppState,
    context: &mut UiActionContext<'_, '_>,
) {
    if actions.persist_settings {
        let _ = context.settings_store.save(&app_state.settings);
    }
    if actions.reconnect_wifi {
        apply_target_wifi_config(app_state, context.wifi_peripheral, context.wifi_runtime);
    }
    if actions.start_bms_scan {
        context.bms_frame_assembler.reset();
        app_state.clear_bms_scan_candidates();
        *context.pending_bms_command = context.bms_ble.start_scan();
    }
}

fn ensure_first_boot_provisioning(settings: &mut DeviceSettings) -> bool {
    let mut changed = false;
    let rng = Rng::new();

    if !setup_ap_ssid_matches_policy(settings.wifi.setup_ap_ssid) {
        let mut random = [0_u8; 16];
        rng.read(&mut random);
        if let Ok(ssid) = generate_setup_ap_ssid(&random) {
            settings.wifi.setup_ap_ssid = ssid;
            changed = true;
        }
    }

    if !settings.wifi.setup_ap_password_saved
        || !setup_ap_password_matches_policy(settings.wifi.setup_ap_password)
    {
        let mut random = [0_u8; 16];
        rng.read(&mut random);
        if let Ok(password) = generate_setup_ap_password(&random) {
            settings.wifi.setup_ap_password = password;
            settings.wifi.setup_ap_password_saved = true;
            changed = true;
        }
    }

    changed
}

#[allow(dead_code)]
fn draw_screen(
    display: &mut display::St7789<'_>,
    app_state: &AppState,
    screen: UiScreen,
) -> Result<(), esp_hal::spi::Error> {
    match screen {
        UiScreen::Dashboard => {
            if board::tft::SHOW_SETUP_QR_ON_DASHBOARD
                && app_state.settings.wifi.setup_ap_state.enabled()
                && matches!(app_state.wifi, WifiLinkState::SetupApOnly)
            {
                draw_setup_ap_qr_screen(display, &app_state.settings)
            } else {
                display.draw_dashboard(
                    &app_state.dashboard_snapshot(),
                    build_config::FIRMWARE_VERSION,
                )
            }
        }
        UiScreen::QuickMenu => {
            display.draw_dashboard(
                &app_state.dashboard_snapshot(),
                build_config::FIRMWARE_VERSION,
            )?;
            display.draw_quick_menu()
        }
        UiScreen::Settings => display.draw_settings_menu(&app_state.settings),
    }
}

#[allow(dead_code)]
fn draw_quick_menu_opening(
    display: &mut display::St7789<'_>,
    app_state: &AppState,
    delay: &Delay,
) -> Result<(), esp_hal::spi::Error> {
    display.draw_dashboard(
        &app_state.dashboard_snapshot(),
        build_config::FIRMWARE_VERSION,
    )?;
    for panel_height in [24_u16, 48, 72, 96, touch_ui::QUICK_MENU_PANEL_HEIGHT] {
        display.draw_quick_menu_panel(panel_height)?;
        delay.delay_millis(18);
    }
    Ok(())
}

#[allow(dead_code)]
fn draw_quick_menu_closing(
    display: &mut display::St7789<'_>,
    app_state: &AppState,
    delay: &Delay,
) -> Result<(), esp_hal::spi::Error> {
    let (width, _) = app_state.settings.display_rotation.logical_size();
    let mut previous_height = touch_ui::QUICK_MENU_PANEL_HEIGHT;

    for panel_height in [96_u16, 72, 48, 24, 0] {
        if previous_height > panel_height {
            display.fill_rect(
                0,
                panel_height,
                width,
                previous_height - panel_height,
                display::BLACK,
            )?;
        }
        if panel_height > 0 {
            display.draw_quick_menu_panel(panel_height)?;
        }
        previous_height = panel_height;
        delay.delay_millis(18);
    }

    display.draw_dashboard(
        &app_state.dashboard_snapshot(),
        build_config::FIRMWARE_VERSION,
    )
}

#[allow(dead_code)]
fn show_dashboard_bms_notice_if_needed(
    display: &mut display::St7789<'_>,
    app_state: &AppState,
    delay: &Delay,
) -> Result<(), esp_hal::spi::Error> {
    if app_state.dashboard_snapshot().bms_online {
        return Ok(());
    }

    esp_println::println!("[ui] dashboard notice: bms_offline");
    display.draw_dashboard_notice("BMS OFFLINE", "CHECK BINDING")?;
    delay.delay_millis(DASHBOARD_BMS_NOTICE_MS);
    draw_screen(display, app_state, UiScreen::Dashboard)
}

#[allow(dead_code)]
fn draw_setup_ap_qr_screen(
    display: &mut display::St7789<'_>,
    settings: &DeviceSettings,
) -> Result<(), esp_hal::spi::Error> {
    let Ok(payload) = wifi_qr_payload(settings.wifi.setup_ap_ssid, settings.wifi.setup_ap_password)
    else {
        return display.draw_dashboard(
            &AppState::new(*settings).dashboard_snapshot(),
            build_config::FIRMWARE_VERSION,
        );
    };
    let Ok(qr) = qr::encode_text(payload.as_str()) else {
        return display.draw_dashboard(
            &AppState::new(*settings).dashboard_snapshot(),
            build_config::FIRMWARE_VERSION,
        );
    };

    display.draw_setup_ap_qr(
        &qr,
        settings.wifi.setup_ap_ssid.as_str(),
        settings.wifi.setup_ap_password.as_str(),
    )
}

fn handle_ui_action(
    action: UiAction,
    app_state: &mut AppState,
    display: &mut display::St7789<'_>,
    context: &mut UiActionContext<'_, '_>,
) -> Result<(), esp_hal::spi::Error> {
    let changed = touch_ui::apply_action(&mut app_state.settings, action);
    let runtime_actions = runtime_effects::apply_touch_action(action);

    if matches!(action, UiAction::EnableWifiReprovisioning) {
        app_state.enable_reprovisioning();
    }
    if matches!(action, UiAction::RotateDisplay) {
        display.set_rotation(app_state.settings.display_rotation)?;
    }
    if matches!(action, UiAction::RestoreDefaults) {
        let _ = ensure_first_boot_provisioning(&mut app_state.settings);
    }
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    if matches!(
        action,
        UiAction::EnableWifiReprovisioning | UiAction::RestoreDefaults
    ) {
        apply_target_wifi_config(app_state, context.wifi_peripheral, context.wifi_runtime);
    }
    if runtime_actions.start_bms_scan {
        context.bms_frame_assembler.reset();
        app_state.clear_bms_scan_candidates();
        *context.pending_bms_command = context.bms_ble.start_scan();
    }

    if changed {
        let _ = context.settings_store.save(&app_state.settings);
    }

    Ok(())
}

fn fast_blink(mut tft_backlight: Output<'_>, mut status_led: Output<'_>, delay: Delay) -> ! {
    loop {
        tft_backlight.set_high();
        status_led.toggle();
        delay.delay_millis(100);
    }
}

fn alternate_tft_controller(controller: board::tft::Controller) -> board::tft::Controller {
    match controller {
        board::tft::Controller::St7789 => board::tft::Controller::Ili9341,
        board::tft::Controller::Ili9341 => board::tft::Controller::St7789,
    }
}

fn tft_probe_target(step: u8) -> (board::tft::Controller, DisplayRotation, bool) {
    let controller = if step & 0b1000 == 0 {
        board::tft::CONTROLLER
    } else {
        alternate_tft_controller(board::tft::CONTROLLER)
    };
    let rotation = match step & 0b11 {
        0 => DisplayRotation::Portrait,
        1 => DisplayRotation::Landscape,
        2 => DisplayRotation::InvertedPortrait,
        _ => DisplayRotation::InvertedLandscape,
    };
    let invert_colors = step & 0b100 != 0;
    (controller, rotation, invert_colors)
}

fn run_touch_calibration(
    display: &mut display::St7789<'_>,
    touch: &mut touch::Xpt2046<'_>,
    rotation: DisplayRotation,
) -> Result<TouchCalibration, esp_hal::spi::Error> {
    let (width, height) = rotation.logical_size();
    let margin = 24;
    let targets = [
        ScreenPoint {
            x: margin,
            y: margin,
        },
        ScreenPoint {
            x: width - margin - 1,
            y: margin,
        },
        ScreenPoint {
            x: width - margin - 1,
            y: height - margin - 1,
        },
        ScreenPoint {
            x: margin,
            y: height - margin - 1,
        },
    ];
    let mut raw = [RawTouchPoint { x: 0, y: 0 }; 4];

    for (index, target) in targets.iter().copied().enumerate() {
        display.draw_calibration_target(target)?;
        raw[index] = touch.wait_for_touch();
        display.draw_touch_feedback(target)?;
        touch.wait_for_release();
    }

    Ok(TouchCalibration::from_four_points(
        raw[0], raw[1], raw[2], raw[3], width, height,
    ))
}
