#include "esp_bms_lvgl_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_lvgl_contract.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "draw/lv_draw_line.h"
#include "widgets/canvas/lv_canvas.h"

static const char *TAG = "bms_lvgl_ui";

#define QUICK_PANEL_BUTTON_COUNT 5
#define QUICK_PULL_OPEN_DY 34
#define QUICK_PULL_MAX_DX 64
#define RETURN_SWIPE_MIN_DX 58
#define RETURN_SWIPE_MAX_DY 46
#define QUICK_BRIGHTNESS_MIN 10
#define QUICK_BRIGHTNESS_MAX 100
#define QUICK_VOLUME_MIN 0
#define QUICK_VOLUME_MAX 100
#define QUICK_PANEL_ITEM_COUNT 5
#define QUICK_TILE_SCALE_NORMAL 256
#define QUICK_TILE_SCALE_PRESSED 270
#define QUICK_TILE_SCALE_LONG 292
#define QUICK_BLUETOOTH_ICON_W 26
#define QUICK_BLUETOOTH_ICON_H 32

_Static_assert(sizeof(esp_bms_dashboard_snapshot_t) == 388,
               "esp_bms_dashboard_snapshot_t ABI size changed; update C snapshot consumers too");
_Static_assert(sizeof(esp_bms_lvgl_action_t) == 4,
               "esp_bms_lvgl_action_t ABI size changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_NONE == 0,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD == 1,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU == 2,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_SETTINGS == 3,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING == 4,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS == 5,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY == 6,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT == 7,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE == 8,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_START_BMS_BIND == 9,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS == 10,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS == 11,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_VOLUME == 12,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");

typedef struct {
    lv_display_t *display;
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *pages;
    lv_obj_t *battery_page;
    lv_obj_t *gps_page;
    lv_obj_t *settings_page;
    lv_obj_t *settings_button;
    lv_obj_t *quick_pull_zone;
    lv_obj_t *quick_panel;
    lv_obj_t *quick_panel_items[QUICK_PANEL_BUTTON_COUNT];
    lv_obj_t *quick_brightness_label;
    lv_obj_t *quick_brightness_track;
    lv_obj_t *quick_brightness_fill;
    lv_obj_t *quick_brightness_knob;
    lv_obj_t *quick_brightness_tile;
    lv_obj_t *quick_volume_label;
    lv_obj_t *quick_volume_track;
    lv_obj_t *quick_volume_fill;
    lv_obj_t *quick_volume_knob;
    lv_obj_t *quick_volume_tile;
    lv_obj_t *quick_edit_button;
    lv_obj_t *quick_edit_icon;
    lv_obj_t *quick_panel_item_icons[QUICK_PANEL_BUTTON_COUNT];
    bool quick_panel_item_active[QUICK_PANEL_BUTTON_COUNT];

    lv_obj_t *speed;
    lv_obj_t *gps_state;
    lv_obj_t *bms_state;
    lv_obj_t *wifi_state;
    lv_obj_t *ota_state;
    lv_obj_t *soc;
    lv_obj_t *pack_voltage;
    lv_obj_t *current;
    lv_obj_t *capacity;
    lv_obj_t *cell_stats;
    lv_obj_t *bms_error;
    lv_obj_t *temperature;
    lv_obj_t *local_battery;
    lv_obj_t *gps_detail;
    lv_obj_t *setup_ap_info;
    lv_obj_t *setup_ap_qr;

    int32_t width;
    int32_t height;
    bool dragging;
    bool settling;
    int32_t drag_start_pages_x;
    uint32_t drag_last_sample_log_ms;
    lv_point_t quick_pull_start;
    lv_point_t return_swipe_start;
    lv_point_t quick_drag_start;
    esp_bms_lvgl_page_t page;
    esp_bms_lvgl_action_event_t pending_event;
    lv_obj_t *quick_drag_obj;
    int32_t quick_drag_obj_x;
    int32_t quick_drag_obj_y;
    uint8_t quick_brightness_percent;
    uint8_t quick_volume_percent;
    esp_bms_lvgl_action_t quick_long_action_pending;
    bool deferred_snapshot_valid;
    bool quick_panel_open;
    bool quick_pull_tracking;
    bool return_swipe_tracking;
    bool quick_edit_mode;
    bool quick_drag_moved;
    bool quick_long_triggered;
    esp_bms_dashboard_snapshot_t deferred_snapshot;
    char current_setup_ap_qr_payload[sizeof(((esp_bms_dashboard_snapshot_t *)0)->setup_ap_qr_payload)];
    bool initialized;
} esp_bms_lvgl_ui_t;

static esp_bms_lvgl_ui_t s_ui;
LV_DRAW_BUF_DEFINE_STATIC(s_quick_bluetooth_draw_buf,
                          QUICK_BLUETOOTH_ICON_W,
                          QUICK_BLUETOOTH_ICON_H,
                          LV_COLOR_FORMAT_ARGB8888);
static bool s_quick_bluetooth_draw_buf_initialized;

static void finish_page_scroll_state(bool flush_snapshot);
static void move_to_page(esp_bms_lvgl_page_t page, bool animated);
static void show_dashboard_view(void);
static void set_quick_panel_open(bool open);
static void set_quick_edit_mode(bool edit_mode);
static void quick_tile_set_scale(lv_obj_t *obj, int32_t scale);

static const lv_color_t COLOR_BG = LV_COLOR_MAKE(0x08, 0x0a, 0x0e);
static const lv_color_t COLOR_PANEL = LV_COLOR_MAKE(0x12, 0x18, 0x20);
static const lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(0x16, 0x20, 0x29);
static const lv_color_t COLOR_SOC = LV_COLOR_MAKE(0x05, 0x68, 0xde);
static const lv_color_t COLOR_WHITE = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xe8, 0xf1, 0xff);
static const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0xa9, 0xb4, 0xc8);
static const lv_color_t COLOR_ACCENT = LV_COLOR_MAKE(0x74, 0xd6, 0xb5);
static const lv_color_t COLOR_WARN = LV_COLOR_MAKE(0xff, 0xc8, 0x57);
static const lv_color_t COLOR_BAD = LV_COLOR_MAKE(0xff, 0x6b, 0x6b);

static void clear_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 4, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(obj, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return obj;
}

static void label_set_text_if_changed(lv_obj_t *obj, const char *text)
{
    const char *current = lv_label_get_text(obj);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(obj, text);
    }
}

static void label_set_text_fmt_if_changed(lv_obj_t *obj, const char *fmt, ...)
{
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    label_set_text_if_changed(obj, text);
}

static void label_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    const lv_color_t current = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    if (!lv_color_eq(current, color)) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
}

static lv_obj_t *panel_label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                             lv_color_t bg, const lv_font_t *font)
{
    lv_obj_t *box = panel(parent, x, y, w, h, bg);
    return label(box, 4, 4, w - 8, h - 8, font);
}

static void set_obj_hidden(lv_obj_t *obj, bool hidden)
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

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool get_active_pointer(lv_point_t *point)
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

#if LV_USE_QRCODE
static lv_obj_t *setup_ap_qr(lv_obj_t *parent, int32_t x, int32_t y, int32_t size)
{
    lv_obj_t *obj = lv_qrcode_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_qrcode_set_size(obj, size);
    lv_qrcode_set_dark_color(obj, lv_color_black());
    lv_qrcode_set_light_color(obj, lv_color_white());
    lv_qrcode_set_quiet_zone(obj, true);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}
#else
static lv_obj_t *setup_ap_qr(lv_obj_t *parent, int32_t x, int32_t y, int32_t size)
{
    (void)parent;
    (void)x;
    (void)y;
    (void)size;
    return NULL;
}
#endif

static void show_dashboard_view(void)
{
    finish_page_scroll_state(true);
    lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
    set_quick_panel_open(false);
}

static void show_settings_view(void)
{
    finish_page_scroll_state(true);
    lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
    set_quick_panel_open(false);
    set_obj_hidden(s_ui.quick_pull_zone, true);
    lv_obj_move_foreground(s_ui.settings_page);
}

