#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bms_lvgl_ui.h"
#include "esp_bms_speed_dashboard.h"
#include "lvgl.h"

typedef enum {
    HOST_COMMAND_NONE = 0,
    HOST_COMMAND_SPEED_UP,
    HOST_COMMAND_SPEED_DOWN,
    HOST_COMMAND_PAGE_BATTERY,
    HOST_COMMAND_PAGE_CONTROLLER,
    HOST_COMMAND_PAGE_GPS,
    HOST_COMMAND_PAGE_CAST,
    HOST_COMMAND_TOGGLE_GPS_FIX,
    HOST_COMMAND_TOGGLE_BMS,
    HOST_COMMAND_TOGGLE_CONTROLLER,
    HOST_COMMAND_TOGGLE_UNIT,
    HOST_COMMAND_TOGGLE_CONSUMPTION,
    HOST_COMMAND_ROTATE,
    HOST_COMMAND_QUIT,
} host_command_t;

typedef struct {
    lv_display_t *display;
    esp_bms_dashboard_snapshot_t snapshot;
    uint16_t speed_kmh_deci;
    uint16_t controller_speed_kmh_deci;
    int32_t metric_consumption_deci_wh_per_km;
    host_command_t pending_command;
    bool running;
    bool sdl_quit_seen;
} host_app_t;

static void snapshot_flag_set(esp_bms_dashboard_snapshot_t *snapshot, uint32_t flag, bool enabled)
{
    esp_bms_dashboard_snapshot_flag_set(snapshot, flag, enabled);
}

static bool snapshot_flag_get(const esp_bms_dashboard_snapshot_t *snapshot, uint32_t flag)
{
    return esp_bms_dashboard_snapshot_flag_get(snapshot, flag);
}

static uint16_t display_speed_deci(uint16_t kmh_deci, esp_bms_speed_unit_t unit)
{
    return unit == ESP_BMS_SPEED_UNIT_MPH
               ? (uint16_t)(((uint32_t)kmh_deci * 621371U) / 1000000U)
               : kmh_deci;
}

static void refresh_speed_snapshot(host_app_t *app)
{
    esp_bms_dashboard_snapshot_t *snapshot = &app->snapshot;
    const bool controller_online =
        snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE);
    snapshot->active_speed_source =
        snapshot->speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER && controller_online
            ? ESP_BMS_SPEED_SOURCE_CONTROLLER
            : ESP_BMS_SPEED_SOURCE_GPS;
    const bool speed_valid = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                                 ? snapshot_flag_get(
                                       snapshot,
                                       ESP_BMS_DASHBOARD_FLAG_CONTROLLER_SPEED_VALID)
                                 : snapshot_flag_get(snapshot,
                                                     ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID);
    snapshot_flag_set(snapshot, ESP_BMS_DASHBOARD_FLAG_SPEED_VALID, speed_valid);
    const uint16_t active_speed_kmh_deci =
        snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
            ? app->controller_speed_kmh_deci
            : app->speed_kmh_deci;
    snapshot->speed_deci_units = speed_valid
                                     ? display_speed_deci(active_speed_kmh_deci,
                                                          snapshot->speed_unit)
                                     : 0U;
    snapshot->average_speed_deci_units = display_speed_deci(720U, snapshot->speed_unit);
    app->snapshot.controller_speed_deci_units =
        display_speed_deci(app->controller_speed_kmh_deci, app->snapshot.speed_unit);
    snapshot->average_consumption_deci_wh_per_distance = 0;
    if (snapshot->average_consumption_valid) {
        snapshot->average_consumption_deci_wh_per_distance =
            snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                ? (int32_t)(((int64_t)app->metric_consumption_deci_wh_per_km *
                             INT64_C(1609344) +
                             INT64_C(500000)) /
                            INT64_C(1000000))
                : app->metric_consumption_deci_wh_per_km;
    }
    snapshot->remaining_range_valid = esp_bms_remaining_range_km(
        snapshot->preset_range_km,
        snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_SOC_VALID),
        snapshot->soc_percent,
        snapshot->average_consumption_valid &&
            snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_PACK_VOLTAGE_VALID) &&
            snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CAPACITY_REMAINING_VALID),
        snapshot->pack_voltage_mv,
        snapshot->capacity_remaining_mah,
        app->metric_consumption_deci_wh_per_km,
        &snapshot->remaining_range_km);
}

