#![no_std]
#![no_main]

#[cfg(all(target_arch = "xtensa", feature = "net"))]
extern crate alloc;

mod assets;
mod battery_adc;
mod board;
mod build_config;
mod display;
mod flash_settings;
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
    touch_ui::{self, TouchUi, UiAction, UiScreen},
};

esp_bootloader_esp_idf::esp_app_desc!();

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
    let mut tft_backlight = Output::new(
        peripherals.GPIO21,
        board::tft::BACKLIGHT_ON_LEVEL,
        OutputConfig::default(),
    );
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
    #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
    apply_target_wifi_config(&mut app_state, &mut wifi_peripheral, &mut wifi_runtime);

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
            .with_mosi(peripherals.GPIO13),
        Err(_) => fast_blink(tft_backlight, status_r, delay),
    };

    status_b.set_high();
    let mut display = display::St7789::new(spi, tft_cs, tft_dc, delay);
    if display.init(app_state.settings.display_rotation).is_err() {
        fast_blink(tft_backlight, status_r, delay);
    }
    if display.draw_color_bars().is_err() {
        fast_blink(tft_backlight, status_r, delay);
    }
    status_b.set_low();
    status_g.set_high();

    let mut touch = touch::Xpt2046::new(
        touch_clk, touch_mosi, touch_miso, touch_cs, touch_irq, delay,
    );
    if board::touch::CALIBRATE_ON_BOOT || app_state.settings.touch_calibration.is_none() {
        match run_touch_calibration(
            &mut display,
            &mut touch,
            app_state.settings.display_rotation,
        ) {
            Ok(calibration) => {
                app_state.settings.touch_calibration = Some(calibration);
                let _ = settings_store.save(&app_state.settings);
            }
            Err(_) => fast_blink(tft_backlight, status_r, delay),
        }
    }
    let mut ui = TouchUi::new();
    let mut bms_ble = BmsBleRuntime::new();
    let mut bms_frame_assembler = FrameAssembler::new();
    let mut pending_bms_command = BmsBleCommand::None;
    if draw_screen(&mut display, &app_state, ui.screen).is_err() {
        fast_blink(tft_backlight, status_r, delay);
    }

    let mut battery_sample_tick = 0_u8;
    loop {
        tft_backlight.set_high();
        #[cfg(all(target_arch = "xtensa", feature = "wireless"))]
        if let Some(runtime) = wifi_runtime.as_mut() {
            runtime.poll();
        }
        poll_battery_adc(&mut battery_adc, &mut battery_sample_tick, &mut app_state);
        poll_gps_uart(&mut gps_uart, &mut gps_service, &mut app_state);
        if let Some(calibration) = app_state.settings.touch_calibration
            && let Some(raw) = touch.read_raw_average()
        {
            let point = calibration.map(raw);
            let (_, screen_height) = app_state.settings.display_rotation.logical_size();
            let action = ui.handle_tap(point, screen_height);
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
            let _ = draw_screen(&mut display, &app_state, ui.screen);
            touch.wait_for_release();
        }
        status_r.toggle();
        delay.delay_millis(250);
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
    sample_tick: &mut u8,
    app_state: &mut AppState,
) {
    *sample_tick = sample_tick.saturating_add(1);
    if *sample_tick < battery_adc::DEFAULT_SAMPLE_PERIOD_TICKS {
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

fn draw_screen(
    display: &mut display::St7789<'_>,
    app_state: &AppState,
    screen: UiScreen,
) -> Result<(), esp_hal::spi::Error> {
    match screen {
        UiScreen::Dashboard => {
            if app_state.settings.wifi.setup_ap_state.enabled()
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
        UiScreen::Settings => display.draw_settings_menu(&app_state.settings),
    }
}

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