static void queue_action(esp_bms_lvgl_action_t action)
{
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        s_ui.pending_event.action = action;
        s_ui.pending_event.brightness_percent_valid = false;
        s_ui.pending_event.brightness_percent = 0;
        s_ui.pending_event.volume_percent_valid = false;
        s_ui.pending_event.volume_percent = 0;
    }
}

static uint8_t clamp_brightness_percent(int32_t value)
{
    if (value < QUICK_BRIGHTNESS_MIN) {
        return QUICK_BRIGHTNESS_MIN;
    }
    if (value > QUICK_BRIGHTNESS_MAX) {
        return QUICK_BRIGHTNESS_MAX;
    }
    return (uint8_t)value;
}

static uint8_t clamp_volume_percent(int32_t value)
{
    if (value < QUICK_VOLUME_MIN) {
        return QUICK_VOLUME_MIN;
    }
    if (value > QUICK_VOLUME_MAX) {
        return QUICK_VOLUME_MAX;
    }
    return (uint8_t)value;
}

typedef enum {
    QUICK_LEVEL_BRIGHTNESS = 0,
    QUICK_LEVEL_VOLUME = 1,
} quick_level_kind_t;

static bool quick_level_control_is_vertical(lv_obj_t *tile)
{
    return tile && lv_obj_get_height(tile) > lv_obj_get_width(tile);
}

static void layout_quick_level_control(lv_obj_t *tile,
                                       lv_obj_t *label_obj,
                                       lv_obj_t *track,
                                       lv_obj_t *fill,
                                       lv_obj_t *knob,
                                       uint8_t value,
                                       uint8_t min_value,
                                       uint8_t max_value)
{
    if (!tile || !label_obj || !track || !fill || !knob) {
        return;
    }

    const int32_t w = lv_obj_get_width(tile);
    const int32_t h = lv_obj_get_height(tile);
    const int32_t range = max_value > min_value ? (int32_t)(max_value - min_value) : 1;
    const int32_t clamped = clamp_i32(value, min_value, max_value);
    if (quick_level_control_is_vertical(tile)) {
        const int32_t track_h = h - 34;
        const int32_t track_x = w - 20;
        const int32_t track_y = 24;
        int32_t fill_h = ((clamped - min_value) * track_h) / range;
        fill_h = clamp_i32(fill_h, 4, track_h);
        lv_obj_set_pos(label_obj, 8, 4);
        lv_obj_set_size(label_obj, w - 32, 18);
        lv_obj_set_pos(track, track_x, track_y);
        lv_obj_set_size(track, 8, track_h);
        lv_obj_set_pos(fill, track_x, track_y + track_h - fill_h);
        lv_obj_set_size(fill, 8, fill_h);
        lv_obj_set_pos(knob, track_x - 4, track_y + track_h - fill_h - 4);
    } else {
        const int32_t track_x = 8;
        const int32_t track_y = h - 18;
        const int32_t track_w = w - 16;
        int32_t fill_w = ((clamped - min_value) * track_w) / range;
        fill_w = clamp_i32(fill_w, 4, track_w);
        lv_obj_set_pos(label_obj, 8, 4);
        lv_obj_set_size(label_obj, w - 16, 18);
        lv_obj_set_pos(track, track_x, track_y);
        lv_obj_set_size(track, track_w, 8);
        lv_obj_set_pos(fill, track_x, track_y);
        lv_obj_set_size(fill, fill_w, 8);
        lv_obj_set_pos(knob, track_x + fill_w - 8, track_y - 4);
    }
}

static void set_quick_brightness_value(uint8_t brightness_percent, bool queue)
{
    const uint8_t clamped = clamp_brightness_percent(brightness_percent);
    s_ui.quick_brightness_percent = clamped;
    if (s_ui.quick_brightness_label) {
        label_set_text_fmt_if_changed(s_ui.quick_brightness_label, LV_SYMBOL_EYE_OPEN " %u%%", clamped);
    }
    layout_quick_level_control(s_ui.quick_brightness_tile,
                               s_ui.quick_brightness_label,
                               s_ui.quick_brightness_track,
                               s_ui.quick_brightness_fill,
                               s_ui.quick_brightness_knob,
                               clamped,
                               QUICK_BRIGHTNESS_MIN,
                               QUICK_BRIGHTNESS_MAX);
    if (queue) {
        s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS;
        s_ui.pending_event.brightness_percent_valid = true;
        s_ui.pending_event.brightness_percent = clamped;
    }
}

static void set_quick_volume_value(uint8_t volume_percent, bool queue)
{
    const uint8_t clamped = clamp_volume_percent(volume_percent);
    s_ui.quick_volume_percent = clamped;
    if (s_ui.quick_volume_label) {
        label_set_text_fmt_if_changed(s_ui.quick_volume_label, LV_SYMBOL_VOLUME_MID " %u%%", clamped);
    }
    layout_quick_level_control(s_ui.quick_volume_tile,
                               s_ui.quick_volume_label,
                               s_ui.quick_volume_track,
                               s_ui.quick_volume_fill,
                               s_ui.quick_volume_knob,
                               clamped,
                               QUICK_VOLUME_MIN,
                               QUICK_VOLUME_MAX);
    if (queue) {
        s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_SET_VOLUME;
        s_ui.pending_event.volume_percent_valid = true;
        s_ui.pending_event.volume_percent = clamped;
    }
}

static void refresh_quick_level_layouts(void)
{
    set_quick_brightness_value(s_ui.quick_brightness_percent ? s_ui.quick_brightness_percent : 85U, false);
    set_quick_volume_value(s_ui.quick_volume_percent, false);
}

static void set_quick_panel_open(bool open)
{
    s_ui.quick_panel_open = open;
    if (!open) {
        s_ui.quick_drag_obj = NULL;
        s_ui.quick_drag_moved = false;
        s_ui.quick_long_triggered = false;
        s_ui.quick_long_action_pending = ESP_BMS_LVGL_ACTION_NONE;
        set_quick_edit_mode(false);
    }
    set_obj_hidden(s_ui.quick_panel, !open);

    const bool settings_visible = s_ui.settings_page &&
                                  !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    set_obj_hidden(s_ui.quick_pull_zone, open || settings_visible);
    if (open) {
        lv_obj_move_foreground(s_ui.quick_panel);
    } else if (!settings_visible && s_ui.quick_pull_zone) {
        lv_obj_move_foreground(s_ui.quick_pull_zone);
    }
}

static void set_quick_edit_mode(bool edit_mode)
{
    s_ui.quick_edit_mode = edit_mode;
    if (s_ui.quick_edit_icon) {
        lv_obj_set_style_text_color(s_ui.quick_edit_icon, edit_mode ? COLOR_SOC : COLOR_MUTED, LV_PART_MAIN);
    }
    if (s_ui.quick_edit_button) {
        lv_obj_set_style_border_width(s_ui.quick_edit_button, edit_mode ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ui.quick_edit_button, COLOR_SOC, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_ui.quick_edit_button, LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void quick_edit_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        quick_tile_set_scale(s_ui.quick_edit_button, QUICK_TILE_SCALE_PRESSED);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        quick_tile_set_scale(s_ui.quick_edit_button, QUICK_TILE_SCALE_LONG);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        quick_tile_set_scale(s_ui.quick_edit_button, QUICK_TILE_SCALE_NORMAL);
    } else if (code == LV_EVENT_CLICKED) {
        set_quick_edit_mode(!s_ui.quick_edit_mode);
    }
}

static void perform_ui_action(esp_bms_lvgl_action_t action, bool close_quick_panel)
{
    if (close_quick_panel) {
        set_quick_panel_open(false);
    }

    if (action == ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU) {
        set_quick_panel_open(!s_ui.quick_panel_open);
        return;
    }

    queue_action(action);

    if (action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS) {
        show_settings_view();
    } else if (action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD) {
        show_dashboard_view();
    }
}

static void action_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    esp_bms_lvgl_action_t action = (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    perform_ui_action(action, true);
}

static bool process_return_swipe_event(lv_event_code_t code)
{
    if (code == LV_EVENT_PRESSED) {
        s_ui.return_swipe_tracking = get_active_pointer(&s_ui.return_swipe_start);
        return false;
    }

    if (code == LV_EVENT_PRESSING && s_ui.return_swipe_tracking) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return false;
        }

        const int32_t dx = point.x - s_ui.return_swipe_start.x;
        const int32_t dy = point.y - s_ui.return_swipe_start.y;
        if (dx >= RETURN_SWIPE_MIN_DX && abs_i32(dy) <= RETURN_SWIPE_MAX_DY) {
            s_ui.return_swipe_tracking = false;
            if (s_ui.quick_panel_open) {
                set_quick_panel_open(false);
            } else if (s_ui.settings_page &&
                       !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN)) {
                show_dashboard_view();
            } else if (s_ui.page != ESP_BMS_LVGL_PAGE_BATTERY) {
                move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, true);
            }
            lv_indev_wait_release(lv_indev_active());
            return true;
        }
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.return_swipe_tracking = false;
    }
    return false;
}