static void init_snapshot(host_app_t *app)
{
    esp_bms_dashboard_snapshot_t *snapshot = &app->snapshot;
    memset(snapshot, 0, sizeof(*snapshot));

    snapshot->flags = ESP_BMS_DASHBOARD_FLAG_SPEED_VALID |
                      ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID |
                      ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE |
                      ESP_BMS_DASHBOARD_FLAG_PACK_VOLTAGE_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CURRENT_VALID |
                      ESP_BMS_DASHBOARD_FLAG_SOC_VALID |
                      ESP_BMS_DASHBOARD_FLAG_MIN_CELL_VALID |
                      ESP_BMS_DASHBOARD_FLAG_AVERAGE_CELL_VALID |
                      ESP_BMS_DASHBOARD_FLAG_MAX_CELL_VALID |
                      ESP_BMS_DASHBOARD_FLAG_DELTA_CELL_VALID |
                      ESP_BMS_DASHBOARD_FLAG_TOTAL_CAPACITY_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CAPACITY_REMAINING_VALID |
                      ESP_BMS_DASHBOARD_FLAG_LOCAL_BATTERY_VALID |
                      ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ENABLED |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_CONNECTION_ENABLED |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_SPEED_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_RPM_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_GEAR_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_POWER_VALID |
                      ESP_BMS_DASHBOARD_FLAG_CONTROLLER_TEMP_VALID |
                      ESP_BMS_DASHBOARD_FLAG_MOTOR_TEMP_VALID;
    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(snapshot, index, true);
        snapshot->bms_temperature_celsius[index] = (int16_t)(24 + index);
    }

    app->speed_kmh_deci = 880U;
    app->controller_speed_kmh_deci = 860U;
    app->metric_consumption_deci_wh_per_km = 238;
    snapshot->gps_sentences_seen = 1234U;
    snapshot->uptime_seconds = 3661U;
    snapshot->pack_voltage_mv = 72800U;
    snapshot->total_capacity_mah = 140000U;
    snapshot->capacity_remaining_mah = 104600U;
    snapshot->local_battery_mv = 3920U;
    snapshot->speed_unit = ESP_BMS_SPEED_UNIT_KMH;
    snapshot->speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    snapshot->active_speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    snapshot->speed_dashboard_style = ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE;
    snapshot->average_speed_valid = true;
    snapshot->average_consumption_valid = true;
    snapshot->preset_range_km = ESP_BMS_PRESET_RANGE_DEFAULT_KM;
    snapshot->current_deci_amps = 126;
    snapshot->soc_percent = 76U;
    snapshot->min_cell_voltage_mv = 3625U;
    snapshot->average_cell_voltage_mv = 3640U;
    snapshot->max_cell_voltage_mv = 3653U;
    snapshot->delta_cell_voltage_mv = 28U;
    snapshot->brightness_percent = 85U;
    snapshot->volume_percent = 65U;
    snapshot->bms_type = 0U;
    snapshot->controller_rpm = 4280U;
    snapshot->controller_power_w = 3800;
    snapshot->controller_temp_c = 46;
    snapshot->motor_temp_c = 58;
    snapshot->controller_tire_rim_inch = 17U;
    snapshot->controller_tire_aspect_percent = 70U;
    snapshot->controller_tire_width_mm = 100U;
    snapshot->controller_wheel_circumference_mm = 1800U;
    snapshot->controller_gear_ratio_centi = 100U;
    snapshot->controller_fallback_wheel_circumference_mm = 1800U;
    snapshot->controller_fallback_gear_ratio_centi = 100U;
    snapshot->controller_gear = 3U;
    snapshot->gps_local_hour = 14U;
    snapshot->gps_local_minute = 32U;
    snapshot->gps_local_year = 2026U;
    snapshot->gps_local_month = 7U;
    snapshot->gps_local_day = 15U;
    snapshot->gps_local_weekday = 3U;
    snapshot->gps_local_time_valid = true;
    snapshot->gps_local_date_valid = true;
    snapshot->wifi = ESP_BMS_WIFI_OFFLINE;
    snprintf(snapshot->bluetooth_name, sizeof(snapshot->bluetooth_name), "ESP32 BMS GPS");
    snprintf(snapshot->bms_info_text, sizeof(snapshot->bms_info_text), "BMS ONLINE");
    snprintf(snapshot->setup_ap_ssid, sizeof(snapshot->setup_ap_ssid), "fuckingBms_SIM001");
    snprintf(snapshot->setup_ap_password, sizeof(snapshot->setup_ap_password), "12345678");
    snprintf(snapshot->setup_ap_qr_payload, sizeof(snapshot->setup_ap_qr_payload),
             "WIFI:T:WPA;S:%s;P:%s;;",
             snapshot->setup_ap_ssid,
             snapshot->setup_ap_password);
    refresh_speed_snapshot(app);
}

