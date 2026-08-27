/*
 * esp_bms_lvgl_ui 核心：入口 API、全局状态、手势与页面管理。
 * 由 ui_split.py 从原单文件拆分生成。
 */
#include "esp_bms_lvgl_ui.h"
#include "esp_bms_lvgl_ui_internal.h"

const char *controller_gear_text(uint8_t gear, bool controller_online, bool gear_valid)
{
    if (!controller_online || !gear_valid) {
        return "-";
    }
    switch (gear) {
    case 0U:
        return "N";
    case 1U:
        return "D";
    case 2U:
        return "R";
    default:
        return "-";
    }
}
static uint8_t ui_flag_bit(uint32_t index)
{
    return (uint8_t)(1U << index);
}
bool ui_flag_get(uint8_t flags, uint32_t index)
{
    return (flags & ui_flag_bit(index)) != 0U;
}
void ui_flag_set(uint8_t *flags, uint32_t index, bool enabled)
{
    const uint8_t bit = ui_flag_bit(index);
    if (enabled) {
        *flags |= bit;
    } else {
        *flags &= (uint8_t)~bit;
    }
}

esp_bms_lvgl_ui_t s_ui;
bool s_touch_calibration_supported = true;
bool s_native_gestures_supported;

static bool s_language_zh = true;

void ui_language_set_zh(bool zh)
{
    s_language_zh = zh;
}

bool ui_language_zh(void)
{
    return s_language_zh;
}

const char *ui_t(const char *zh, const char *en)
{
    if (!zh) {
        return en;
    }
    return s_language_zh ? zh : (en && en[0] != '\0' ? en : zh);
}






bool ui_state_flag_get(ui_state_flag_t flag)
{
    return (s_ui.flags & (uint32_t)flag) != 0U;
}

void ui_state_flag_set(ui_state_flag_t flag, bool enabled)
{
    if (enabled) {
        s_ui.flags |= (uint32_t)flag;
    } else {
        s_ui.flags &= ~(uint32_t)flag;
    }
}

#define UI_FLAG(name) ui_state_flag_get(UI_STATE_FLAG_##name)
#define UI_SET_FLAG(name, enabled) ui_state_flag_set(UI_STATE_FLAG_##name, (enabled))

const lv_color_t COLOR_BG = LV_COLOR_MAKE(0x08, 0x0a, 0x0e);
const lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(0x16, 0x20, 0x29);
const lv_color_t COLOR_SOC = LV_COLOR_MAKE(0x00, 0x56, 0xbe);
const lv_color_t COLOR_WHITE = LV_COLOR_MAKE(0xff, 0xff, 0xff);
const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xe8, 0xf1, 0xff);
const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0xa9, 0xb4, 0xc8);
const lv_color_t COLOR_ACCENT = LV_COLOR_MAKE(0x74, 0xd6, 0xb5);
const lv_color_t COLOR_WARN = LV_COLOR_MAKE(0xff, 0xc8, 0x57);
const lv_color_t COLOR_BAD = LV_COLOR_MAKE(0xff, 0x6b, 0x6b);
const lv_color_t COLOR_BOOT_CYAN = LV_COLOR_MAKE(0x00, 0xe5, 0xff);
const lv_color_t COLOR_BOOT_BLUE = LV_COLOR_MAKE(0x00, 0x66, 0xff);
const lv_color_t COLOR_BOOT_DIM = LV_COLOR_MAKE(0x0d, 0x2d, 0x3d);
const lv_color_t COLOR_BOOT_GRID = LV_COLOR_MAKE(0x0a, 0x22, 0x2d);
const lv_color_t COLOR_DASHBOARD_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
const lv_color_t COLOR_DASHBOARD_PANEL = LV_COLOR_MAKE(0x09, 0x0c, 0x10);
const lv_color_t COLOR_DASHBOARD_SOC_PANEL = LV_COLOR_MAKE(0x06, 0x32, 0x70);
const lv_color_t COLOR_DASHBOARD_BORDER = LV_COLOR_MAKE(0x3e, 0x42, 0x47);
const lv_color_t COLOR_DASHBOARD_SOC_BORDER = LV_COLOR_MAKE(0x4a, 0x9b, 0xf5);
const lv_color_t COLOR_DASHBOARD_TITLE = LV_COLOR_MAKE(0x9d, 0xd8, 0xff);
const lv_color_t COLOR_DASHBOARD_VALUE = LV_COLOR_MAKE(0x2d, 0x8a, 0x66);
const lv_color_t COLOR_STATUS_OK = LV_COLOR_MAKE(0x20, 0xe8, 0x7b);
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
const lv_color_t COLOR_CONTROLLER_VALUE = LV_COLOR_MAKE(0x20, 0xd7, 0x83);
#endif
const lv_color_t COLOR_SPEED_BAND_DARK = LV_COLOR_MAKE(0x00, 0x55, 0x94);
const lv_color_t COLOR_SPEED_BAND_BLUE = LV_COLOR_MAKE(0x00, 0xb8, 0xf0);
const lv_color_t COLOR_SPEED_BAND_IDLE = LV_COLOR_MAKE(0x27, 0x29, 0x2d);
const lv_color_t COLOR_SPEED_BAND_DANGER = LV_COLOR_MAKE(0xc8, 0x24, 0x2f);
const lv_color_t COLOR_SPEED_DIVIDER = LV_COLOR_MAKE(0x00, 0xc8, 0xf2);
#if defined(CONFIG_IDF_TARGET_ESP32) && ESP_BMS_FEATURE_DASHBOARD_S1000RR
extern const uint8_t speed_dashboard_static_landscape_rgb565_start[]
    asm("_binary_speed_dashboard_static_landscape_rgb565_start");

const lv_image_dsc_t SPEED_DASHBOARD_STATIC_LANDSCAPE = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH,
    .header.h = SPEED_DASHBOARD_STATIC_LANDSCAPE_HEIGHT,
    .header.stride = SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH * sizeof(uint16_t),
    .data_size = SPEED_DASHBOARD_STATIC_LANDSCAPE_BYTES,
    .data = speed_dashboard_static_landscape_rgb565_start,
};
#endif
#if ESP_BMS_FEATURE_GPS
const lv_color_t COLOR_SPEED_GPS_OK = LV_COLOR_MAKE(0x43, 0xe3, 0x36);
#endif
const lv_color_t COLOR_FIREBLADE_RED = LV_COLOR_MAKE(0xf4, 0x18, 0x25);
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
const lv_color_t COLOR_FIREBLADE_BLACK = LV_COLOR_MAKE(0x05, 0x05, 0x05);
const lv_color_t COLOR_FIREBLADE_BRIDGE = LV_COLOR_MAKE(0x33, 0x33, 0x33);
const lv_color_t COLOR_FIREBLADE_MODE = LV_COLOR_MAKE(0x7f, 0x7f, 0x7e);
const lv_color_t COLOR_FIREBLADE_GRAY = LV_COLOR_MAKE(0xdf, 0xe0, 0xe2);
const lv_color_t COLOR_FIREBLADE_DANGER_BG = LV_COLOR_MAKE(0xff, 0xcf, 0xcf);
const lv_color_t COLOR_FIREBLADE_GREEN = LV_COLOR_MAKE(0x08, 0xa8, 0x13);
const lv_color_t COLOR_FIREBLADE_GEAR_BORDER = LV_COLOR_MAKE(0xc8, 0xc8, 0xc8);
#endif
const lv_color_t COLOR_SETTINGS_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
const lv_color_t COLOR_SETTINGS_CARD = LV_COLOR_MAKE(0x00, 0x00, 0x00);
const lv_color_t COLOR_SETTINGS_LIST = LV_COLOR_MAKE(0x24, 0x24, 0x24);
const lv_color_t COLOR_SETTINGS_BORDER = LV_COLOR_MAKE(0x32, 0x32, 0x32);
const lv_color_t COLOR_SETTINGS_TEXT = LV_COLOR_MAKE(0xff, 0xff, 0xff);
const lv_color_t COLOR_SETTINGS_MUTED = LV_COLOR_MAKE(0xff, 0xff, 0xff);
const lv_color_t COLOR_SETTINGS_ACCENT = LV_COLOR_MAKE(0xff, 0xff, 0xff);