static void return_swipe_event_cb(lv_event_t *event)
{
    (void)process_return_swipe_event(lv_event_get_code(event));
}

static void quick_pull_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.quick_pull_tracking = get_active_pointer(&s_ui.quick_pull_start);
        return;
    }

    if (code == LV_EVENT_PRESSING && s_ui.quick_pull_tracking) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }

        const int32_t dx = point.x - s_ui.quick_pull_start.x;
        const int32_t dy = point.y - s_ui.quick_pull_start.y;
        if (dy >= QUICK_PULL_OPEN_DY && abs_i32(dx) <= QUICK_PULL_MAX_DX) {
            s_ui.quick_pull_tracking = false;
            set_quick_panel_open(true);
            lv_indev_wait_release(lv_indev_active());
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.quick_pull_tracking = false;
    }
}

static lv_obj_t *action_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                              const char *text, esp_bms_lvgl_action_t action)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_PANEL);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, return_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(box, return_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(box, return_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(box, return_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(box, action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    lv_obj_t *text_label = label(box, 4, 3, w - 8, h - 6, &lv_font_montserrat_14);
    lv_label_set_text(text_label, text);
    lv_obj_add_flag(text_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(text_label, return_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(text_label, return_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(text_label, return_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(text_label, return_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(text_label, action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    return box;
}

typedef enum {
    QUICK_ITEM_BLUETOOTH = 0,
    QUICK_ITEM_HOTSPOT,
    QUICK_ITEM_WIFI,
    QUICK_ITEM_ROTATE,
    QUICK_ITEM_SETTINGS,
} quick_panel_item_kind_t;

typedef struct {
    quick_panel_item_kind_t kind;
    const char *icon;
    esp_bms_lvgl_action_t click_action;
    esp_bms_lvgl_action_t long_action;
    bool hotspot_icon;
} quick_panel_item_t;

static const quick_panel_item_t QUICK_PANEL_ITEMS[QUICK_PANEL_BUTTON_COUNT] = {
    { QUICK_ITEM_BLUETOOTH, LV_SYMBOL_BLUETOOTH, ESP_BMS_LVGL_ACTION_START_BMS_BIND,
      ESP_BMS_LVGL_ACTION_SHOW_SETTINGS, false },
    { QUICK_ITEM_HOTSPOT, NULL, ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING,
      ESP_BMS_LVGL_ACTION_SHOW_SETTINGS, true },
    { QUICK_ITEM_WIFI, LV_SYMBOL_WIFI, ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING,
      ESP_BMS_LVGL_ACTION_SHOW_SETTINGS, false },
    { QUICK_ITEM_ROTATE, LV_SYMBOL_LOOP, ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY,
      ESP_BMS_LVGL_ACTION_NONE, false },
    { QUICK_ITEM_SETTINGS, LV_SYMBOL_SETTINGS, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      ESP_BMS_LVGL_ACTION_NONE, false },
};

static uint32_t quick_panel_item_index(const quick_panel_item_t *item)
{
    if (!item || item < QUICK_PANEL_ITEMS ||
        item >= QUICK_PANEL_ITEMS + QUICK_PANEL_BUTTON_COUNT) {
        return QUICK_PANEL_BUTTON_COUNT;
    }
    return (uint32_t)(item - QUICK_PANEL_ITEMS);
}

static lv_color_t quick_icon_color(bool active)
{
    return active ? COLOR_SOC : COLOR_TEXT;
}

static lv_color_t quick_bluetooth_symbol_color(bool active)
{
    return active ? COLOR_SOC : COLOR_WHITE;
}

static void quick_bluetooth_canvas_draw_line(lv_layer_t *layer,
                                             int32_t x0,
                                             int32_t y0,
                                             int32_t x1,
                                             int32_t y1,
                                             lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = 4;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;
    lv_draw_line(layer, &dsc);
}

static void quick_icon_tree_set_color(lv_obj_t *obj, lv_color_t color)
{
    if (!obj) {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(obj);
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    } else if (child_count == 0) {
        lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, color, LV_PART_MAIN);
    }
    for (uint32_t i = 0; i < child_count; ++i) {
        quick_icon_tree_set_color(lv_obj_get_child(obj, (int32_t)i), color);
    }
}

static void quick_bluetooth_icon_set_active(lv_obj_t *icon, bool active)
{
    if (!icon) {
        return;
    }
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    const lv_color_t color = quick_bluetooth_symbol_color(active);
    lv_canvas_fill_bg(icon, lv_color_black(), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(icon, &layer);
    quick_bluetooth_canvas_draw_line(&layer, 13, 3, 13, 29, color);
    quick_bluetooth_canvas_draw_line(&layer, 5, 10, 13, 16, color);
    quick_bluetooth_canvas_draw_line(&layer, 13, 16, 21, 8, color);
    quick_bluetooth_canvas_draw_line(&layer, 21, 8, 13, 3, color);
    quick_bluetooth_canvas_draw_line(&layer, 5, 22, 13, 16, color);
    quick_bluetooth_canvas_draw_line(&layer, 13, 16, 21, 24, color);
    quick_bluetooth_canvas_draw_line(&layer, 21, 24, 13, 29, color);
    lv_canvas_finish_layer(icon, &layer);
    lv_obj_invalidate(icon);
}

static void quick_icon_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, LV_PART_MAIN);
}

static void quick_icon_start_fill_animation(lv_obj_t *icon, lv_color_t color)
{
    if (!icon) {
        return;
    }
    quick_icon_tree_set_color(icon, color);
    lv_anim_delete(icon, quick_icon_opa_anim_cb);
    lv_obj_set_style_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, 180);
    lv_anim_set_exec_cb(&anim, quick_icon_opa_anim_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void quick_tile_set_scale(lv_obj_t *obj, int32_t scale)
{
    if (!obj) {
        return;
    }
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(obj, scale, LV_PART_MAIN);
}

static void quick_drag_begin(lv_obj_t *obj)
{
    if (!obj || !get_active_pointer(&s_ui.quick_drag_start)) {
        return;
    }
    s_ui.quick_drag_obj = obj;
    s_ui.quick_drag_obj_x = lv_obj_get_x(obj);
    s_ui.quick_drag_obj_y = lv_obj_get_y(obj);
    s_ui.quick_drag_moved = false;
}

static void quick_drag_update(void)
{
    if (!s_ui.quick_drag_obj) {
        return;
    }
    lv_point_t point = { 0 };
    if (!get_active_pointer(&point)) {
        return;
    }
    const int32_t dx = point.x - s_ui.quick_drag_start.x;
    const int32_t dy = point.y - s_ui.quick_drag_start.y;
    if (abs_i32(dx) > 3 || abs_i32(dy) > 3) {
        s_ui.quick_drag_moved = true;
    }
    const int32_t max_x = s_ui.width - lv_obj_get_width(s_ui.quick_drag_obj);
    const int32_t max_y = s_ui.height - lv_obj_get_height(s_ui.quick_drag_obj);
    lv_obj_set_pos(s_ui.quick_drag_obj,
                   clamp_i32(s_ui.quick_drag_obj_x + dx, 0, max_x),
                   clamp_i32(s_ui.quick_drag_obj_y + dy, 0, max_y));
}

static bool quick_drag_end(void)
{
    const bool moved = s_ui.quick_drag_moved;
    s_ui.quick_drag_obj = NULL;
    s_ui.quick_drag_moved = false;
    return moved;
}

static void quick_panel_item_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (process_return_swipe_event(code)) {
        return;
    }

    const quick_panel_item_t *item = (const quick_panel_item_t *)lv_event_get_user_data(event);
    if (!item) {
        return;
    }

    lv_obj_t *tile = (lv_obj_t *)lv_event_get_target(event);
    const uint32_t index = quick_panel_item_index(item);
    if (code == LV_EVENT_PRESSED) {
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
        if (s_ui.quick_edit_mode) {
            quick_drag_begin(tile);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING && s_ui.quick_edit_mode) {
        quick_drag_update();
        return;
    }

    if (code == LV_EVENT_LONG_PRESSED) {
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
        if (!s_ui.quick_edit_mode && item->long_action != ESP_BMS_LVGL_ACTION_NONE) {
            s_ui.quick_long_action_pending = item->long_action;
            s_ui.quick_long_triggered = true;
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        if (s_ui.quick_edit_mode) {
            (void)quick_drag_end();
            return;
        }
        if (s_ui.quick_long_triggered && code == LV_EVENT_RELEASED) {
            const esp_bms_lvgl_action_t action = s_ui.quick_long_action_pending;
            s_ui.quick_long_action_pending = ESP_BMS_LVGL_ACTION_NONE;
            if (action != ESP_BMS_LVGL_ACTION_NONE) {
                perform_ui_action(action, true);
            }
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (s_ui.quick_edit_mode || s_ui.quick_long_triggered) {
            s_ui.quick_long_triggered = false;
            s_ui.quick_long_action_pending = ESP_BMS_LVGL_ACTION_NONE;
            return;
        }
        if (index < QUICK_PANEL_BUTTON_COUNT) {
            if (item->kind == QUICK_ITEM_BLUETOOTH) {
                quick_bluetooth_icon_set_active(s_ui.quick_panel_item_icons[index],
                                                s_ui.quick_panel_item_active[index]);
                lv_anim_delete(s_ui.quick_panel_item_icons[index], quick_icon_opa_anim_cb);
                lv_obj_set_style_opa(s_ui.quick_panel_item_icons[index], LV_OPA_TRANSP, LV_PART_MAIN);
                lv_anim_t anim;
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, s_ui.quick_panel_item_icons[index]);
                lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
                lv_anim_set_duration(&anim, 180);
                lv_anim_set_exec_cb(&anim, quick_icon_opa_anim_cb);
                lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
                lv_anim_start(&anim);
            } else {
                quick_icon_start_fill_animation(s_ui.quick_panel_item_icons[index],
                                                quick_icon_color(s_ui.quick_panel_item_active[index]));
            }
        }
        const bool close_panel = item->click_action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS ||
                                 item->click_action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD;
        perform_ui_action(item->click_action, close_panel);
    }
}

static uint32_t quick_first_utf8_codepoint(const char *text)
{
    if (!text || !text[0]) {
        return 0;
    }

    const uint8_t *bytes = (const uint8_t *)text;
    if ((bytes[0] & 0x80U) == 0U) {
        return bytes[0];
    }
    if ((bytes[0] & 0xe0U) == 0xc0U && bytes[1]) {
        return ((uint32_t)(bytes[0] & 0x1fU) << 6) |
               (uint32_t)(bytes[1] & 0x3fU);
    }
    if ((bytes[0] & 0xf0U) == 0xe0U && bytes[1] && bytes[2]) {
        return ((uint32_t)(bytes[0] & 0x0fU) << 12) |
               ((uint32_t)(bytes[1] & 0x3fU) << 6) |
               (uint32_t)(bytes[2] & 0x3fU);
    }
    if ((bytes[0] & 0xf8U) == 0xf0U && bytes[1] && bytes[2] && bytes[3]) {
        return ((uint32_t)(bytes[0] & 0x07U) << 18) |
               ((uint32_t)(bytes[1] & 0x3fU) << 12) |
               ((uint32_t)(bytes[2] & 0x3fU) << 6) |
               (uint32_t)(bytes[3] & 0x3fU);
    }
    return 0;
}

static lv_obj_t *quick_symbol_icon(lv_obj_t *parent,
                                   int32_t content_w,
                                   int32_t content_h,
                                   const char *symbol,
                                   const lv_font_t *font)
{
    if (!font) {
        return NULL;
    }

    const int32_t label_w = content_w;
    const int32_t label_h = font->line_height;
    int32_t label_x = 0;
    int32_t label_y = (content_h - label_h) / 2;

    const uint32_t letter = quick_first_utf8_codepoint(symbol);
    lv_font_glyph_dsc_t glyph = { 0 };
    if (letter != 0 && lv_font_get_glyph_dsc(font, &glyph, letter, 0) &&
        glyph.box_w > 0 && glyph.box_h > 0) {
        const int32_t glyph_center_x2 = label_w - (int32_t)glyph.adv_w +
                                        (2 * (int32_t)glyph.ofs_x) + (int32_t)glyph.box_w;
        const int32_t glyph_top = (int32_t)font->line_height - (int32_t)font->base_line -
                                  (int32_t)glyph.box_h - (int32_t)glyph.ofs_y;
        const int32_t glyph_center_y2 = (2 * glyph_top) + (int32_t)glyph.box_h;
        label_x = (content_w - glyph_center_x2) / 2;
        label_y = (content_h - glyph_center_y2) / 2;
    }

    lv_obj_t *icon_label = label(parent, label_x, label_y, label_w, label_h, font);
    lv_label_set_text(icon_label, symbol ? symbol : "");
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon_label, LV_OPA_TRANSP, LV_PART_MAIN);
    return icon_label;
}

static lv_obj_t *quick_bluetooth_icon(lv_obj_t *parent, int32_t w, int32_t h)
{
    if (!s_quick_bluetooth_draw_buf_initialized) {
        LV_DRAW_BUF_INIT_STATIC(s_quick_bluetooth_draw_buf);
        s_quick_bluetooth_draw_buf_initialized = true;
    }

    const int32_t content_w = w - 8;
    const int32_t content_h = h - 8;
    lv_obj_t *icon = lv_canvas_create(parent);
    clear_style(icon);
    lv_canvas_set_draw_buf(icon, &s_quick_bluetooth_draw_buf);
    lv_obj_set_pos(icon,
                   (content_w - QUICK_BLUETOOTH_ICON_W) / 2,
                   (content_h - QUICK_BLUETOOTH_ICON_H) / 2);
    lv_obj_set_size(icon, QUICK_BLUETOOTH_ICON_W, QUICK_BLUETOOTH_ICON_H);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    quick_bluetooth_icon_set_active(icon, false);
    return icon;
}

static lv_obj_t *quick_hotspot_icon(lv_obj_t *parent, int32_t w, int32_t h)
{
    const int32_t content_w = w - 8;
    const int32_t content_h = h - 8;

    lv_obj_t *root = lv_obj_create(parent);
    clear_style(root);
    lv_obj_set_pos(root, (content_w - 44) / 2, (content_h - 42) / 2);
    lv_obj_set_size(root, 44, 42);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);

    (void)quick_symbol_icon(root, 44, 22, LV_SYMBOL_WIFI, &lv_font_montserrat_24);

    lv_obj_t *dot = lv_obj_create(root);
    clear_style(dot);
    lv_obj_set_pos(dot, 19, 20);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *base = lv_obj_create(root);
    clear_style(base);
    lv_obj_set_pos(base, 4, 28);
    lv_obj_set_size(base, 36, 12);
    lv_obj_set_style_radius(base, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(base, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, LV_PART_MAIN);
    return root;
}

static lv_obj_t *quick_panel_tile(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  int32_t w,
                                  int32_t h,
                                  uint32_t index,
                                  const quick_panel_item_t *item)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_PANEL_ALT);
    const int32_t content_w = w - 8;
    const int32_t content_h = h - 8;
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_PRESSED, (void *)item);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_PRESSING, (void *)item);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_RELEASED, (void *)item);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_PRESS_LOST, (void *)item);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_LONG_PRESSED, (void *)item);
    lv_obj_add_event_cb(box, quick_panel_item_event_cb, LV_EVENT_CLICKED, (void *)item);

    if (item->kind == QUICK_ITEM_BLUETOOTH) {
        s_ui.quick_panel_item_icons[index] = quick_bluetooth_icon(box, w, h);
    } else if (item->hotspot_icon) {
        s_ui.quick_panel_item_icons[index] = quick_hotspot_icon(box, w, h);
    } else {
        s_ui.quick_panel_item_icons[index] = quick_symbol_icon(box,
                                                               content_w,
                                                               content_h,
                                                               item->icon,
                                                               &lv_font_montserrat_24);
    }
    return box;
}

static bool quick_item_active_from_snapshot(const quick_panel_item_t *item,
                                            const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!item || !snapshot) {
        return false;
    }
    switch (item->kind) {
    case QUICK_ITEM_BLUETOOTH:
        return snapshot->bms_online;
    case QUICK_ITEM_HOTSPOT:
        return snapshot->setup_ap_enabled || snapshot->wifi == ESP_BMS_WIFI_SETUP_AP;
    case QUICK_ITEM_WIFI:
        return snapshot->wifi == ESP_BMS_WIFI_CONNECTING ||
               snapshot->wifi == ESP_BMS_WIFI_CONNECTED;
    case QUICK_ITEM_ROTATE:
    case QUICK_ITEM_SETTINGS:
    default:
        return false;
    }
}