static host_command_t command_from_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:
        return HOST_COMMAND_SPEED_UP;
    case SDLK_DOWN:
        return HOST_COMMAND_SPEED_DOWN;
    case SDLK_1:
        return HOST_COMMAND_PAGE_BATTERY;
    case SDLK_2:
        return HOST_COMMAND_PAGE_CONTROLLER;
    case SDLK_3:
        return HOST_COMMAND_PAGE_GPS;
    case SDLK_4:
        return HOST_COMMAND_PAGE_CAST;
    case SDLK_f:
        return HOST_COMMAND_TOGGLE_GPS_FIX;
    case SDLK_b:
        return HOST_COMMAND_TOGGLE_BMS;
    case SDLK_c:
        return HOST_COMMAND_TOGGLE_CONTROLLER;
    case SDLK_u:
        return HOST_COMMAND_TOGGLE_UNIT;
    case SDLK_e:
        return HOST_COMMAND_TOGGLE_CONSUMPTION;
    case SDLK_r:
        return HOST_COMMAND_ROTATE;
    case SDLK_q:
    case SDLK_ESCAPE:
        return HOST_COMMAND_QUIT;
    default:
        return HOST_COMMAND_NONE;
    }
}

static int sdl_event_watch(void *userdata, SDL_Event *event)
{
    host_app_t *app = userdata;
    if (event->type == SDL_QUIT) {
        app->running = false;
        app->sdl_quit_seen = true;
    } else if (event->type == SDL_WINDOWEVENT &&
               event->window.event == SDL_WINDOWEVENT_CLOSE) {
        app->running = false;
    } else if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        app->pending_command = command_from_key(event->key.keysym.sym);
    }
    return 1;
}

static void push_key(SDL_Keycode key)
{
    SDL_Event event = {
        .type = SDL_KEYDOWN,
    };
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = key;
    (void)SDL_PushEvent(&event);
}

static void rotate_display(host_app_t *app)
{
    const int32_t width = lv_display_get_horizontal_resolution(app->display);
    const int32_t height = lv_display_get_vertical_resolution(app->display);
    lv_display_set_resolution(app->display, height, width);
}