const lv_color_t COLOR_SWITCH_ACTIVE = LV_COLOR_MAKE(0x34, 0xc7, 0x59);
const char *const DASHBOARD_TEMP_KEYS[ESP_BMS_BMS_TEMP_MAX_COUNT] = {
    "T1",
    "T2",
    "T3",
    "T4",
    "BAL",
    "MOS",
};

const uint16_t BMS_SAFETY_BITS[ESP_BMS_BMS_SAFETY_COUNT] = {
    ESP_BMS_BMS_SAFETY_CELL_OV,
    ESP_BMS_BMS_SAFETY_CELL_UV,
    ESP_BMS_BMS_SAFETY_OVER_TEMP,
    ESP_BMS_BMS_SAFETY_UNDER_TEMP,
    ESP_BMS_BMS_SAFETY_OVER_CURRENT,
    ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT,
    ESP_BMS_BMS_SAFETY_CELL_DELTA,
    ESP_BMS_BMS_SAFETY_BALANCING,
};

const char *const BMS_SAFETY_KEYS[ESP_BMS_BMS_SAFETY_COUNT] = {
    "过压保护", "欠压保护", "高温保护", "低温保护",
    "过流保护", "短路保护", "压差保护", "均衡状态",
};

const char *const BMS_SAFETY_KEYS_EN[ESP_BMS_BMS_SAFETY_COUNT] = {
    "Over-voltage", "Under-voltage", "Over-temp", "Under-temp",
    "Over-current", "Short circuit", "Cell delta", "Balancing",
};

#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
const char *const FIREBLADE_SCALE_LABELS[FIREBLADE_SCALE_LABEL_COUNT] = {
    "0", "20", "40", "60", "80", "100", "120", "140", "160", "180",
};
#endif

void clear_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
bool dashboard_native_landscape_enabled(void)
{
    return s_ui.width == (int32_t)DASHBOARD_STATIC_CACHE_WIDTH &&
           s_ui.height == (int32_t)DASHBOARD_STATIC_CACHE_HEIGHT;
}
#endif

bool bms_native_landscape_enabled(void)
{
    return s_ui.width >= 480 && s_ui.height >= 320 && s_ui.width >= s_ui.height;
}

bool bms_native_portrait_enabled(void)
{
    return s_ui.width == 320 && s_ui.height == 480;
}

#if LV_USE_SNAPSHOT
static void *dashboard_static_cache_alloc(size_t bytes)
{
#if ESP_BMS_LVGL_UI_SIMULATOR
    return lv_malloc(bytes);
#else
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}
#endif

static void dashboard_static_cache_free(void *pixels)
{
    if (!pixels) {
        return;
    }
#if ESP_BMS_LVGL_UI_SIMULATOR
    lv_free(pixels);
#else
    heap_caps_free(pixels);
#endif
}

#if LV_USE_SNAPSHOT
static bool dashboard_static_cache_enabled(void)
{
#if ESP_BMS_LVGL_UI_SIMULATOR || defined(CONFIG_IDF_TARGET_ESP32S3)
    return s_ui.width == (int32_t)DASHBOARD_STATIC_CACHE_WIDTH &&
           s_ui.height == (int32_t)DASHBOARD_STATIC_CACHE_HEIGHT;
#else
    return false;
#endif
}
#endif

void dashboard_static_cache_release_one(dashboard_static_cache_t *cache)
{
    if (!cache) {
        return;
    }
    if (cache->pixels) {
        lv_image_cache_drop(&cache->draw_buf);
        dashboard_static_cache_free(cache->pixels);
    }
    memset(cache, 0, sizeof(*cache));
}

static void dashboard_static_cache_release(void)
{
    dashboard_static_cache_release_one(&s_ui.battery_static_cache);
    dashboard_static_cache_release_one(&s_ui.fireblade_static_cache);
    dashboard_static_cache_release_one(&s_ui.speed_static_cache);
}

bool dashboard_static_cache_finalize(dashboard_static_cache_t *cache,
                                            lv_obj_t *page,
                                            lv_obj_t *static_layer,
                                            const char *name)
{
#if LV_USE_SNAPSHOT
    if (!dashboard_static_cache_enabled() || !cache || !page || !static_layer) {
        return false;
    }

    cache->pixels = dashboard_static_cache_alloc(DASHBOARD_STATIC_CACHE_BYTES);
    if (!cache->pixels) {
        ESP_LOGW(TAG, "dashboard_cache name=%s cache=off reason=psram", name);
        return false;
    }
    if (lv_draw_buf_init(&cache->draw_buf,
                         DASHBOARD_STATIC_CACHE_WIDTH,
                         DASHBOARD_STATIC_CACHE_HEIGHT,
                         LV_COLOR_FORMAT_RGB565,
                         LV_STRIDE_AUTO,
                         cache->pixels,
                         DASHBOARD_STATIC_CACHE_BYTES) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(static_layer,
                                     LV_COLOR_FORMAT_RGB565,
                                     &cache->draw_buf) != LV_RESULT_OK) {
        ESP_LOGW(TAG, "dashboard_cache name=%s cache=off reason=snapshot", name);
        dashboard_static_cache_release_one(cache);
        return false;
    }

    cache->image = lv_image_create(page);
    clear_style(cache->image);
    lv_image_set_src(cache->image, &cache->draw_buf);
    lv_obj_set_pos(cache->image, 0, 0);
    lv_obj_set_size(cache->image,
                    DASHBOARD_STATIC_CACHE_WIDTH,
                    DASHBOARD_STATIC_CACHE_HEIGHT);
    lv_obj_clear_flag(cache->image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(cache->image);
    lv_obj_delete(static_layer);
    cache->active = true;
    ESP_LOGI(TAG,
             "dashboard_cache name=%s cache=on bytes=%u",
             name,
             (unsigned)DASHBOARD_STATIC_CACHE_BYTES);
    return true;
#else
    (void)cache;
    (void)page;
    (void)static_layer;
    (void)name;
    return false;
#endif
}

void label_set_text_if_changed(lv_obj_t *obj, const char *text)
{
    const char *current = lv_label_get_text(obj);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(obj, text);
    }
}

void bms_label_set(lv_obj_t *obj, char *buffer, size_t buffer_len, const char *text)
{
    if (!obj || !buffer || buffer_len == 0U || !text) {
        return;
    }
    /* Only skip when the label already points at the buffer and the text is
     * unchanged. After a page release/recreate the label holds the LVGL
     * default placeholder text while the buffer still holds the old value, so
     * a plain strcmp() would wrongly skip re-applying the text and the value
     * would be replaced by the placeholder. */
    if (lv_label_get_text(obj) == buffer && strcmp(buffer, text) == 0) {
        return;
    }
    (void)snprintf(buffer, buffer_len, "%s", text);
    lv_label_set_text_static(obj, buffer);
}