static void update_quick_item_colors(const esp_bms_dashboard_snapshot_t *snapshot)
{
    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        const bool active = quick_item_active_from_snapshot(&QUICK_PANEL_ITEMS[index], snapshot);
        s_ui.quick_panel_item_active[index] = active;
        if (QUICK_PANEL_ITEMS[index].kind == QUICK_ITEM_BLUETOOTH) {
            quick_bluetooth_icon_set_active(s_ui.quick_panel_item_icons[index], active);
        } else {
            quick_icon_tree_set_color(s_ui.quick_panel_item_icons[index], quick_icon_color(active));
        }
        if (s_ui.quick_panel_item_icons[index]) {
            lv_obj_set_style_opa(s_ui.quick_panel_item_icons[index], LV_OPA_COVER, LV_PART_MAIN);
        }
    }
}

static lv_obj_t *quick_level_tile_for_kind(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_tile : s_ui.quick_brightness_tile;
}

static lv_obj_t *quick_level_track_for_kind(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_track : s_ui.quick_brightness_track;
}

static void quick_level_set_from_pointer(quick_level_kind_t kind)
{
    lv_point_t point = { 0 };
    lv_obj_t *track = quick_level_track_for_kind(kind);
    if (!get_active_pointer(&point) || !track) {
        return;
    }

    lv_area_t area = { 0 };
    lv_obj_get_coords(track, &area);
    lv_obj_t *tile = quick_level_tile_for_kind(kind);
    const int32_t min_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const int32_t max_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t range = max_value - min_value;
    int32_t percent = min_value;
    if (quick_level_control_is_vertical(tile)) {
        const int32_t track_h = area.y2 - area.y1 + 1;
        const int32_t offset = area.y2 - point.y;
        percent = min_value + ((offset * range) / (track_h > 1 ? track_h - 1 : 1));
    } else {
        const int32_t track_w = area.x2 - area.x1 + 1;
        const int32_t offset = point.x - area.x1;
        percent = min_value + ((offset * range) / (track_w > 1 ? track_w - 1 : 1));
    }

    if (kind == QUICK_LEVEL_VOLUME) {
        set_quick_volume_value(clamp_volume_percent(percent), true);
    } else {
        set_quick_brightness_value(clamp_brightness_percent(percent), true);
    }
}