static bool apply_command(host_app_t *app, host_command_t command)
{
    esp_bms_dashboard_snapshot_t *snapshot = &app->snapshot;
    switch (command) {
    case HOST_COMMAND_SPEED_UP: {
        uint16_t *speed = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                              ? &app->controller_speed_kmh_deci
                              : &app->speed_kmh_deci;
        *speed = *speed >= 1790U ? 1800U : (uint16_t)(*speed + 10U);
        refresh_speed_snapshot(app);
        return true;
    }
    case HOST_COMMAND_SPEED_DOWN: {
        uint16_t *speed = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                              ? &app->controller_speed_kmh_deci
                              : &app->speed_kmh_deci;
        *speed = *speed <= 10U ? 0U : (uint16_t)(*speed - 10U);
        refresh_speed_snapshot(app);
        return true;
    }
    case HOST_COMMAND_PAGE_BATTERY:
        (void)esp_bms_lvgl_ui_set_page(ESP_BMS_LVGL_PAGE_BATTERY, true);
        return false;
    case HOST_COMMAND_PAGE_CONTROLLER:
        (void)esp_bms_lvgl_ui_set_page(ESP_BMS_LVGL_PAGE_CONTROLLER, true);
        return false;
    case HOST_COMMAND_PAGE_GPS:
        (void)esp_bms_lvgl_ui_set_page(ESP_BMS_LVGL_PAGE_GPS, true);
        return false;
    case HOST_COMMAND_PAGE_CAST:
        (void)esp_bms_lvgl_ui_set_page(ESP_BMS_LVGL_PAGE_CAST, true);
        return false;
    case HOST_COMMAND_TOGGLE_GPS_FIX:
        snapshot_flag_set(snapshot,
                          ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID,
                          !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID));
        refresh_speed_snapshot(app);
        return true;
    case HOST_COMMAND_TOGGLE_BMS:
        snapshot_flag_set(snapshot,
                          ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE,
                          !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE));
        return true;
    case HOST_COMMAND_TOGGLE_CONTROLLER:
        snapshot_flag_set(snapshot,
                          ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE,
                          !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_ONLINE));
        refresh_speed_snapshot(app);
        return true;
    case HOST_COMMAND_TOGGLE_UNIT:
        snapshot->speed_unit = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_KMH
                                   ? ESP_BMS_SPEED_UNIT_MPH
                                   : ESP_BMS_SPEED_UNIT_KMH;
        refresh_speed_snapshot(app);
        return true;
    case HOST_COMMAND_TOGGLE_CONSUMPTION:
        snapshot->average_consumption_valid = !snapshot->average_consumption_valid;
        refresh_speed_snapshot(app);
        return true;
    case HOST_COMMAND_ROTATE:
        rotate_display(app);
        return true;
    case HOST_COMMAND_QUIT:
        app->running = false;
        return false;
    case HOST_COMMAND_NONE:
    default:
        return false;
    }
}