void bms_native_set_safety_status(const esp_bms_dashboard_snapshot_t *snapshot)
{
    const bool online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        const uint16_t bit = BMS_SAFETY_BITS[index];
        const bool supported = online && (snapshot->bms_safety_supported_mask & bit) != 0U;
        const bool active = supported && (snapshot->bms_safety_active_mask & bit) != 0U;
        const char *text = "--";
        lv_color_t color = COLOR_MUTED;
        if (supported && bit == ESP_BMS_BMS_SAFETY_BALANCING) {
            text = active ? ui_t("均衡中", "Balancing") : ui_t("待机", "Idle");
            color = active ? COLOR_ACCENT : COLOR_MUTED;
        } else if (supported) {
            text = active ? ui_t("告警", "Alarm") : ui_t("正常", "OK");
            color = active ? COLOR_BAD : COLOR_STATUS_OK;
        }
        bms_label_set(s_ui.bms_safety_values[index],
                      s_ui.bms_safety_buf[index],
                      sizeof(s_ui.bms_safety_buf[index]),
                      text);
        label_set_text_color_if_changed(s_ui.bms_safety_values[index], color);
        lv_obj_t *check = s_ui.bms_safety_checks[index];
        const bool show_check = supported &&
                                (bit == ESP_BMS_BMS_SAFETY_BALANCING ? active : !active);
        if (check) {
            if (show_check) {
                lv_obj_clear_flag(check, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_line_color(check,
                                            bit == ESP_BMS_BMS_SAFETY_BALANCING
                                                ? COLOR_ACCENT
                                                : COLOR_STATUS_OK,
                                            LV_PART_MAIN);
            } else {
                lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void label_set_text_fmt_if_changed(lv_obj_t *obj, const char *fmt, ...)
{
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    label_set_text_if_changed(obj, text);
}

void label_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    const lv_color_t current = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    if (!lv_color_eq(current, color)) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
}

void set_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool get_active_pointer(lv_point_t *point)
{
    if (!point) {
        return false;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return false;
    }
    lv_indev_get_point(indev, point);
    return point->x >= 0 && point->y >= 0;
}

bool settings_view_is_visible(void)
{
    return s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
}

static bool quick_pull_start_allowed(const lv_point_t *point)
{
    if (!point || UI_FLAG(QUICK_PANEL_OPEN) || settings_view_is_visible()) {
        return false;
    }

    return point->y <= (s_ui.height * QUICK_PULL_START_MAX_Y_NUM) / QUICK_PULL_START_MAX_Y_DEN;
}

static bool return_home_start_allowed(const lv_point_t *point)
{
    if (!point) {
        return false;
    }

    return point->y >= (s_ui.height * RETURN_HOME_START_MIN_Y_NUM) / RETURN_HOME_START_MIN_Y_DEN;
}

void show_dashboard_view(void)
{
    settings_bms_popup_close();
    if (s_ui.settings_carousel) {
        lv_obj_clean(s_ui.settings_carousel);
    }
    if (s_ui.settings_swipe_drag_dx == 0) {
        settings_swipe_indicator_hide();
    }
    finish_page_scroll_state(true);
    dashboard_pages_release_except(s_ui.page);
    dashboard_page_content_ensure(s_ui.page);
    lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
    set_quick_panel_open(false);
}

void show_settings_view(void)
{
    finish_page_scroll_state(true);
    dashboard_page_content_release(ESP_BMS_LVGL_PAGE_BATTERY);
    dashboard_page_content_release(ESP_BMS_LVGL_PAGE_GPS);
    dashboard_page_content_release(ESP_BMS_LVGL_PAGE_CAST);
    dashboard_page_content_release(ESP_BMS_LVGL_PAGE_MUSIC);
    if (lv_obj_get_parent(s_ui.settings_page) == s_ui.staging_screen) {
        lv_obj_set_parent(s_ui.settings_page, s_ui.root);
    }
    lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
    set_quick_panel_open(false);
    set_obj_hidden(s_ui.quick_pull_zone, true);
    UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, false);
    settings_show_root();
    lv_obj_move_foreground(s_ui.settings_page);
}

void queue_action_with_commit(esp_bms_lvgl_action_t action, bool committed)
{
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
        s_ui.pending_event.action = action;
        ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, committed);
    }
}

void queue_action(esp_bms_lvgl_action_t action)
{
    queue_action_with_commit(action, true);
}

void queue_bms_bind_action(const char *mac)
{
    if (!mac || mac[0] == '\0') {
        return;
    }

    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_START_BMS_BIND;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, true);
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, BMS_MAC_VALID, true);
    (void)snprintf(s_ui.pending_event.bms_mac, sizeof(s_ui.pending_event.bms_mac), "%s", mac);
}

void queue_controller_bind_action(const char *mac)
{
    if (!mac || mac[0] == '\0') {
        return;
    }

    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, true);
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, CONTROLLER_MAC_VALID, true);
    (void)snprintf(s_ui.pending_event.controller_mac,
                   sizeof(s_ui.pending_event.controller_mac),
                   "%s",
                   mac);
}

void queue_touch_calibration_sample(uint8_t target_index,
                                           const lv_point_t *observed,
                                           const lv_point_t *expected)
{
    if (!observed || !expected) {
        return;
    }
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE;
    s_ui.pending_event.touch_observed_x = (uint16_t)clamp_i32(observed->x, 0, s_ui.width - 1);
    s_ui.pending_event.touch_observed_y = (uint16_t)clamp_i32(observed->y, 0, s_ui.height - 1);
    s_ui.pending_event.touch_target_x = (uint16_t)clamp_i32(expected->x, 0, s_ui.width - 1);
    s_ui.pending_event.touch_target_y = (uint16_t)clamp_i32(expected->y, 0, s_ui.height - 1);
    s_ui.pending_event.touch_target_index = target_index;
}

uint8_t clamp_brightness_percent(int32_t value)
{
    if (value < QUICK_BRIGHTNESS_MIN) {
        return QUICK_BRIGHTNESS_MIN;
    }
    if (value > QUICK_BRIGHTNESS_MAX) {
        return QUICK_BRIGHTNESS_MAX;
    }
    return (uint8_t)value;
}

uint8_t clamp_volume_percent(int32_t value)
{
    if (value < QUICK_VOLUME_MIN) {
        return QUICK_VOLUME_MIN;
    }
    if (value > QUICK_VOLUME_MAX) {
        return QUICK_VOLUME_MAX;
    }
    return (uint8_t)value;
}



void perform_ui_action(esp_bms_lvgl_action_t action, bool close_quick_panel)
{
    if (close_quick_panel) {
        set_quick_panel_open(false);
    }

#if MEDIA_HID_PAGE_ENABLED
    if (action == ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE) {
        /* Local play/pause icon toggle: the HID link has no playback status
         * feedback channel, so the remote keeps its own state (like a
         * consumer headset button). The key itself also answers/hangs up
         * phone calls on Android/iOS. */
        s_ui.music_play_paused = !s_ui.music_play_paused;
        if (s_ui.music_control_icons[1]) {
            lv_label_set_text(s_ui.music_control_icons[1],
                              s_ui.music_play_paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        }
        if (s_ui.music_control_captions[1]) {
            lv_label_set_text(s_ui.music_control_captions[1],
                              s_ui.music_play_paused ? ui_t("暂停/挂断", "Pause/Hang up")
                                                     : ui_t("播放/接听", "Play/Answer"));
        }
    }
#endif

    if (action == ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU) {
        set_quick_panel_open(!UI_FLAG(QUICK_PANEL_OPEN));
        return;
    }

    if (action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY) {
        lv_indev_wait_release(lv_indev_active());
        show_dashboard_view();
        quick_rotate_toast_show();
        queue_action_with_commit(action, false);
        return;
    }
    queue_action(action);

    if (action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS) {
        show_settings_view();
    } else if (action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD) {
        show_dashboard_view();
    }
}

void action_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    if (process_return_swipe_event(code, false)) {
        return;
    }

    esp_bms_lvgl_action_t action = (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    perform_ui_action(action, true);
}

bool process_return_swipe_event(lv_event_code_t code, bool allow_start)
{
    if (s_native_gestures_supported) {
        return false;
    }
    if (UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE)) {
        UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
        s_ui.return_swipe_drag_dy = 0;
        UI_SET_FLAG(RETURN_SWIPE_CANCELLED, false);
        return false;
    }

    if (code == LV_EVENT_PRESSED) {
        UI_SET_FLAG(RETURN_SWIPE_CANCELLED, false);
        s_ui.return_swipe_drag_dy = 0;
        UI_SET_FLAG(RETURN_SWIPE_TRACKING,
                    allow_start &&
                        get_active_pointer(&s_ui.return_swipe_start) &&
                        return_home_start_allowed(&s_ui.return_swipe_start));
        return false;
    }

    if (UI_FLAG(RETURN_SWIPE_CANCELLED)) {
        if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED) {
            UI_SET_FLAG(RETURN_SWIPE_CANCELLED, false);
            UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
        }
        return true;
    }

    if (code == LV_EVENT_PRESSING && UI_FLAG(RETURN_SWIPE_TRACKING)) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return false;
        }

        const int32_t dx = point.x - s_ui.return_swipe_start.x;
        const int32_t dy = point.y - s_ui.return_swipe_start.y;
        if (dx >= RETURN_HOME_RIGHT_CANCEL_MIN_DX &&
            abs_i32(dy) <= RETURN_HOME_RIGHT_CANCEL_MAX_DY) {
            UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
            s_ui.return_swipe_drag_dy = 0;
            UI_SET_FLAG(RETURN_SWIPE_CANCELLED, true);
            UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, UI_FLAG(QUICK_PANEL_OPEN) && !UI_FLAG(QUICK_PANEL_SETTLING));
            if (UI_FLAG(QUICK_PANEL_OPEN) && s_ui.quick_panel) {
                lv_obj_set_y(s_ui.quick_panel, 0);
            }
            lv_indev_wait_release(lv_indev_active());
            return true;
        }
        if (UI_FLAG(QUICK_PANEL_OPEN) && dy < 0 && abs_i32(dx) <= RETURN_HOME_SWIPE_MAX_DX) {
            s_ui.return_swipe_drag_dy = clamp_i32(-dy, 0, s_ui.height);
            UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, false);
            if (s_ui.quick_panel) {
                lv_obj_set_y(s_ui.quick_panel, -s_ui.return_swipe_drag_dy);
            }
            return true;
        }
        if (dy <= -RETURN_HOME_SWIPE_MIN_DY && abs_i32(dx) <= RETURN_HOME_SWIPE_MAX_DX) {
            UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
            s_ui.return_swipe_drag_dy = 0;
            if (UI_FLAG(QUICK_PANEL_OPEN)) {
                quick_panel_animate_to_open_state(false);
            } else if (s_ui.page != ESP_BMS_LVGL_PAGE_BATTERY) {
                move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, true);
            }
            lv_indev_wait_release(lv_indev_active());
            return true;
        }
    }

    if (code == LV_EVENT_RELEASED && UI_FLAG(RETURN_SWIPE_TRACKING) && UI_FLAG(QUICK_PANEL_OPEN)) {
        const bool should_return = s_ui.return_swipe_drag_dy >= RETURN_HOME_SWIPE_MIN_DY;
        const bool moved = s_ui.return_swipe_drag_dy > 3;
        UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
        s_ui.return_swipe_drag_dy = 0;
        if (should_return) {
            quick_panel_animate_to_open_state(false);
        } else if (s_ui.quick_panel) {
            quick_panel_animate_to_open_state(true);
        }
        if (moved) {
            UI_SET_FLAG(RETURN_SWIPE_CANCELLED, true);
        }
        return moved || should_return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
        s_ui.return_swipe_drag_dy = 0;
        if (code == LV_EVENT_PRESS_LOST && UI_FLAG(QUICK_PANEL_OPEN) && s_ui.quick_panel) {
            lv_obj_set_y(s_ui.quick_panel, 0);
            UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, !UI_FLAG(QUICK_PANEL_SETTLING));
        }
    }
    return false;
}