static void quick_level_event_cb(lv_event_t *event)
{
    const quick_level_kind_t kind = (quick_level_kind_t)(uintptr_t)lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *tile = quick_level_tile_for_kind(kind);
    if (!tile) {
        return;
    }

    if (s_ui.quick_edit_mode) {
        if (code == LV_EVENT_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
            quick_drag_begin(tile);
        } else if (code == LV_EVENT_PRESSING) {
            quick_drag_update();
        } else if (code == LV_EVENT_LONG_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
            const bool moved = quick_drag_end();
            if (code == LV_EVENT_RELEASED && !moved) {
                refresh_quick_level_layouts();
            }
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
        quick_level_set_from_pointer(kind);
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        quick_level_set_from_pointer(kind);
        if (code == LV_EVENT_RELEASED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
    }
}

static lv_obj_t *quick_level_tile(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  int32_t w,
                                  int32_t h,
                                  quick_level_kind_t kind,
                                  uint8_t value)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_PANEL_ALT);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)kind);

    lv_obj_t *level_label = label(box, 8, 4, w - 16, 18, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(level_label, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_align(level_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    lv_obj_t *track = lv_obj_create(box);
    clear_style(track);
    lv_obj_set_style_radius(track, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(track, COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(track, quick_level_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(track, quick_level_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(track, quick_level_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);

    lv_obj_t *fill = lv_obj_create(box);
    clear_style(fill);
    lv_obj_set_style_radius(fill, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fill, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(fill, quick_level_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(fill, quick_level_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(fill, quick_level_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);

    lv_obj_t *knob = lv_obj_create(box);
    clear_style(knob);
    lv_obj_set_size(knob, 16, 16);
    lv_obj_set_style_radius(knob, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(knob, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(knob, quick_level_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(knob, quick_level_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(knob, quick_level_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);

    if (kind == QUICK_LEVEL_VOLUME) {
        s_ui.quick_volume_tile = box;
        s_ui.quick_volume_label = level_label;
        s_ui.quick_volume_track = track;
        s_ui.quick_volume_fill = fill;
        s_ui.quick_volume_knob = knob;
        set_quick_volume_value(value, false);
    } else {
        s_ui.quick_brightness_tile = box;
        s_ui.quick_brightness_label = level_label;
        s_ui.quick_brightness_track = track;
        s_ui.quick_brightness_fill = fill;
        s_ui.quick_brightness_knob = knob;
        set_quick_brightness_value(value, false);
    }
    return box;
}

static const char *wifi_text(esp_bms_wifi_state_t state)
{
    switch (state) {
    case ESP_BMS_WIFI_SETUP_AP:
        return "AP";
    case ESP_BMS_WIFI_CONNECTING:
        return "WIFI...";
    case ESP_BMS_WIFI_CONNECTED:
        return "WIFI";
    case ESP_BMS_WIFI_OFFLINE:
    default:
        return "OFF";
    }
}

static const char *ota_text(esp_bms_ota_state_t state)
{
    switch (state) {
    case ESP_BMS_OTA_CHECKING:
        return "OTA CHK";
    case ESP_BMS_OTA_AVAILABLE:
        return "OTA NEW";
    case ESP_BMS_OTA_DOWNLOADING:
        return "OTA DL";
    case ESP_BMS_OTA_VERIFYING:
        return "OTA VFY";
    case ESP_BMS_OTA_READY:
        return "OTA RDY";
    case ESP_BMS_OTA_FAILED:
        return "OTA ERR";
    case ESP_BMS_OTA_IDLE:
    default:
        return "OTA IDLE";
    }
}

static void format_mv(char *out, size_t len, bool valid, uint32_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%lu.%02luV", (unsigned long)(mv / 1000), (unsigned long)((mv % 1000) / 10));
}

static void format_deci_amps(char *out, size_t len, bool valid, int16_t deci_amps)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    const char sign = deci_amps < 0 ? '-' : '+';
    uint16_t abs_value = deci_amps < 0 ? (uint16_t)(-deci_amps) : (uint16_t)deci_amps;
    snprintf(out, len, "%c%u.%uA", sign, abs_value / 10, abs_value % 10);
}

static void format_cell_v(char *out, size_t len, bool valid, uint16_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%u.%03u", mv / 1000, mv % 1000);
}

static void format_temp_c(char *out, size_t len, bool valid, int16_t celsius)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%dC", (int)celsius);
}

static void set_header(const esp_bms_dashboard_snapshot_t *snapshot)
{
    label_set_text_color_if_changed(s_ui.gps_state, snapshot->gps_fix_valid ? COLOR_ACCENT : COLOR_WARN);
    label_set_text_if_changed(s_ui.gps_state, snapshot->gps_fix_valid ? "GPS OK" : "GPS --");

    label_set_text_color_if_changed(s_ui.bms_state, snapshot->bms_online ? COLOR_ACCENT : COLOR_BAD);
    label_set_text_if_changed(s_ui.bms_state, snapshot->bms_online ? "BMS OK" : "BMS OFF");

    label_set_text_color_if_changed(s_ui.wifi_state,
                                    snapshot->wifi == ESP_BMS_WIFI_OFFLINE ? COLOR_WARN : COLOR_ACCENT);
    label_set_text_if_changed(s_ui.wifi_state, wifi_text(snapshot->wifi));

    label_set_text_color_if_changed(s_ui.ota_state, snapshot->ota == ESP_BMS_OTA_FAILED ? COLOR_BAD : COLOR_MUTED);
    label_set_text_if_changed(s_ui.ota_state, ota_text(snapshot->ota));
}

static void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot)
{
    const char *ssid = snapshot->setup_ap_ssid[0] != '\0' ? snapshot->setup_ap_ssid : "--";
    const char *password = snapshot->setup_ap_password[0] != '\0' ? snapshot->setup_ap_password : "--";
    label_set_text_fmt_if_changed(s_ui.setup_ap_info, "SETUP %s\nSSID %.31s\nPW %.8s",
                                  snapshot->setup_ap_enabled ? "ON" : "OFF",
                                  ssid,
                                  password);

#if LV_USE_QRCODE
    if (s_ui.setup_ap_qr) {
        if (snapshot->setup_ap_qr_payload[0] == '\0') {
            s_ui.current_setup_ap_qr_payload[0] = '\0';
            lv_obj_add_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(s_ui.current_setup_ap_qr_payload, snapshot->setup_ap_qr_payload) == 0) {
            lv_obj_clear_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else if (lv_qrcode_update(s_ui.setup_ap_qr,
                                    snapshot->setup_ap_qr_payload,
                                    strlen(snapshot->setup_ap_qr_payload)) == LV_RESULT_OK) {
            snprintf(s_ui.current_setup_ap_qr_payload, sizeof(s_ui.current_setup_ap_qr_payload), "%s",
                     snapshot->setup_ap_qr_payload);
            lv_obj_clear_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else {
            s_ui.current_setup_ap_qr_payload[0] = '\0';
            lv_obj_add_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
}

static void set_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    char voltage[24];
    char current[24];
    char ah[40];
    char min_cell[16];
    char avg_cell[16];
    char max_cell[16];
    char t1[8];
    char t2[8];
    char t3[8];
    char t4[8];
    char mos[8];
    char bal[8];

    if (snapshot->soc_valid) {
        const uint16_t soc = snapshot->soc_percent > 100 ? 100 : snapshot->soc_percent;
        if (snapshot->capacity_remaining_valid && snapshot->total_capacity_valid) {
            (void)snprintf(ah,
                           sizeof(ah),
                           "%lu.%02lu/%lu.%02luAh",
                           (unsigned long)(snapshot->capacity_remaining_mah / 1000U),
                           (unsigned long)((snapshot->capacity_remaining_mah % 1000U) / 10U),
                           (unsigned long)(snapshot->total_capacity_mah / 1000U),
                           (unsigned long)((snapshot->total_capacity_mah % 1000U) / 10U));
        } else {
            (void)snprintf(ah, sizeof(ah), "--/--Ah");
        }
        label_set_text_fmt_if_changed(s_ui.soc, "%u%%\n%s", soc, ah);
    } else {
        (void)snprintf(ah, sizeof(ah), "--/--Ah");
        label_set_text_if_changed(s_ui.soc, "--\n--/--Ah");
    }

    format_mv(voltage, sizeof(voltage), snapshot->pack_voltage_valid, snapshot->pack_voltage_mv);
    format_deci_amps(current, sizeof(current), snapshot->current_valid, snapshot->current_deci_amps);
    label_set_text_fmt_if_changed(s_ui.pack_voltage, "PACK\n%sV\n%s", voltage, current);

    format_cell_v(min_cell, sizeof(min_cell), snapshot->min_cell_valid, snapshot->min_cell_voltage_mv);
    format_cell_v(avg_cell, sizeof(avg_cell), snapshot->average_cell_valid, snapshot->average_cell_voltage_mv);
    format_cell_v(max_cell, sizeof(max_cell), snapshot->max_cell_valid, snapshot->max_cell_voltage_mv);
    if (snapshot->delta_cell_valid) {
        label_set_text_fmt_if_changed(s_ui.cell_stats, "MAX  %s\nMIN  %s\nDIFF %umV\nAVG  %s",
                                      max_cell, min_cell, snapshot->delta_cell_voltage_mv, avg_cell);
    } else {
        label_set_text_fmt_if_changed(s_ui.cell_stats, "MAX  %s\nMIN  %s\nDIFF --\nAVG  %s",
                                      max_cell, min_cell, avg_cell);
    }

    if (snapshot->bms_protection_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_BAD);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\nPROT %.7s", snapshot->bms_protection_codes[0]);
    } else if (snapshot->bms_warning_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_WARN);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\nWARN %.7s", snapshot->bms_warning_codes[0]);
    } else if (snapshot->bms_info_text[0] != '\0') {
        label_set_text_color_if_changed(s_ui.bms_error, snapshot->bms_online ? COLOR_ACCENT : COLOR_WARN);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\n%.15s", snapshot->bms_info_text);
    } else {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_MUTED);
        label_set_text_if_changed(s_ui.bms_error, "BMS INFO\nOK");
    }

    format_temp_c(t1, sizeof(t1), snapshot->bms_temperature_valid[0], snapshot->bms_temperature_celsius[0]);
    format_temp_c(t2, sizeof(t2), snapshot->bms_temperature_valid[1], snapshot->bms_temperature_celsius[1]);
    format_temp_c(t3, sizeof(t3), snapshot->bms_temperature_valid[2], snapshot->bms_temperature_celsius[2]);
    format_temp_c(t4, sizeof(t4), snapshot->bms_temperature_valid[3], snapshot->bms_temperature_celsius[3]);
    format_temp_c(mos, sizeof(mos), snapshot->bms_temperature_valid[4], snapshot->bms_temperature_celsius[4]);
    format_temp_c(bal, sizeof(bal), snapshot->bms_temperature_valid[5], snapshot->bms_temperature_celsius[5]);
    label_set_text_fmt_if_changed(s_ui.temperature, "T1   T2   T3   T4   BAL  MOS\n%-4s %-4s %-4s %-4s %-4s %-4s",
                                  t1, t2, t3, t4, bal, mos);

    set_setup_ap(snapshot);
    set_quick_brightness_value(snapshot->brightness_percent, false);
    set_quick_volume_value(snapshot->volume_percent, false);
    update_quick_item_colors(snapshot);
}

static void apply_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    set_header(snapshot);
    set_dashboard(snapshot);
}

static void defer_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    memcpy(&s_ui.deferred_snapshot, snapshot, sizeof(s_ui.deferred_snapshot));
    s_ui.deferred_snapshot_valid = true;
}

static void flush_deferred_dashboard_snapshot(void)
{
    if (!s_ui.deferred_snapshot_valid) {
        return;
    }

    esp_bms_dashboard_snapshot_t snapshot;
    memcpy(&snapshot, &s_ui.deferred_snapshot, sizeof(snapshot));
    s_ui.deferred_snapshot_valid = false;
    apply_dashboard_snapshot(&snapshot);
}

static void invalidate_dashboard_viewport(void)
{
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE
    if (!s_ui.root) {
        return;
    }

    const lv_area_t area = {
        .x1 = 0,
        .y1 = 0,
        .x2 = s_ui.width - 1,
        .y2 = s_ui.height - 1,
    };
    lv_obj_invalidate_area(s_ui.root, &area);
#endif
}

static int32_t page_target_scroll_x(esp_bms_lvgl_page_t page)
{
    return page == ESP_BMS_LVGL_PAGE_GPS ? s_ui.width : 0;
}

static esp_bms_lvgl_page_t page_from_scroll_x(int32_t scroll_x)
{
    return scroll_x >= (s_ui.width / 2) ? ESP_BMS_LVGL_PAGE_GPS : ESP_BMS_LVGL_PAGE_BATTERY;
}

static void finish_page_scroll_state(bool flush_snapshot)
{
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
        s_ui.page = page_from_scroll_x(lv_obj_get_scroll_x(s_ui.pages));
        lv_obj_scroll_to_x(s_ui.pages, page_target_scroll_x(s_ui.page), LV_ANIM_OFF);
        invalidate_dashboard_viewport();
    }

    s_ui.dragging = false;
    s_ui.settling = false;
    if (flush_snapshot) {
        flush_deferred_dashboard_snapshot();
    }
}

static void move_to_page(esp_bms_lvgl_page_t page, bool animated)
{
    lv_obj_stop_scroll_anim(s_ui.pages);
    const int32_t target_x = page_target_scroll_x(page);
    const int32_t current_x = lv_obj_get_scroll_x(s_ui.pages);
    if (current_x == target_x) {
        s_ui.page = page;
        s_ui.dragging = false;
        s_ui.settling = false;
        flush_deferred_dashboard_snapshot();
        return;
    }

    s_ui.page = page;
    s_ui.dragging = false;
    s_ui.settling = animated;
    lv_obj_scroll_to_x(s_ui.pages, target_x, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animated) {
        flush_deferred_dashboard_snapshot();
    }
}

static void page_scroll_event_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_ui.pages) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.drag_start_pages_x = lv_obj_get_scroll_x(s_ui.pages);
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] press scroll_x=%ld page=%d",
                 (long)s_ui.drag_start_pages_x,
                 (int)s_ui.page);
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL_BEGIN) {
        s_ui.dragging = true;
        s_ui.drag_start_pages_x = lv_obj_get_scroll_x(s_ui.pages);
        s_ui.drag_last_sample_log_ms = lv_tick_get();
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] scroll_begin scroll_x=%ld page=%d",
                 (long)s_ui.drag_start_pages_x,
                 (int)s_ui.page);
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL) {
        invalidate_dashboard_viewport();
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_SAMPLE_DIAGNOSTICS
        if (lv_tick_elaps(s_ui.drag_last_sample_log_ms) >= CONFIG_ESP_BMS_LVGL_UI_DRAG_SAMPLE_PERIOD_MS) {
            s_ui.drag_last_sample_log_ms = lv_tick_get();
            ESP_LOGI(TAG, "[drag] sample scroll_x=%ld from=%ld page=%d",
                     (long)lv_obj_get_scroll_x(s_ui.pages),
                     (long)s_ui.drag_start_pages_x,
                     (int)s_ui.page);
        }
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL_END) {
        const int32_t scroll_x = lv_obj_get_scroll_x(s_ui.pages);
        const esp_bms_lvgl_page_t target = page_from_scroll_x(scroll_x);
        s_ui.dragging = false;
        s_ui.settling = false;
        s_ui.page = target;
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] scroll_end scroll_x=%ld target=%d",
                 (long)scroll_x,
                 (int)target);