static bool apply_action_event(host_app_t *app, const esp_bms_lvgl_action_event_t *event)
{
    esp_bms_dashboard_snapshot_t *snapshot = &app->snapshot;
    switch (event->action) {
    case ESP_BMS_LVGL_ACTION_NONE:
        return false;
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        snapshot_flag_set(snapshot,
                          ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED,
                          !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED));
        snapshot->wifi = snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_SETUP_AP_ENABLED)
                             ? ESP_BMS_WIFI_SETUP_AP
                             : ESP_BMS_WIFI_OFFLINE;
        return true;
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        snapshot->brightness_percent = snapshot->brightness_percent >= 85U
                                              ? 30U
                                              : snapshot->brightness_percent >= 60U ? 85U : 60U;
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_BRIGHTNESS_PERCENT_VALID)) {
            snapshot->brightness_percent = event->brightness_percent;
            return true;
        }
        return false;
    case ESP_BMS_LVGL_ACTION_SET_VOLUME:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_VOLUME_PERCENT_VALID)) {
            snapshot->volume_percent = event->volume_percent;
            return true;
        }
        return false;
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        rotate_display(app);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        snapshot->speed_unit = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_KMH
                                   ? ESP_BMS_SPEED_UNIT_MPH
                                   : ESP_BMS_SPEED_UNIT_KMH;
        refresh_speed_snapshot(app);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
        snapshot_flag_set(
            snapshot,
            ESP_BMS_DASHBOARD_FLAG_CONTROLLER_CONNECTION_ENABLED,
            !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_CONNECTION_ENABLED));
        refresh_speed_snapshot(app);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
        snapshot_flag_set(snapshot,
                          ESP_BMS_DASHBOARD_FLAG_CONTROLLER_PAGE_ENABLED,
                          !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_PAGE_ENABLED));
        snapshot->speed_dashboard_style =
            snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_PAGE_ENABLED)
                ? ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER
                : ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR;
        return true;
    case ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID) &&
            event->numeric_delta >= ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR &&
            event->numeric_delta <= ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE) {
            snapshot->speed_dashboard_style =
                (esp_bms_speed_dashboard_style_t)event->numeric_delta;
            snapshot_flag_set(
                snapshot,
                ESP_BMS_DASHBOARD_FLAG_CONTROLLER_PAGE_ENABLED,
                snapshot->speed_dashboard_style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER);
            return true;
        }
        return false;
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        snapshot->speed_source = snapshot->speed_source == ESP_BMS_SPEED_SOURCE_GPS
                                     ? ESP_BMS_SPEED_SOURCE_CONTROLLER
                                     : ESP_BMS_SPEED_SOURCE_GPS;
        refresh_speed_snapshot(app);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID) &&
            event->numeric_delta >= 0 &&
            event->numeric_delta <= (int16_t)ESP_BMS_REMAINING_RANGE_MAX_KM) {
            snapshot->preset_range_km = (uint16_t)event->numeric_delta;
            refresh_speed_snapshot(app);
            return true;
        }
        return false;
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        init_snapshot(app);
        return true;
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        snapshot_flag_set(
            snapshot,
            ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ADVERTISING,
            !snapshot_flag_get(snapshot, ESP_BMS_DASHBOARD_FLAG_BLUETOOTH_ADVERTISING));
        return true;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID)) {
            int32_t value = (int32_t)snapshot->controller_fallback_wheel_circumference_mm +
                            event->numeric_delta;
            value = value < 0 ? 0 : value > 4000 ? 4000 : value;
            snapshot->controller_fallback_wheel_circumference_mm = (uint16_t)value;
            return true;
        }
        return false;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
        if (esp_bms_lvgl_action_event_flag_get(
                event, ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID)) {
            int32_t value = (int32_t)snapshot->controller_fallback_gear_ratio_centi +
                            event->numeric_delta;
            value = value < 50 ? 50 : value > 1000 ? 1000 : value;
            snapshot->controller_fallback_gear_ratio_centi = (uint16_t)value;
            return true;
        }
        return false;
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        snapshot->bms_type = (uint8_t)(event->action - ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
        snapshot->controller_tire_rim_inch = event->controller_tire_rim_inch;
        snapshot->controller_tire_aspect_percent = event->controller_tire_aspect_percent;
        snapshot->controller_tire_width_mm = event->controller_tire_width_mm;
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        snapshot->controller_gear_ratio_centi = event->controller_gear_ratio_centi;
        return true;
    default:
        fprintf(stderr, "simulator: action %d received (no host hardware effect)\n",
                (int)event->action);
        return false;
    }
}

static bool process_ui_action(host_app_t *app)
{
    esp_bms_lvgl_action_event_t event = { 0 };
    if (esp_bms_lvgl_ui_take_action_event(&event) != ESP_OK) {
        return false;
    }
    return apply_action_event(app, &event);
}

static void print_help(const char *program)
{
    printf("用法: %s [--portrait] [--headless] [--screenshot FILE.bmp]\n", program);
    puts("快捷键: 上/下=速度  1/2/3/4=页面  f=GPS  b=BMS  c=控制器  u=单位  e=电耗  r=旋转  q=退出");
}