void return_swipe_event_cb(lv_event_t *event)
{
    (void)process_return_swipe_event(lv_event_get_code(event), true);
}

void quick_pull_event_cb(lv_event_t *event)
{
    if (s_native_gestures_supported) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (settings_view_is_visible()) {
        UI_SET_FLAG(QUICK_PULL_TRACKING, false);
        s_ui.quick_pull_drag_dy = 0;
        if (s_ui.quick_panel && !UI_FLAG(QUICK_PANEL_OPEN)) {
            lv_obj_set_y(s_ui.quick_panel, 0);
            set_obj_hidden(s_ui.quick_panel, true);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        s_ui.quick_pull_drag_dy = 0;
        UI_SET_FLAG(QUICK_PULL_TRACKING,
                    get_active_pointer(&s_ui.quick_pull_start) &&
                        quick_pull_start_allowed(&s_ui.quick_pull_start));
        if (UI_FLAG(QUICK_PULL_TRACKING) && s_ui.quick_panel) {
            quick_panel_stop_settle_anim();
            UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, false);
            lv_obj_set_y(s_ui.quick_panel, -s_ui.height);
            set_obj_hidden(s_ui.quick_panel, false);
            lv_obj_move_foreground(s_ui.quick_panel);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING && UI_FLAG(QUICK_PULL_TRACKING)) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }

        const int32_t dx = point.x - s_ui.quick_pull_start.x;
        const int32_t dy = point.y - s_ui.quick_pull_start.y;
        if (abs_i32(dx) > QUICK_PULL_MAX_DX) {
            UI_SET_FLAG(QUICK_PULL_TRACKING, false);
            s_ui.quick_pull_drag_dy = 0;
            if (s_ui.quick_panel) {
                lv_obj_set_y(s_ui.quick_panel, 0);
                set_obj_hidden(s_ui.quick_panel, true);
            }
            if (s_ui.quick_pull_zone) {
                lv_obj_move_foreground(s_ui.quick_pull_zone);
            }
            return;
        }
        s_ui.quick_pull_drag_dy = dy > 0 ? clamp_i32(point.y, 0, s_ui.height) : 0;
        if (s_ui.quick_panel) {
            lv_obj_set_y(s_ui.quick_panel, s_ui.quick_pull_drag_dy - s_ui.height);
        }
        return;
    }

    if (code == LV_EVENT_RELEASED && UI_FLAG(QUICK_PULL_TRACKING)) {
        const bool should_open = s_ui.quick_pull_drag_dy >= quick_pull_open_threshold();
        UI_SET_FLAG(QUICK_PULL_TRACKING, false);
        if (s_ui.quick_panel) {
            quick_panel_animate_to_open_state(should_open);
        }
        s_ui.quick_pull_drag_dy = 0;
        if (should_open) {
            lv_indev_wait_release(lv_indev_active());
        }
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        UI_SET_FLAG(QUICK_PULL_TRACKING, false);
        s_ui.quick_pull_drag_dy = 0;
        if (s_ui.quick_panel && !UI_FLAG(QUICK_PANEL_OPEN)) {
            quick_panel_animate_to_open_state(false);
        }
        if (s_ui.quick_pull_zone) {
            lv_obj_move_foreground(s_ui.quick_pull_zone);
        }
    }
}









const quick_panel_item_t QUICK_PANEL_ITEMS[QUICK_PANEL_BUTTON_COUNT] = {
#if ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER
    { QUICK_ITEM_BLUETOOTH, QUICK_BLUETOOTH_SYMBOL, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "蓝牙设置", "Bluetooth", false },
#endif
#if ESP_BMS_FEATURE_NETWORK
    { QUICK_ITEM_HOTSPOT, NULL, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "热点设置", "Hotspot", true },
#endif
    { QUICK_ITEM_ROTATE, LV_SYMBOL_LOOP, ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY,
      "旋转屏幕", "Rotate", false },
#if ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER
    { QUICK_ITEM_SPEED, LV_SYMBOL_GPS, ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT,
      "点击切换", "Tap to switch", false },
#endif
    { QUICK_ITEM_SETTINGS, LV_SYMBOL_SETTINGS, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "设备设置", "Settings", false },
    { QUICK_ITEM_LOCK, NULL, ESP_BMS_LVGL_ACTION_NONE,
      "LOCK", "LOCK", false },
};

const settings_option_t SETTINGS_OPTIONS[SETTINGS_OPTIONS_COUNT] = {
#if ESP_BMS_FEATURE_NETWORK
    { SETTINGS_DETAIL_HOTSPOT, "热点共享", "Hotspot share", "Setup AP", "Setup AP",
      QUICK_HOTSPOT_SYMBOL, &wlanJZ },
#endif
#if ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER
    { SETTINGS_DETAIL_BLUETOOTH, "蓝牙", "Bluetooth", "附近可见", "Visible nearby",
      QUICK_BLUETOOTH_SYMBOL, &bluetoothon },
#endif
#if ESP_BMS_FEATURE_BMS
    { SETTINGS_DETAIL_BMS, "BMS设置", "BMS settings", "扫描绑定", "Scan & bind",
      LV_SYMBOL_CHARGE, &lv_font_montserrat_24 },
#endif
#if ESP_BMS_FEATURE_GPS
    { SETTINGS_DETAIL_GPS, "GPS", "GPS", "定位与搜星", "Position & satellites",
      LV_SYMBOL_GPS, &lv_font_montserrat_24 },
#endif
#if ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER
    { SETTINGS_DETAIL_DASHBOARD, "仪表", "Dashboard", "显示与速度", "Display & speed",
      "D", &lv_font_montserrat_24 },
#endif
#if ESP_BMS_FEATURE_CONTROLLER
    { SETTINGS_DETAIL_CONTROLLER, "控制器", "Controller", "FarDriver", "FarDriver",
      "C", &lv_font_montserrat_24 },
#endif
    { SETTINGS_DETAIL_SYSTEM, "系统", "System", "显示与控制", "Display & controls",
      LV_SYMBOL_SETTINGS, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_ABOUT, "关于本机", "About", "设备信息", "Device info",
      "i", &lv_font_montserrat_24 },
};