#endif
        flush_deferred_dashboard_snapshot();
    }
}

static void create_screen(lv_display_t *display)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    clear_style(screen);
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_display_set_default(display);
    lv_screen_load(screen);

    s_ui.display = display;
    s_ui.root = screen;
    s_ui.width = lv_display_get_horizontal_resolution(display);
    s_ui.height = lv_display_get_vertical_resolution(display);
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t page_h = s_ui.height;
    const int32_t settings_y = 0;
    const int32_t settings_h = s_ui.height - settings_y;
    const int32_t content_w = s_ui.width - 16;

    s_ui.header = panel(screen, 0, 0, s_ui.width, 20, COLOR_BG);
    lv_obj_set_style_radius(s_ui.header, 0, LV_PART_MAIN);
    s_ui.settings_button = label(s_ui.header, 6, 2, 34, 16, &lv_font_montserrat_14);
    lv_label_set_text(s_ui.settings_button, "SET");
    lv_obj_set_style_text_color(s_ui.settings_button, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.settings_button, action_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)ESP_BMS_LVGL_ACTION_SHOW_SETTINGS);
    if (portrait) {
        s_ui.gps_state = label(s_ui.header, 42, 2, 46, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 92, 2, 56, 16, &lv_font_montserrat_14);
        s_ui.wifi_state = label(s_ui.header, 152, 2, 40, 16, &lv_font_montserrat_14);
        s_ui.ota_state = label(s_ui.header, 196, 2, 42, 16, &lv_font_montserrat_14);
    } else {
        s_ui.gps_state = label(s_ui.header, 48, 2, 50, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 104, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.wifi_state = label(s_ui.header, 166, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.ota_state = label(s_ui.header, 222, 2, 92, 16, &lv_font_montserrat_14);
    }

    s_ui.pages = lv_obj_create(screen);
    clear_style(s_ui.pages);
    lv_obj_set_pos(s_ui.pages, 0, 0);
    lv_obj_set_size(s_ui.pages, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.pages, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.pages, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                  LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                  LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(s_ui.pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_ui.pages, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_snap_y(s_ui.pages, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scrollbar_mode(s_ui.pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_END, NULL);

    s_ui.battery_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.battery_page);
    lv_obj_set_pos(s_ui.battery_page, 0, 0);
    lv_obj_set_size(s_ui.battery_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.battery_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.battery_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.battery_page, LV_OBJ_FLAG_SNAPPABLE);

    s_ui.gps_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.gps_page);
    lv_obj_set_pos(s_ui.gps_page, s_ui.width, 0);
    lv_obj_set_size(s_ui.gps_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.gps_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.gps_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.gps_page, LV_OBJ_FLAG_SNAPPABLE);

    if (portrait) {
        s_ui.soc = panel_label(s_ui.battery_page, 8, 8, 108, 112, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        lv_obj_set_style_bg_color(lv_obj_get_parent(s_ui.soc), COLOR_SOC, LV_PART_MAIN);
        s_ui.pack_voltage = panel_label(s_ui.battery_page, 124, 8, 108, 112, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.bms_error = panel_label(s_ui.battery_page, 8, 128, 108, 120, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.cell_stats = panel_label(s_ui.battery_page, 124, 128, 108, 120, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.temperature = panel_label(s_ui.battery_page, 8, 256, content_w, 56, COLOR_PANEL, &lv_font_montserrat_14);
    } else {
        s_ui.soc = panel_label(s_ui.battery_page, 8, 8, 148, 84, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        lv_obj_set_style_bg_color(lv_obj_get_parent(s_ui.soc), COLOR_SOC, LV_PART_MAIN);
        s_ui.pack_voltage = panel_label(s_ui.battery_page, 164, 8, 148, 84, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.bms_error = panel_label(s_ui.battery_page, 8, 100, 148, 70, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.cell_stats = panel_label(s_ui.battery_page, 164, 100, 148, 70, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.temperature = panel_label(s_ui.battery_page, 8, 178, 304, 54, COLOR_PANEL, &lv_font_montserrat_14);
    }
    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.temperature, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_ui.settings_page = lv_obj_create(screen);
    clear_style(s_ui.settings_page);
    lv_obj_set_pos(s_ui.settings_page, 0, settings_y);
    lv_obj_set_size(s_ui.settings_page, s_ui.width, settings_h);
    lv_obj_set_style_bg_color(s_ui.settings_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.settings_page, return_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.settings_page, return_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.settings_page, return_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.settings_page, return_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);

    const int32_t action_w = portrait ? 88 : 84;
    const int32_t setup_info_x = action_w + 16;
    const int32_t setup_info_w = s_ui.width - setup_info_x - 8;
    const int32_t setup_qr_size = setup_info_w < 128 ? setup_info_w : 128;
    action_panel(s_ui.settings_page, 8, 8, action_w, 28, "BACK", ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD);
    action_panel(s_ui.settings_page, 8, 44, action_w, 22, "SETUP AP", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING);
    action_panel(s_ui.settings_page, 8, 70, action_w, 22, "BRIGHT", ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS);
    action_panel(s_ui.settings_page, 8, 96, action_w, 22, "ROTATE", ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY);
    action_panel(s_ui.settings_page, 8, 122, action_w, 22, "SPEED", ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT);
    action_panel(s_ui.settings_page, 8, 148, action_w, 22, "LANG", ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE);
    action_panel(s_ui.settings_page, 8, 174, action_w, 22, "BMS", ESP_BMS_LVGL_ACTION_START_BMS_BIND);
    action_panel(s_ui.settings_page, 8, 200, action_w, 22, "RESTORE", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS);
    s_ui.setup_ap_info = panel_label(s_ui.settings_page, setup_info_x, 8, setup_info_w, 68,
                                     COLOR_PANEL_ALT, &lv_font_montserrat_14);
    s_ui.setup_ap_qr = setup_ap_qr(s_ui.settings_page, setup_info_x, 84, setup_qr_size);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);

    const int32_t quick_cols = QUICK_PANEL_BUTTON_COUNT;
    const int32_t quick_gap = 8;
    const int32_t quick_pad = 16;
    const int32_t quick_rows = 2;
    const int32_t quick_tile_h = portrait ? 56 : 64;
    const int32_t quick_tile_w = (s_ui.width - (quick_pad * 2) - ((quick_cols - 1) * quick_gap)) / quick_cols;
    const int32_t quick_grid_w = (quick_cols * quick_tile_w) + ((quick_cols - 1) * quick_gap);
    const int32_t quick_grid_h = (quick_rows * quick_tile_h) + ((quick_rows - 1) * quick_gap);
    const int32_t quick_left = (s_ui.width - quick_grid_w) / 2;
    const int32_t quick_top = (s_ui.height - quick_grid_h) / 2;

    s_ui.quick_panel = lv_obj_create(screen);
    clear_style(s_ui.quick_panel);
    lv_obj_set_pos(s_ui.quick_panel, 0, 0);
    lv_obj_set_size(s_ui.quick_panel, s_ui.width, s_ui.height);
    lv_obj_set_style_radius(s_ui.quick_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_panel, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_panel, return_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_panel, return_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_panel, return_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_panel, return_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);

    const int32_t control_w = (quick_grid_w - quick_gap) / 2;
    (void)quick_level_tile(s_ui.quick_panel,
                           quick_left,
                           quick_top,
                           control_w,
                           quick_tile_h,
                           QUICK_LEVEL_BRIGHTNESS,
                           85U);
    (void)quick_level_tile(s_ui.quick_panel,
                           quick_left + control_w + quick_gap,
                           quick_top,
                           control_w,
                           quick_tile_h,
                           QUICK_LEVEL_VOLUME,
                           65U);

    s_ui.quick_edit_button = panel(s_ui.quick_panel, s_ui.width - 34, 8, 26, 24, COLOR_PANEL_ALT);
    lv_obj_add_flag(s_ui.quick_edit_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.quick_edit_icon = quick_symbol_icon(s_ui.quick_edit_button, 18, 16, LV_SYMBOL_EDIT, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_ui.quick_edit_icon, COLOR_MUTED, LV_PART_MAIN);

    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        const quick_panel_item_t *item = &QUICK_PANEL_ITEMS[index];
        const int32_t tile_x = quick_left + ((int32_t)index * (quick_tile_w + quick_gap));
        const int32_t tile_y = quick_top + quick_tile_h + quick_gap;
        s_ui.quick_panel_items[index] = quick_panel_tile(s_ui.quick_panel,
                                                         tile_x,
                                                         tile_y,
                                                         quick_tile_w,
                                                         quick_tile_h,
                                                         index,
                                                         item);
    }
    set_quick_edit_mode(false);
    lv_obj_add_flag(s_ui.quick_panel, LV_OBJ_FLAG_HIDDEN);

    const int32_t pull_w = portrait ? 116 : 140;
    const int32_t pull_h = 34;
    s_ui.quick_pull_zone = lv_obj_create(screen);
    clear_style(s_ui.quick_pull_zone);
    lv_obj_set_pos(s_ui.quick_pull_zone, (s_ui.width - pull_w) / 2, 0);
    lv_obj_set_size(s_ui.quick_pull_zone, pull_w, pull_h);
    lv_obj_set_style_bg_opa(s_ui.quick_pull_zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_pull_zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_pull_zone, quick_pull_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_pull_zone, quick_pull_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_pull_zone, quick_pull_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_pull_zone, quick_pull_event_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *pull_handle = lv_obj_create(s_ui.quick_pull_zone);
    clear_style(pull_handle);
    lv_obj_set_pos(pull_handle, (pull_w - 36) / 2, 3);
    lv_obj_set_size(pull_handle, 36, 4);
    lv_obj_set_style_radius(pull_handle, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pull_handle, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pull_handle, LV_OPA_COVER, LV_PART_MAIN);

    set_quick_panel_open(false);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
}

static esp_err_t rebuild_screen_if_resolution_changed(void)
{
    ESP_RETURN_ON_FALSE(s_ui.display, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    const int32_t width = lv_display_get_horizontal_resolution(s_ui.display);
    const int32_t height = lv_display_get_vertical_resolution(s_ui.display);
    if (width == s_ui.width && height == s_ui.height) {
        return ESP_OK;
    }

    lv_obj_t *old_root = s_ui.root;
    const esp_bms_lvgl_page_t page = s_ui.page;
    const esp_bms_lvgl_action_event_t pending_event = s_ui.pending_event;
    const bool settings_visible = s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_display_t *display = s_ui.display;

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.display = display;
    s_ui.pending_event = pending_event;
    s_ui.initialized = true;
    create_screen(display);
    move_to_page(page, false);
    if (settings_visible) {
        show_settings_view();
    } else {
        show_dashboard_view();
    }
    if (old_root) {
        lv_obj_delete(old_root);
    }
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is required");
    ESP_RETURN_ON_FALSE(!s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI already initialized");
    create_screen(display);
    s_ui.initialized = true;
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_ERROR(rebuild_screen_if_resolution_changed(), TAG, "rebuild UI after resolution change failed");
    if (s_ui.dragging || s_ui.settling) {
        defer_dashboard_snapshot(snapshot);
        return ESP_OK;
    }

    apply_dashboard_snapshot(snapshot);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated)
{
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_FALSE(page == ESP_BMS_LVGL_PAGE_BATTERY || page == ESP_BMS_LVGL_PAGE_GPS,
                        ESP_ERR_INVALID_ARG, TAG, "invalid page");

    move_to_page(page, animated);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_take_action_event(esp_bms_lvgl_action_event_t *event)
{
    ESP_RETURN_ON_FALSE(event, ESP_ERR_INVALID_ARG, TAG, "action event output is required");
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");

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