static bool save_screenshot(lv_display_t *display, const char *path)
{
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    const int32_t width = lv_display_get_horizontal_resolution(display);
    const int32_t height = lv_display_get_vertical_resolution(display);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                          width,
                                                          height,
                                                          32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (!renderer || !surface ||
        SDL_RenderReadPixels(renderer,
                             NULL,
                             SDL_PIXELFORMAT_ARGB8888,
                             surface ? surface->pixels : NULL,
                             surface ? surface->pitch : 0) != 0 ||
        SDL_SaveBMP(surface, path) != 0) {
        fprintf(stderr, "保存截图失败: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return false;
    }
    SDL_FreeSurface(surface);
    return true;
}

int main(int argc, char **argv)
{
    bool portrait = false;
    bool headless = false;
    bool run_ok = true;
    const char *screenshot_path = NULL;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--portrait") == 0) {
            portrait = true;
        } else if (strcmp(argv[index], "--headless") == 0) {
            headless = true;
        } else if (strcmp(argv[index], "--screenshot") == 0 && index + 1 < argc) {
            screenshot_path = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "未知参数: %s\n", argv[index]);
            print_help(argv[0]);
            return 2;
        }
    }

    if (headless && setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) {
        perror("setenv SDL_VIDEODRIVER");
        return 1;
    }

    host_app_t app = {
        .running = true,
    };
    init_snapshot(&app);

    lv_init();
    app.display = lv_sdl_window_create(portrait ? 240 : 320, portrait ? 320 : 240);
    if (!app.display) {
        fprintf(stderr, "SDL display creation failed: %s\n", SDL_GetError());
        lv_deinit();
        return 1;
    }
    lv_sdl_window_set_title(app.display, "ESP BMS LVGL Simulator");
    lv_indev_t *mouse = lv_sdl_mouse_create();
    if (!mouse) {
        fputs("SDL mouse creation failed\n", stderr);
        lv_deinit();
        return 1;
    }
    lv_indev_set_display(mouse, app.display);
    SDL_AddEventWatch(sdl_event_watch, &app);

    if (esp_bms_lvgl_ui_init(app.display) != ESP_OK ||
        esp_bms_lvgl_ui_update(&app.snapshot) != ESP_OK ||
        esp_bms_lvgl_ui_set_page(ESP_BMS_LVGL_PAGE_GPS, false) != ESP_OK) {
        fputs("真实 UI 初始化失败\n", stderr);
        SDL_DelEventWatch(sdl_event_watch, &app);
        lv_deinit();
        return 1;
    }
    print_help(argv[0]);
    unsigned frame = 0U;
    unsigned stress_updates = 0U;
    const unsigned frame_limit = headless ? (screenshot_path ? 120U : 360U) : UINT32_MAX;
    while (app.running && frame < frame_limit) {
        lv_timer_handler();
        if (!app.running) {
            break;
        }

        host_command_t command = app.pending_command;
        app.pending_command = HOST_COMMAND_NONE;
        bool snapshot_changed = apply_command(&app, command);
        snapshot_changed = process_ui_action(&app) || snapshot_changed;

        if (headless && !screenshot_path) {
            if (stress_updates < 300U) {
                app.speed_kmh_deci = (uint16_t)((stress_updates * 37U) % 1801U);
                app.snapshot.controller_temp_c = 35 + (int16_t)(stress_updates % 40U);
                app.snapshot.motor_temp_c = 45 + (int16_t)(stress_updates % 45U);
                app.snapshot.controller_gear = (uint8_t)((stress_updates % 6U) + 1U);
                app.snapshot.gps_local_minute = (uint8_t)(stress_updates % 60U);
                refresh_speed_snapshot(&app);
                if (esp_bms_lvgl_ui_set_page(
                        (stress_updates & 1U) == 0U ? ESP_BMS_LVGL_PAGE_BATTERY
                                                   : ESP_BMS_LVGL_PAGE_GPS,
                        false) != ESP_OK) {
                    fputs("页面压力切换失败\n", stderr);
                    run_ok = false;
                    app.running = false;
                }
                snapshot_changed = true;
                ++stress_updates;
            } else if (frame == 310U) {
                push_key(SDLK_r);
            }
        }

        if (snapshot_changed) {
            if (esp_bms_lvgl_ui_update(&app.snapshot) != ESP_OK) {
                fputs("真实 UI 快照更新失败\n", stderr);
                run_ok = false;
                app.running = false;
            }
        }
        lv_delay_ms(5);
        ++frame;
    }

    if (screenshot_path && !save_screenshot(app.display, screenshot_path)) {
        run_ok = false;
    }
    SDL_DelEventWatch(sdl_event_watch, &app);
    if (!app.sdl_quit_seen) {
        lv_deinit();
    }
    if (headless && frame == frame_limit) {
        printf("headless smoke passed: %s, %u frames\n",
               portrait ? "240x320" : "320x240",
               frame);
        if (!screenshot_path) {
            printf("fireblade stress updates: %u\n", stress_updates);
            if (stress_updates != 300U) {
                run_ok = false;
            }
        }
        printf("remaining range: %u km (%s)\n",
               app.snapshot.remaining_range_km,
               app.snapshot.remaining_range_valid ? "valid" : "invalid");
    }
    return run_ok ? 0 : 1;
}