const settings_detail_row_t SETTINGS_HOTSPOT_ROWS[SETTINGS_HOTSPOT_ROWS_COUNT] = {
    { "状态", "Status", "热点已打开", "Hotspot on", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "名称", "Name", "fuckingBms_xxxxxx", "fuckingBms_xxxxxx", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "密码", "Password", "8 DIGITS", "8 DIGITS", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "手机页面", "Web page", "192.168.4.1 网页配置", "192.168.4.1 web config", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "配置入口", "Setup portal", "开启配网入口", "Enable setup portal", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING, SETTINGS_SYSTEM_VIEW_ROOT },
    { "二维码", "QR code", "网页查看", "See on web page", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING, SETTINGS_SYSTEM_VIEW_ROOT },
};

const settings_detail_row_t SETTINGS_BLUETOOTH_ROWS[SETTINGS_BLUETOOTH_ROWS_COUNT] = {
    { "状态", "Status", "未连接", "Not connected", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "名称", "Name", "ESP32 BMS GPS", "ESP32 BMS GPS", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "可被发现", "Discoverable", "附近可见", "Visible nearby", ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING, SETTINGS_SYSTEM_VIEW_ROOT },
};

const settings_detail_row_t SETTINGS_BMS_ROWS[SETTINGS_BMS_ROWS_COUNT] = {
    { "蓝牙连接", "Bluetooth", "扫描绑定", "Scan & bind", ESP_BMS_LVGL_ACTION_START_BMS_BIND, SETTINGS_SYSTEM_VIEW_ROOT },
};

const char *const SETTINGS_BMS_TYPE_LABELS[SETTINGS_BMS_TYPE_COUNT] = {
    "蚂蚁 ANT",
    "极空 JK",
    "嘉佰达 JBD",
    "达锂 Daly",
    "彦阳 BMS",
};

const char *const SETTINGS_BMS_TYPE_LABELS_EN[SETTINGS_BMS_TYPE_COUNT] = {
    "ANT",
    "JK",
    "JBD",
    "Daly",
    "Yanyang BMS",
};

const esp_bms_lvgl_action_t SETTINGS_BMS_TYPE_ACTIONS[SETTINGS_BMS_TYPE_COUNT] = {
    ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JK,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG,
};

_Static_assert(ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS) == ARRAY_SIZE(SETTINGS_BMS_TYPE_ACTIONS),
               "BMS type labels must match runtime BMS type count");

const settings_detail_row_t SETTINGS_SYSTEM_ROWS[SETTINGS_SYSTEM_ROWS_COUNT] = {
#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
    { "亮度", "Brightness", "调节屏幕亮度", "Adjust screen brightness", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_BRIGHTNESS },
#endif
#if ESP_BMS_FEATURE_AUDIO
    { "音量", "Volume", "调节提示音量", "Adjust beep volume", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_VOLUME },
#endif
    { "调节条位置", "Slider position", "中间", "Middle", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_LEVEL_POSITION },
    { "启动动画", "Boot animation", "电量 / BMW / HONDA", "Battery / BMW / HONDA", ESP_BMS_LVGL_ACTION_NONE,
      SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION },
    { "屏幕校准", "Touch calibration", "校准触摸位置", "Calibrate touch position", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION },
    { "旋转屏幕", "Rotate screen", "点击操作", "Tap to act", ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY, SETTINGS_SYSTEM_VIEW_ROOT },
    { "语言切换", "Language", "点击操作", "Tap to act", ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "恢复默认", "Restore defaults", "清除设置", "Clear settings", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS, SETTINGS_SYSTEM_VIEW_ROOT },
};

const settings_detail_row_t SETTINGS_ABOUT_ROWS[SETTINGS_ABOUT_ROWS_COUNT] = {
    { "设备", "Device", SETTINGS_ABOUT_DEVICE_MODEL, SETTINGS_ABOUT_DEVICE_MODEL, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "固件版本", "Firmware", "--", "--", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "屏幕", "Display", SETTINGS_ABOUT_DISPLAY_MODEL, SETTINGS_ABOUT_DISPLAY_MODEL, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
};

/* 定义大小必须与 esp_bms_lvgl_ui_internal.h 中的 extern 声明一致，
 * 否则 ARRAY_SIZE() 按声明遍历会越界。 */
_Static_assert(ARRAY_SIZE(SETTINGS_OPTIONS) == SETTINGS_OPTIONS_COUNT,
               "SETTINGS_OPTIONS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_HOTSPOT_ROWS) == SETTINGS_HOTSPOT_ROWS_COUNT,
               "SETTINGS_HOTSPOT_ROWS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_BLUETOOTH_ROWS) == SETTINGS_BLUETOOTH_ROWS_COUNT,
               "SETTINGS_BLUETOOTH_ROWS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_BMS_ROWS) == SETTINGS_BMS_ROWS_COUNT,
               "SETTINGS_BMS_ROWS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS) == SETTINGS_BMS_TYPE_COUNT,
               "SETTINGS_BMS_TYPE_LABELS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_BMS_TYPE_ACTIONS) == SETTINGS_BMS_TYPE_COUNT,
               "SETTINGS_BMS_TYPE_ACTIONS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_SYSTEM_ROWS) == SETTINGS_SYSTEM_ROWS_COUNT,
               "SETTINGS_SYSTEM_ROWS size mismatch");
_Static_assert(ARRAY_SIZE(SETTINGS_ABOUT_ROWS) == SETTINGS_ABOUT_ROWS_COUNT,
               "SETTINGS_ABOUT_ROWS size mismatch");

void apply_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    const bool had_last_snapshot = UI_FLAG(LAST_SNAPSHOT_VALID);
    const bool previous_bms_online = SNAPSHOT_FLAG(&s_ui.last_snapshot, BMS_ONLINE);
    const bool previous_controller_online =
        SNAPSHOT_FLAG(&s_ui.last_snapshot, CONTROLLER_ONLINE);
    const uint8_t previous_bms_type = s_ui.last_snapshot.bms_type;
    const uint8_t previous_boot_animation_style =
        s_ui.last_snapshot.boot_animation_style;
    const uint16_t previous_preset_range_km = s_ui.last_snapshot.preset_range_km;
    const uint32_t previous_capacity_estimate_mah = s_ui.last_snapshot.bms_capacity_estimate_mah;
    const bool previous_bluetooth_enabled = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_ENABLED);
    const bool previous_bluetooth_advertising = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_ADVERTISING);
    const bool previous_bluetooth_connected = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_CONNECTED);
    const uint8_t previous_bms_scan_candidate_count = s_ui.last_snapshot.bms_scan_candidate_count;
    char previous_bms_info_text[sizeof(s_ui.last_snapshot.bms_info_text)] = { 0 };
    char previous_bms_error_text[sizeof(s_ui.last_snapshot.bms_error_text)] = { 0 };
    char previous_bluetooth_name[sizeof(s_ui.last_snapshot.bluetooth_name)] = { 0 };
    if (had_last_snapshot) {
        snprintf(previous_bluetooth_name,
                 sizeof(previous_bluetooth_name),
                 "%s",
                 s_ui.last_snapshot.bluetooth_name);
        snprintf(previous_bms_info_text,
                 sizeof(previous_bms_info_text),
                 "%s",
                 s_ui.last_snapshot.bms_info_text);
        snprintf(previous_bms_error_text,
                 sizeof(previous_bms_error_text),
                 "%s",
                 s_ui.last_snapshot.bms_error_text);
    }
    const bool bms_scan_candidates_changed =
        !had_last_snapshot ||
        strcmp(previous_bms_info_text, snapshot->bms_info_text) != 0 ||
        settings_ble_candidate_rows_changed(s_ui.last_snapshot.bms_scan_candidates,
                                            previous_bms_scan_candidate_count,
                                            snapshot->bms_scan_candidates,
                                            snapshot->bms_scan_candidate_count);
    const bool preset_range_changed = !had_last_snapshot ||
                                      previous_preset_range_km != snapshot->preset_range_km;
    const bool capacity_estimate_changed = !had_last_snapshot ||
                                           previous_capacity_estimate_mah !=
                                               snapshot->bms_capacity_estimate_mah;
    const bool controller_view_changed =
        settings_controller_view_changed(&s_ui.last_snapshot, snapshot, had_last_snapshot);
    const bool dashboard_view_changed = !had_last_snapshot ||
                                        s_ui.last_snapshot.speed_unit != snapshot->speed_unit ||
                                        s_ui.last_snapshot.speed_source != snapshot->speed_source ||
                                        s_ui.last_snapshot.speed_dashboard_style !=
                                            snapshot->speed_dashboard_style ||
                                        s_ui.last_snapshot.gps_module_state !=
                                            snapshot->gps_module_state;
    const bool gps_view_changed = !had_last_snapshot ||
                                  s_ui.last_snapshot.gps_module_state != snapshot->gps_module_state ||
                                  SNAPSHOT_FLAG(&s_ui.last_snapshot, GPS_FIX_VALID) !=
                                      SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID) ||
                                  s_ui.last_snapshot.gps_local_time_valid !=
                                      snapshot->gps_local_time_valid ||
                                  s_ui.last_snapshot.gps_local_hour != snapshot->gps_local_hour ||
                                  s_ui.last_snapshot.gps_local_minute != snapshot->gps_local_minute ||
                                  s_ui.last_snapshot.gps_satellite_info_valid !=
                                      snapshot->gps_satellite_info_valid ||
                                  s_ui.last_snapshot.gps_satellites_visible !=
                                      snapshot->gps_satellites_visible ||
                                  s_ui.last_snapshot.gps_satellites_used !=
                                      snapshot->gps_satellites_used ||
                                  s_ui.last_snapshot.gps_max_cn0 != snapshot->gps_max_cn0 ||
                                  s_ui.last_snapshot.gps_average_cn0 !=
                                      snapshot->gps_average_cn0 ||
                                  s_ui.last_snapshot.gps_constellation_mask !=
                                      snapshot->gps_constellation_mask ||
                                  s_ui.last_snapshot.gps_fix_dimension != snapshot->gps_fix_dimension ||
                                  s_ui.last_snapshot.gps_hdop_centi != snapshot->gps_hdop_centi ||
                                  s_ui.last_snapshot.gps_hdop_valid != snapshot->gps_hdop_valid;
    const bool controller_ble_changed =
        !had_last_snapshot ||
        SNAPSHOT_FLAG(&s_ui.last_snapshot, CONTROLLER_ONLINE) !=
            SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE) ||
        s_ui.last_snapshot.controller_scan_active != snapshot->controller_scan_active ||
        s_ui.last_snapshot.controller_scan_revision != snapshot->controller_scan_revision ||
        strcmp(s_ui.last_snapshot.controller_bound_name,
               snapshot->controller_bound_name) != 0 ||
        settings_controller_candidate_rows_changed(&s_ui.last_snapshot, snapshot);
    memcpy(&s_ui.last_snapshot, snapshot, sizeof(s_ui.last_snapshot));
    UI_SET_FLAG(LAST_SNAPSHOT_VALID, true);

    set_header(snapshot);
    speed_page_sync(snapshot);
    set_dashboard(snapshot);
    set_setup_ap(snapshot);
    speed_dashboard_style_apply(snapshot);
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
    if (dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS)) {
        set_controller_dashboard(snapshot);
    }
#endif
    set_gps_dashboard(snapshot);
    set_cast_page(snapshot);
    set_music_page(snapshot);
    if (had_last_snapshot &&
        ((!previous_bms_online && SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) ||
         (!previous_controller_online && SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE)))) {
        quick_toast_show_text(ui_t("绑定成功", "Bound"));
    } else if (s_ui.quick_connecting_toast_active &&
               had_last_snapshot &&
               strcmp(previous_bms_info_text, snapshot->bms_info_text) != 0 &&
               snapshot->bms_info_text[0] != '\0' &&
               strcmp(snapshot->bms_info_text, "BMS BIND") != 0 &&
               strcmp(snapshot->bms_info_text, "BMS SCAN") != 0 &&
               strcmp(snapshot->bms_info_text, "BMS CONN") != 0 &&
               strcmp(snapshot->bms_info_text, "BMS DISC") != 0 &&
               strcmp(snapshot->bms_info_text, "BMS ON") != 0 &&
               strcmp(snapshot->bms_info_text, "BMS OFF") != 0) {
        quick_toast_cancel();
        set_obj_hidden(s_ui.quick_toast, true);
    }

    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BLUETOOTH &&
        (!had_last_snapshot ||
         previous_bluetooth_enabled != SNAPSHOT_FLAG(snapshot, BLUETOOTH_ENABLED) ||
         previous_bluetooth_advertising != SNAPSHOT_FLAG(snapshot, BLUETOOTH_ADVERTISING) ||
         previous_bluetooth_connected != SNAPSHOT_FLAG(snapshot, BLUETOOTH_CONNECTED) ||
         strcmp(previous_bluetooth_name, snapshot->bluetooth_name) != 0 ||
         strcmp(previous_bms_error_text, snapshot->bms_error_text) != 0)) {
        settings_show_detail(SETTINGS_DETAIL_BLUETOOTH);
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BMS) {
        const bool bms_online_changed =
            !had_last_snapshot || previous_bms_online != SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
        const bool bms_type_changed = !had_last_snapshot || previous_bms_type != snapshot->bms_type;
        if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST &&
            (bms_scan_candidates_changed || bms_online_changed)) {
            settings_bms_ble_refresh_rows(snapshot,
                                          SETTINGS_BLE_SOURCE_BMS,
                                          false,
                                          "list-refresh");
        } else if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_TYPE_LIST &&
                   bms_type_changed) {
            settings_show_bms_type_picker();
        } else if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_ROOT &&
                   (bms_scan_candidates_changed || bms_online_changed || bms_type_changed ||
                    preset_range_changed || capacity_estimate_changed)) {
            settings_show_bms_detail();
        }
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_CONTROLLER &&
        controller_view_changed) {
        if (s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT) {
            settings_show_controller_detail();
        } else if (s_ui.settings_controller_view ==
                       (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST &&
                   controller_ble_changed) {
            settings_bms_ble_refresh_rows(snapshot,
                                          SETTINGS_BLE_SOURCE_CONTROLLER,
                                          false,
                                          "list-refresh");
        }
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_DASHBOARD &&
        dashboard_view_changed) {
        switch ((settings_dashboard_view_t)s_ui.settings_dashboard_view) {
        case SETTINGS_DASHBOARD_VIEW_STYLE_LIST:
            settings_show_controller_style_picker();
            break;
        case SETTINGS_DASHBOARD_VIEW_SPEED_UNIT_LIST:
            settings_show_speed_unit_picker();
            break;
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
        case SETTINGS_DASHBOARD_VIEW_SPEED_SOURCE_LIST:
            settings_show_speed_source_picker();
            break;
#endif
        case SETTINGS_DASHBOARD_VIEW_ROOT:
        default:
            settings_show_dashboard_detail();
            break;
        }
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_GPS && gps_view_changed) {
        settings_show_gps_detail();
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_SYSTEM &&
        s_ui.settings_system_view == (uint8_t)SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION &&
        (!had_last_snapshot ||
         previous_boot_animation_style != snapshot->boot_animation_style)) {
        settings_show_system_view(SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION);
    }
}

static void defer_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    memcpy(&s_ui.deferred_snapshot, snapshot, sizeof(s_ui.deferred_snapshot));
    UI_SET_FLAG(DEFERRED_SNAPSHOT_VALID, true);
}

void flush_deferred_dashboard_snapshot(void)
{
    if (!UI_FLAG(DEFERRED_SNAPSHOT_VALID)) {
        return;
    }

    esp_bms_dashboard_snapshot_t snapshot;
    memcpy(&snapshot, &s_ui.deferred_snapshot, sizeof(snapshot));
    UI_SET_FLAG(DEFERRED_SNAPSHOT_VALID, false);
    apply_dashboard_snapshot(&snapshot);
}

esp_err_t ui_rebuild_screen(const esp_bms_dashboard_snapshot_t *snapshot,
                            bool language_changed)
{
    ESP_RETURN_ON_FALSE(s_ui.display, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    lv_obj_t *old_root = s_ui.root;
    esp_bms_lvgl_page_t page = s_ui.page;
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER) {
        page = ESP_BMS_LVGL_PAGE_GPS;
    }
    const esp_bms_lvgl_action_event_t pending_event = s_ui.pending_event;
    const bool settings_visible = s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    const bool settings_boot_preview_active =
        s_ui.settings_boot_preview_timer != NULL;
    const bool screen_locked = UI_FLAG(SCREEN_LOCKED);
    const bool rotate_toast_active = UI_FLAG(QUICK_ROTATE_TOAST_ACTIVE);
    const uint8_t quick_level_position = s_ui.quick_level_position;
    const quick_panel_layout_t quick_layouts[QUICK_LAYOUT_COUNT] = {
        s_ui.quick_layouts[QUICK_LAYOUT_PORTRAIT],
        s_ui.quick_layouts[QUICK_LAYOUT_LANDSCAPE],
    };
    const uint8_t settings_detail_id = language_changed ? s_ui.settings_detail_id
                                                        : (uint8_t)SETTINGS_DETAIL_NONE;
    lv_display_t *display = s_ui.display;

    lv_indev_reset(NULL, NULL);
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
    }
    quick_toast_cancel();
    settings_calibration_start_timer_cancel();
    if (s_ui.quick_level_save_timer) {
        lv_timer_delete(s_ui.quick_level_save_timer);
        s_ui.quick_level_save_timer = NULL;
    }
    screen_unlock_timer_cancel();
    settings_boot_preview_timer_cancel();
    settings_bms_popup_close();
    settings_restore_popup_close();
    if (s_ui.settings_swipe_indicator) {
        lv_obj_delete(s_ui.settings_swipe_indicator);
        s_ui.settings_swipe_indicator = NULL;
    }
    if (s_ui.staging_screen) {
        lv_obj_delete(s_ui.staging_screen);
        s_ui.staging_screen = NULL;
    }
    if (old_root) {
        lv_obj_delete(old_root);
    }
    dashboard_static_cache_release();

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.display = display;
    s_ui.pending_event = pending_event;
    s_ui.quick_level_position = quick_level_position;
    memcpy(s_ui.quick_layouts, quick_layouts, sizeof(s_ui.quick_layouts));
    if (snapshot) {
        memcpy(&s_ui.last_snapshot, snapshot, sizeof(s_ui.last_snapshot));
        UI_SET_FLAG(LAST_SNAPSHOT_VALID, true);
    }
    UI_SET_FLAG(INITIALIZED, true);
    UI_SET_FLAG(SCREEN_LOCKED, screen_locked);
    create_screen(display);
    move_to_page(page, false);
    if (settings_boot_preview_active) {
        show_settings_view();
        settings_show_system_view(SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION);
    } else if (settings_visible) {
        show_settings_view();
        if (settings_detail_id != (uint8_t)SETTINGS_DETAIL_NONE &&
            settings_detail_is_enabled((settings_detail_id_t)settings_detail_id)) {
            settings_show_detail((settings_detail_id_t)settings_detail_id);
        }
    } else {
        show_dashboard_view();
    }
    if (rotate_toast_active) {
        quick_rotate_toast_show();
    }
    screen_lock_reapply();
    return ESP_OK;
}

esp_err_t rebuild_screen_if_needed(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(s_ui.display, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    const int32_t width = lv_display_get_horizontal_resolution(s_ui.display);
    const int32_t height = lv_display_get_vertical_resolution(s_ui.display);
    if (width == s_ui.width && height == s_ui.height) {
        return ESP_OK;
    }

    return ui_rebuild_screen(snapshot, false);
}

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display,
                               bool touch_calibration_supported,
                               bool native_gestures_supported)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is required");
    ESP_RETURN_ON_FALSE(!UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI already initialized");
    s_touch_calibration_supported = touch_calibration_supported;
    s_native_gestures_supported = native_gestures_supported;
    create_screen(display);
    UI_SET_FLAG(INITIALIZED, true);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_suspend(void)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");

    lv_indev_reset(NULL, NULL);
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
    }
    quick_toast_cancel();
    settings_calibration_start_timer_cancel();
    if (s_ui.quick_level_save_timer) {
        lv_timer_delete(s_ui.quick_level_save_timer);
        s_ui.quick_level_save_timer = NULL;
    }
    screen_unlock_timer_cancel();
    settings_boot_preview_timer_cancel();
    settings_bms_popup_close();
    settings_restore_popup_close();
    if (s_ui.settings_swipe_indicator) {
        lv_obj_delete(s_ui.settings_swipe_indicator);
        s_ui.settings_swipe_indicator = NULL;
    }
    if (s_ui.staging_screen) {
        lv_obj_delete(s_ui.staging_screen);
        s_ui.staging_screen = NULL;
    }
    if (s_ui.root) {
        lv_obj_delete(s_ui.root);
    }
    dashboard_static_cache_release();

    memset(&s_ui, 0, sizeof(s_ui));
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    const bool language_changed = snapshot->language_zh != s_language_zh;
    ESP_RETURN_ON_ERROR(rebuild_screen_if_needed(snapshot), TAG, "rebuild UI failed");
    if (UI_FLAG(DRAGGING) || UI_FLAG(SETTLING)) {
        defer_dashboard_snapshot(snapshot);
        return ESP_OK;
    }
    if (language_changed) {
        ui_language_set_zh(snapshot->language_zh);
        return ui_rebuild_screen(snapshot, true);
    }

    apply_dashboard_snapshot(snapshot);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_show_dashboard(void)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_ERROR(rebuild_screen_if_needed(&s_ui.last_snapshot), TAG,
                        "rebuild dashboard pages failed");
    show_dashboard_view();
    screen_lock_reapply();
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_touch_calibration_result(bool success)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_FALSE(s_touch_calibration_supported, ESP_ERR_INVALID_STATE,
                        TAG, "touch calibration is unsupported");
    ESP_RETURN_ON_FALSE(s_ui.settings_system_view ==
                            (uint8_t)SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION,
                        ESP_ERR_INVALID_STATE, TAG, "touch calibration view is not active");
    set_obj_hidden(s_ui.settings_calibration_target, true);
    label_set_text_if_changed(s_ui.settings_calibration_status,
                              success ? ui_t("校准成功，返回系统设置", "Calibrated, back to System")
                                      : ui_t("校准失败，返回后重试", "Calibration failed, retry"));
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_FALSE(page == ESP_BMS_LVGL_PAGE_BATTERY ||
                            page == ESP_BMS_LVGL_PAGE_CONTROLLER ||
                            page == ESP_BMS_LVGL_PAGE_GPS ||
                            page == ESP_BMS_LVGL_PAGE_CAST ||
                            page == ESP_BMS_LVGL_PAGE_MUSIC,
                        ESP_ERR_INVALID_ARG, TAG, "invalid page");

    move_to_page(page, animated);
    return ESP_OK;
}



static bool native_focus_callback(lv_event_cb_t callback)
{
    return callback == quick_panel_item_event_cb ||
           callback == quick_level_event_cb ||
           callback == quick_edit_event_cb ||
           callback == action_event_cb ||
           callback == settings_option_event_cb ||
           callback == settings_detail_action_event_cb ||
           callback == settings_bms_type_option_event_cb ||
           callback == settings_bms_ble_candidate_event_cb ||
           callback == settings_preset_range_button_event_cb ||
           callback == settings_preset_range_confirm_event_cb ||
           callback == settings_controller_confirm_event_cb ||
           callback == settings_controller_value_event_cb ||
           callback == settings_controller_style_option_event_cb ||
           callback == settings_speed_unit_button_event_cb ||
           callback == settings_speed_unit_option_event_cb ||
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
           callback == settings_speed_source_button_event_cb ||
           callback == settings_speed_source_option_event_cb ||
#endif
           callback == settings_restore_cancel_event_cb ||
           callback == settings_restore_accept_event_cb ||
           callback == settings_bms_type_button_event_cb ||
           callback == settings_bms_bind_confirm_cancel_event_cb ||
           callback == settings_bms_bind_confirm_accept_event_cb ||
           callback == settings_bms_ble_refresh_event_cb ||
           callback == settings_system_position_option_event_cb ||
           callback == settings_boot_preview_button_event_cb ||
           callback == settings_boot_animation_option_event_cb;
}

static bool native_focusable(lv_obj_t *obj)
{
    if (!obj || !lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) ||
        lv_obj_has_state(obj, LV_STATE_DISABLED)) {
        return false;
    }
    const uint32_t event_count = lv_obj_get_event_count(obj);
    for (uint32_t index = 0U; index < event_count; ++index) {
        lv_event_dsc_t *descriptor = lv_obj_get_event_dsc(obj, index);
        if (descriptor && native_focus_callback(lv_event_dsc_get_cb(descriptor))) {
            return true;
        }
    }
    return false;
}

static void native_focus_collect(lv_obj_t *obj, native_focus_list_t *list)
{
    if (!obj || !list || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    /* ponytail: 32 controls cover every current page; raise only if a page grows past it. */
    if (native_focusable(obj) && list->count < NATIVE_FOCUS_CAPACITY) {
        list->items[list->count++] = obj;
    }
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t index = 0U; index < child_count; ++index) {
        native_focus_collect(lv_obj_get_child(obj, (int32_t)index), list);
    }
}

static lv_obj_t *native_focus_root(void)
{
    if (s_ui.settings_restore_popup) {
        return s_ui.settings_restore_popup;
    }
    if (s_ui.settings_bms_popup) {
        return s_ui.settings_bms_popup;
    }
    if (UI_FLAG(QUICK_PANEL_OPEN)) {
        return s_ui.quick_panel;
    }
    if (!settings_view_is_visible()) {
        return NULL;
    }
    return s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_NONE
               ? s_ui.settings_carousel
               : s_ui.settings_detail;
}

native_focus_list_t native_focus_list(void)
{
    native_focus_list_t list = { 0 };
    native_focus_collect(native_focus_root(), &list);
    return list;
}

static void native_focus_set(const native_focus_list_t *list, size_t selected)
{
    if (!list || selected >= list->count) {
        return;
    }
    for (size_t index = 0U; index < list->count; ++index) {
        lv_obj_remove_state(list->items[index], LV_STATE_FOCUSED);
    }
    lv_obj_t *obj = list->items[selected];
    lv_obj_set_style_outline_width(obj, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(obj, COLOR_SWITCH_ACTIVE, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 1, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_state(obj, LV_STATE_FOCUSED);
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
}

static void native_focus_step(int direction)
{
    const native_focus_list_t list = native_focus_list();
    if (list.count == 0U) {
        return;
    }
    size_t selected = direction > 0 ? 0U : list.count - 1U;
    for (size_t index = 0U; index < list.count; ++index) {
        if (!lv_obj_has_state(list.items[index], LV_STATE_FOCUSED)) {
            continue;
        }
        selected = direction > 0 ? (index + 1U) % list.count
                                 : (index + list.count - 1U) % list.count;
        break;
    }
    native_focus_set(&list, selected);
}

static void native_focus_confirm(void)
{
    const native_focus_list_t list = native_focus_list();
    if (list.count == 0U) {
        return;
    }
    for (size_t index = 0U; index < list.count; ++index) {
        if (lv_obj_has_state(list.items[index], LV_STATE_FOCUSED)) {
            (void)lv_obj_send_event(list.items[index], LV_EVENT_CLICKED, NULL);
            return;
        }
    }
    native_focus_set(&list, 0U);
}

void native_gesture_back(void)
{
    if (s_ui.settings_restore_popup) {
        settings_restore_popup_close();
    } else if (s_ui.settings_bms_popup) {
        settings_bms_bind_confirm_cancel();
    } else if (UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE)) {
        quick_level_overlay_hide();
    } else if (UI_FLAG(QUICK_PANEL_OPEN)) {
        set_quick_panel_open(false);
    } else if (settings_view_is_visible()) {
        settings_navigate_back();
    }
}

static void native_gesture_change_page(int direction)
{
    if (settings_view_is_visible() || UI_FLAG(QUICK_PANEL_OPEN) ||
        s_ui.boot_active || UI_FLAG(SCREEN_LOCKED)) {
        return;
    }
    const int32_t current_x = page_target_scroll_x(s_ui.page);
    const int32_t last_x = page_last_scroll_x();
    const int32_t target_x = clamp_i32(current_x + (direction * s_ui.width), 0, last_x);
    move_to_page(page_from_scroll_x(target_x), true);
}

esp_err_t esp_bms_lvgl_ui_handle_native_gesture(esp_bms_lvgl_native_gesture_t gesture)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    ESP_RETURN_ON_FALSE(s_native_gestures_supported, ESP_ERR_INVALID_STATE, TAG,
                        "native gestures are unsupported");

    switch (gesture) {
    case ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_RIGHT:
        native_gesture_change_page(1);
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_LEFT:
        native_gesture_change_page(-1);
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_DOWN:
        if (!settings_view_is_visible() && !UI_FLAG(QUICK_PANEL_OPEN) &&
            !s_ui.boot_active && !UI_FLAG(SCREEN_LOCKED)) {
            set_quick_panel_open(true);
        }
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_UP:
        if (UI_FLAG(QUICK_PANEL_OPEN)) {
            set_quick_panel_open(false);
        }
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_DOUBLE_TAP:
        ESP_LOGI(TAG, "native double tap recognized; no action assigned");
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_KEY_PREVIOUS:
        native_focus_step(-1);
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_KEY_NEXT:
        native_focus_step(1);
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_KEY_CONFIRM:
        native_focus_confirm();
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_KEY_BACK:
        native_gesture_back();
        break;
    case ESP_BMS_LVGL_NATIVE_GESTURE_NONE:
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_bms_lvgl_data_source_t esp_bms_lvgl_ui_stable_data_source(void)
{
    if (!UI_FLAG(INITIALIZED) || UI_FLAG(DRAGGING) || UI_FLAG(SETTLING) ||
        UI_FLAG(QUICK_PANEL_OPEN) ||
        (s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN))) {
        return ESP_BMS_LVGL_DATA_SOURCE_NONE;
    }
    switch (s_ui.page) {
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        return ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD;
    case ESP_BMS_LVGL_PAGE_CAST:
    case ESP_BMS_LVGL_PAGE_MUSIC:
        return ESP_BMS_LVGL_DATA_SOURCE_NONE;
    case ESP_BMS_LVGL_PAGE_BATTERY:
    default:
        return ESP_BMS_LVGL_DATA_SOURCE_BMS;
    }
}

bool esp_bms_lvgl_ui_drag_active(void)
{
    return UI_FLAG(INITIALIZED) &&
           (UI_FLAG(DRAGGING) || UI_FLAG(SETTLING) || s_ui.page_scroll_gesture_active);
}

esp_err_t esp_bms_lvgl_ui_take_action_event(esp_bms_lvgl_action_event_t *event)
{
    ESP_RETURN_ON_FALSE(event, ESP_ERR_INVALID_ARG, TAG, "action event output is required");
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");

    *event = s_ui.pending_event;
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_take_action(esp_bms_lvgl_action_t *action)
{
    ESP_RETURN_ON_FALSE(action, ESP_ERR_INVALID_ARG, TAG, "action output is required");
    esp_bms_lvgl_action_event_t event = { 0 };
    ESP_RETURN_ON_ERROR(esp_bms_lvgl_ui_take_action_event(&event), TAG, "take action event failed");
    *action = event.action;
    return ESP_OK;
}
