#include "esp_bms_lvgl_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_lvgl_contract.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "widgets/canvas/lv_canvas.h"
#include "widgets/line/lv_line.h"

static const char *TAG = "bms_lvgl_ui";

LV_FONT_DECLARE(bluetoothon);
LV_FONT_DECLARE(wlanJZ);
LV_FONT_DECLARE(settings_zh_16);

#define QUICK_PANEL_BUTTON_COUNT 6
#define QUICK_PANEL_GRID_COLS 4
#define QUICK_PANEL_GRID_ROWS 2
#define QUICK_PANEL_GRID_SLOT_COUNT (QUICK_PANEL_GRID_COLS * QUICK_PANEL_GRID_ROWS)
#define QUICK_PANEL_CONTROL_COUNT (QUICK_PANEL_BUTTON_COUNT + 2)
#define QUICK_EDIT_BUTTON_SIZE 28
#define SETTINGS_OPTION_COUNT 6
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define QUICK_BLUETOOTH_SYMBOL "\xee\x9c\xa8"
#define QUICK_HOTSPOT_SYMBOL "\xee\x98\xab"
#define QUICK_PULL_OPEN_DY 34
#define QUICK_PULL_MAX_DX 64
#define QUICK_PULL_ZONE_PORTRAIT_H 88
#define QUICK_PULL_ZONE_LANDSCAPE_H 64
#define QUICK_PULL_START_MAX_Y_NUM 4
#define QUICK_PULL_START_MAX_Y_DEN 5
#define QUICK_PANEL_SETTLE_MS 140
#define RETURN_HOME_SWIPE_MIN_DY 58
#define RETURN_HOME_SWIPE_MAX_DX 46
#define RETURN_HOME_START_MIN_Y_NUM 3
#define RETURN_HOME_START_MIN_Y_DEN 4
#define RETURN_HOME_RIGHT_CANCEL_MIN_DX 42
#define RETURN_HOME_RIGHT_CANCEL_MAX_DY 42
#define SETTINGS_SWIPE_BACK_MIN_DX 54
#define SETTINGS_SWIPE_BACK_MAX_DY 42
#define QUICK_BRIGHTNESS_MIN 10
#define QUICK_BRIGHTNESS_MAX 100
#define QUICK_VOLUME_MIN 0
#define QUICK_VOLUME_MAX 100
#define QUICK_LEVEL_PRESET_COUNT 4U
#define QUICK_LEVEL_DRAG_STEP 5
#define QUICK_TOAST_MS 950
#define QUICK_TOAST_SORT_HINT "HOLD TO SORT"
#define QUICK_PANEL_ITEM_COUNT QUICK_PANEL_BUTTON_COUNT
#define QUICK_TILE_PRESS_INSET 4
#define QUICK_TILE_SCALE_NORMAL 256
#define QUICK_TILE_SCALE_PRESSED 270
#define QUICK_TILE_SCALE_LONG 292
#define DASHBOARD_CELL_STAT_COUNT 4U
#define DASHBOARD_CELL_KEY_BITMAP_W 28
#define DASHBOARD_CELL_KEY_BITMAP_H 16
#define DASHBOARD_CELL_KEY_BITMAP_BYTES \
    (((DASHBOARD_CELL_KEY_BITMAP_W * DASHBOARD_CELL_KEY_BITMAP_H) + 7U) / 8U)
#define DASHBOARD_SOC_WAVE_PERIOD 32
#define DASHBOARD_SOC_WAVE_WIDTH 192
#define DASHBOARD_SOC_WAVE_HEIGHT 18

typedef enum {
    QUICK_LAYOUT_PORTRAIT = 0,
    QUICK_LAYOUT_LANDSCAPE = 1,
    QUICK_LAYOUT_COUNT = 2,
} quick_layout_orientation_t;

_Static_assert(QUICK_PANEL_CONTROL_COUNT == QUICK_PANEL_GRID_SLOT_COUNT,
               "quick panel defaults must fill a 2x4 grid");

typedef enum {
    QUICK_DRAG_TARGET_NONE = 0,
    QUICK_DRAG_TARGET_BRIGHTNESS,
    QUICK_DRAG_TARGET_VOLUME,
    QUICK_DRAG_TARGET_ITEM,
} quick_drag_target_kind_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} quick_tile_rect_t;

typedef struct {
    bool valid;
    bool tools_vertical;
    quick_tile_rect_t brightness;
    quick_tile_rect_t volume;
    quick_tile_rect_t items[QUICK_PANEL_BUTTON_COUNT];
} quick_panel_layout_t;

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
    lv_obj_t *settings_root;
    lv_obj_t *settings_detail;
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
    lv_obj_t *quick_level_overlay;
    lv_obj_t *quick_level_overlay_value;
    lv_obj_t *quick_level_overlay_track;
    lv_obj_t *quick_level_overlay_fill;
    lv_obj_t *quick_level_overlay_knob;
    lv_obj_t *quick_toast;
    lv_timer_t *quick_toast_timer;
    lv_obj_t *quick_edit_button;
    lv_obj_t *quick_edit_icon;
    lv_obj_t *quick_panel_item_icons[QUICK_PANEL_BUTTON_COUNT];
    bool quick_panel_item_active[QUICK_PANEL_BUTTON_COUNT];
    bool quick_panel_item_local_active[QUICK_PANEL_BUTTON_COUNT];
    bool quick_panel_item_local_override[QUICK_PANEL_BUTTON_COUNT];
    quick_panel_layout_t quick_layouts[QUICK_LAYOUT_COUNT];

    lv_obj_t *speed;
    lv_obj_t *gps_state;
    lv_obj_t *bms_state;
    lv_obj_t *wifi_state;
    lv_obj_t *ota_state;
    lv_obj_t *soc;
    lv_obj_t *soc_fill;
    lv_obj_t *soc_wave;
    lv_obj_t *pack_voltage;
    lv_obj_t *current;
    lv_obj_t *capacity;
    lv_obj_t *cell_stats;
    lv_obj_t *cell_stat_values[DASHBOARD_CELL_STAT_COUNT];
    lv_obj_t *bms_error;
    lv_obj_t *temperature;
    lv_obj_t *temperature_values[ESP_BMS_BMS_TEMP_MAX_COUNT];
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
    lv_point_t settings_swipe_start;
    lv_point_t quick_drag_start;
    int32_t quick_pull_drag_dy;
    int32_t return_swipe_drag_dy;
    esp_bms_lvgl_page_t page;
    esp_bms_lvgl_action_event_t pending_event;
    lv_obj_t *quick_drag_obj;
    int32_t quick_drag_obj_x;
    int32_t quick_drag_obj_y;
    quick_drag_target_kind_t quick_drag_target_kind;
    uint8_t quick_drag_target_index;
    uint8_t settings_detail_id;
    uint8_t quick_level_overlay_kind;
    uint8_t quick_brightness_percent;
    uint8_t quick_volume_percent;
    bool deferred_snapshot_valid;
    bool quick_panel_open;
    bool quick_panel_interactive;
    bool quick_panel_settling;
    bool quick_panel_animation_target_open;
    bool quick_pull_tracking;
    bool return_swipe_tracking;
    bool return_swipe_cancelled;
    bool settings_swipe_tracking;
    bool settings_swipe_consumed;
    bool quick_edit_mode;
    bool quick_drag_moved;
    bool quick_long_triggered;
    bool quick_level_overlay_active;
    bool quick_level_overlay_dragged;
    bool quick_level_overlay_horizontal;
    bool quick_level_long_triggered;
    bool soc_fill_horizontal;
    bool soc_wave_active;
    bool soc_wave_vertical;
    int32_t soc_wave_span;
    esp_bms_dashboard_snapshot_t deferred_snapshot;
    char current_setup_ap_qr_payload[sizeof(((esp_bms_dashboard_snapshot_t *)0)->setup_ap_qr_payload)];
    bool initialized;
} esp_bms_lvgl_ui_t;

static esp_bms_lvgl_ui_t s_ui;
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_0_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_1_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_2_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_3_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
static bool s_dashboard_cell_key_draw_buf_initialized[DASHBOARD_CELL_STAT_COUNT];

static void finish_page_scroll_state(bool flush_snapshot);
static void move_to_page(esp_bms_lvgl_page_t page, bool animated);
static void show_dashboard_view(void);
static void settings_show_root(void);
static void set_quick_panel_open(bool open);
static void set_quick_edit_mode(bool edit_mode);
static void quick_tile_set_scale(lv_obj_t *obj, int32_t scale);
static void refresh_quick_level_layouts(void);
static bool process_return_swipe_event(lv_event_code_t code, bool allow_start);

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
static const lv_color_t COLOR_SETTINGS_BG = LV_COLOR_MAKE(0xea, 0xf7, 0xf8);
static const lv_color_t COLOR_SETTINGS_CARD = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_SETTINGS_ICON_BG = LV_COLOR_MAKE(0xdb, 0xef, 0xf2);
static const lv_color_t COLOR_SETTINGS_BORDER = LV_COLOR_MAKE(0xc8, 0xda, 0xdf);
static const lv_color_t COLOR_SETTINGS_TEXT = LV_COLOR_MAKE(0x10, 0x1a, 0x2b);
static const lv_color_t COLOR_SETTINGS_MUTED = LV_COLOR_MAKE(0x55, 0x68, 0x76);
static const lv_color_t COLOR_SETTINGS_ACCENT = LV_COLOR_MAKE(0x0e, 0x9c, 0x95);

static const lv_point_precise_t DASHBOARD_SOC_WAVE_POINTS[] = {
    { .x = 0, .y = 9 },     { .x = 4, .y = 5 },     { .x = 8, .y = 3 },
    { .x = 12, .y = 5 },    { .x = 16, .y = 9 },    { .x = 20, .y = 13 },
    { .x = 24, .y = 15 },   { .x = 28, .y = 13 },   { .x = 32, .y = 9 },
    { .x = 36, .y = 5 },    { .x = 40, .y = 3 },    { .x = 44, .y = 5 },
    { .x = 48, .y = 9 },    { .x = 52, .y = 13 },   { .x = 56, .y = 15 },
    { .x = 60, .y = 13 },   { .x = 64, .y = 9 },    { .x = 68, .y = 5 },
    { .x = 72, .y = 3 },    { .x = 76, .y = 5 },    { .x = 80, .y = 9 },
    { .x = 84, .y = 13 },   { .x = 88, .y = 15 },   { .x = 92, .y = 13 },
    { .x = 96, .y = 9 },    { .x = 100, .y = 5 },   { .x = 104, .y = 3 },
    { .x = 108, .y = 5 },   { .x = 112, .y = 9 },   { .x = 116, .y = 13 },
    { .x = 120, .y = 15 },  { .x = 124, .y = 13 },  { .x = 128, .y = 9 },
    { .x = 132, .y = 5 },   { .x = 136, .y = 3 },   { .x = 140, .y = 5 },
    { .x = 144, .y = 9 },   { .x = 148, .y = 13 },  { .x = 152, .y = 15 },
    { .x = 156, .y = 13 },  { .x = 160, .y = 9 },   { .x = 164, .y = 5 },
    { .x = 168, .y = 3 },   { .x = 172, .y = 5 },   { .x = 176, .y = 9 },
    { .x = 180, .y = 13 },  { .x = 184, .y = 15 },  { .x = 188, .y = 13 },
    { .x = 192, .y = 9 },
};

static const uint8_t DASHBOARD_CELL_STAT_KEY_BITMAPS[DASHBOARD_CELL_STAT_COUNT]
                                                   [DASHBOARD_CELL_KEY_BITMAP_BYTES] = {
    {
        0x3f, 0xf0, 0x08, 0x03, 0x01, 0x1f, 0xfe, 0x3f, 0xf0, 0x00, 0x03, 0x01,
        0x07, 0xf8, 0x3f, 0xf0, 0x40, 0x80, 0x00, 0x07, 0xf8, 0x7f, 0xf8, 0x00,
        0x02, 0x20, 0x0f, 0xfe, 0x3f, 0xf8, 0x80, 0x62, 0x29, 0x0b, 0xf6, 0x3e,
        0xb0, 0xa1, 0x62, 0x26, 0x0a, 0x16, 0x7e, 0xf0, 0xbf, 0x60, 0x30, 0x88,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0x00, 0x03, 0xff, 0x02, 0x00, 0x30, 0x10, 0x43, 0xc3, 0xff,
        0x05, 0xd0, 0x30, 0x10, 0x91, 0x03, 0xff, 0x19, 0x10, 0x00, 0x03, 0x9f,
        0xe7, 0xff, 0x89, 0x10, 0x22, 0x00, 0x91, 0x03, 0xff, 0x89, 0x08, 0x22,
        0x90, 0x90, 0x83, 0xeb, 0x09, 0xe9, 0x22, 0x60, 0xb8, 0x57, 0xef, 0x08,
        0x06, 0x03, 0x08, 0x9f, 0x80, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0x41, 0x83, 0xff, 0x82, 0x10, 0x20, 0x01, 0xff, 0xe2, 0x18,
        0x00, 0xc0, 0x21, 0x80, 0xff, 0xc2, 0x18, 0x00, 0xc0, 0x2f, 0xf8, 0x0c,
        0x02, 0x18, 0x1f, 0xfe, 0x21, 0xa0, 0x60, 0x04, 0x1b, 0x05, 0xfc, 0x41,
        0x90, 0x42, 0x04, 0x18, 0x0c, 0x20, 0x5f, 0xf8, 0x82, 0x00, 0x00, 0x17,
        0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0xc4, 0x07, 0xff, 0x8c, 0x40, 0x03, 0x00, 0xcc, 0x01, 0x33,
        0x0c, 0xfe, 0x13, 0x21, 0xf8, 0x21, 0x32, 0x0d, 0x02, 0x0b, 0x40, 0xcf,
        0x20, 0x30, 0x0c, 0x02, 0x7f, 0xf8, 0xe0, 0x20, 0x30, 0x0e, 0x1a, 0x03,
        0x01, 0x8e, 0x20, 0x30, 0x00, 0x82, 0x03, 0x00, 0x00, 0x60, 0x30, 0x00,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
};

static const char *const DASHBOARD_TEMP_KEYS[ESP_BMS_BMS_TEMP_MAX_COUNT] = {
    "T1",
    "T2",
    "T3",
    "T4",
    "BAL",
    "MOS",
};

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

static void dashboard_soc_fill_create(lv_obj_t *soc_panel)
{
    lv_obj_set_style_clip_corner(soc_panel, true, LV_PART_MAIN);

    s_ui.soc_fill = lv_obj_create(soc_panel);
    clear_style(s_ui.soc_fill);
    lv_obj_set_style_radius(s_ui.soc_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.soc_fill, COLOR_SOC, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.soc_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.soc_fill, 0, LV_PART_MAIN);

    s_ui.soc_wave = lv_line_create(s_ui.soc_fill);
    clear_style(s_ui.soc_wave);
    lv_line_set_points(s_ui.soc_wave,
                       DASHBOARD_SOC_WAVE_POINTS,
                       sizeof(DASHBOARD_SOC_WAVE_POINTS) / sizeof(DASHBOARD_SOC_WAVE_POINTS[0]));
    lv_obj_set_size(s_ui.soc_wave, DASHBOARD_SOC_WAVE_WIDTH, DASHBOARD_SOC_WAVE_HEIGHT);
    lv_obj_set_style_line_color(s_ui.soc_wave, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_ui.soc_wave, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_ui.soc_wave, 3, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_ui.soc_wave, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.soc_wave, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.soc_wave, LV_OBJ_FLAG_HIDDEN);
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

static lv_draw_buf_t *dashboard_cell_key_draw_buf(uint8_t index)
{
    switch (index) {
    case 0:
        if (!s_dashboard_cell_key_draw_buf_initialized[0]) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_0_draw_buf);
            s_dashboard_cell_key_draw_buf_initialized[0] = true;
        }
        return &s_dashboard_cell_key_0_draw_buf;
    case 1:
        if (!s_dashboard_cell_key_draw_buf_initialized[1]) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_1_draw_buf);
            s_dashboard_cell_key_draw_buf_initialized[1] = true;
        }
        return &s_dashboard_cell_key_1_draw_buf;
    case 2:
        if (!s_dashboard_cell_key_draw_buf_initialized[2]) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_2_draw_buf);
            s_dashboard_cell_key_draw_buf_initialized[2] = true;
        }
        return &s_dashboard_cell_key_2_draw_buf;
    case 3:
    default:
        if (!s_dashboard_cell_key_draw_buf_initialized[3]) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_3_draw_buf);
            s_dashboard_cell_key_draw_buf_initialized[3] = true;
        }
        return &s_dashboard_cell_key_3_draw_buf;
    }
}

static void dashboard_cell_key_draw(lv_obj_t *canvas, uint8_t index)
{
    if (!canvas || index >= DASHBOARD_CELL_STAT_COUNT) {
        return;
    }

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
    for (uint8_t y = 0; y < DASHBOARD_CELL_KEY_BITMAP_H; ++y) {
        for (uint8_t x = 0; x < DASHBOARD_CELL_KEY_BITMAP_W; ++x) {
            const uint16_t bit_index = ((uint16_t)y * DASHBOARD_CELL_KEY_BITMAP_W) + x;
            const uint8_t byte = DASHBOARD_CELL_STAT_KEY_BITMAPS[index][bit_index / 8U];
            const uint8_t mask = (uint8_t)(1U << (7U - (bit_index % 8U)));
            if ((byte & mask) != 0U) {
                lv_canvas_set_px(canvas, x, y, COLOR_TEXT, LV_OPA_COVER);
            }
        }
    }
}

static lv_obj_t *dashboard_cell_key(lv_obj_t *parent, int32_t x, int32_t y, uint8_t index)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    clear_style(canvas);
    lv_canvas_set_draw_buf(canvas, dashboard_cell_key_draw_buf(index));
    lv_obj_set_pos(canvas, x, y);
    lv_obj_set_size(canvas, DASHBOARD_CELL_KEY_BITMAP_W, DASHBOARD_CELL_KEY_BITMAP_H);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, LV_PART_MAIN);
    dashboard_cell_key_draw(canvas, index);
    return canvas;
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

static void quick_rect_set(quick_tile_rect_t *rect, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!rect) {
        return;
    }
    rect->x = (int16_t)x;
    rect->y = (int16_t)y;
    rect->w = (int16_t)w;
    rect->h = (int16_t)h;
}

static quick_layout_orientation_t quick_layout_orientation(int32_t width, int32_t height)
{
    return width < height ? QUICK_LAYOUT_PORTRAIT : QUICK_LAYOUT_LANDSCAPE;
}

static quick_panel_layout_t *quick_current_layout(void)
{
    return &s_ui.quick_layouts[quick_layout_orientation(s_ui.width, s_ui.height)];
}

static void quick_layout_make_default(quick_panel_layout_t *layout,
                                      int32_t width,
                                      int32_t height,
                                      bool tools_vertical)
{
    if (!layout) {
        return;
    }

    memset(layout, 0, sizeof(*layout));
    (void)tools_vertical;
    layout->valid = true;
    layout->tools_vertical = false;

    const int32_t gap = 8;
    const int32_t quick_pad = 16;
    const int32_t tile_w = (width - (quick_pad * 2) -
                            ((QUICK_PANEL_GRID_COLS - 1) * gap)) / QUICK_PANEL_GRID_COLS;
    const int32_t tile_h = (height - (quick_pad * 2) -
                            ((QUICK_PANEL_GRID_ROWS - 1) * gap)) / QUICK_PANEL_GRID_ROWS;
    int32_t quick_tile = tile_w < tile_h ? tile_w : tile_h;
    if (quick_tile < 1) {
        quick_tile = 1;
    }
    const int32_t quick_grid_w = (QUICK_PANEL_GRID_COLS * quick_tile) +
                                 ((QUICK_PANEL_GRID_COLS - 1) * gap);
    const int32_t quick_grid_h = (QUICK_PANEL_GRID_ROWS * quick_tile) +
                                 ((QUICK_PANEL_GRID_ROWS - 1) * gap);
    const int32_t quick_left = (width - quick_grid_w) / 2;
    const int32_t quick_top = (height - quick_grid_h) / 2;

    quick_tile_rect_t slots[QUICK_PANEL_GRID_SLOT_COUNT] = { 0 };
    for (uint32_t slot = 0; slot < QUICK_PANEL_GRID_SLOT_COUNT; ++slot) {
        const int32_t col = (int32_t)(slot % QUICK_PANEL_GRID_COLS);
        const int32_t row = (int32_t)(slot / QUICK_PANEL_GRID_COLS);
        quick_rect_set(&slots[slot],
                       quick_left + (col * (quick_tile + gap)),
                       quick_top + (row * (quick_tile + gap)),
                       quick_tile,
                       quick_tile);
    }

    layout->brightness = slots[0];
    layout->volume = slots[1];
    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        layout->items[index] = slots[index + 2U];
    }
}

static quick_panel_layout_t *quick_layout_ensure_current(void)
{
    quick_panel_layout_t *layout = quick_current_layout();
    if (!layout->valid) {
        quick_layout_make_default(layout,
                                  s_ui.width,
                                  s_ui.height,
                                  quick_layout_orientation(s_ui.width, s_ui.height) == QUICK_LAYOUT_PORTRAIT);
    }
    return layout;
}

static void quick_obj_apply_rect(lv_obj_t *obj, const quick_tile_rect_t *rect)
{
    if (!obj || !rect) {
        return;
    }
    lv_obj_set_pos(obj, rect->x, rect->y);
    lv_obj_set_size(obj, rect->w, rect->h);
}

static quick_tile_rect_t *quick_layout_rect_for_target(quick_panel_layout_t *layout,
                                                       quick_drag_target_kind_t target_kind,
                                                       uint8_t target_index)
{
    if (!layout) {
        return NULL;
    }
    if (target_kind == QUICK_DRAG_TARGET_BRIGHTNESS) {
        return &layout->brightness;
    }
    if (target_kind == QUICK_DRAG_TARGET_VOLUME) {
        return &layout->volume;
    }
    if (target_kind == QUICK_DRAG_TARGET_ITEM && target_index < QUICK_PANEL_BUTTON_COUNT) {
        return &layout->items[target_index];
    }
    return NULL;
}

static void quick_layout_apply_current(void)
{
    quick_panel_layout_t *layout = quick_layout_ensure_current();
    quick_obj_apply_rect(s_ui.quick_brightness_tile, &layout->brightness);
    quick_obj_apply_rect(s_ui.quick_volume_tile, &layout->volume);
    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        quick_obj_apply_rect(s_ui.quick_panel_items[index], &layout->items[index]);
    }
    refresh_quick_level_layouts();
}

static bool quick_rect_contains_point(const quick_tile_rect_t *rect, int32_t x, int32_t y)
{
    return rect &&
           x >= rect->x &&
           x < (rect->x + rect->w) &&
           y >= rect->y &&
           y < (rect->y + rect->h);
}

static int32_t quick_rect_center_distance_sq(const quick_tile_rect_t *rect, int32_t x, int32_t y)
{
    if (!rect) {
        return INT32_MAX;
    }
    const int32_t dx = (rect->x + (rect->w / 2)) - x;
    const int32_t dy = (rect->y + (rect->h / 2)) - y;
    return (dx * dx) + (dy * dy);
}

static void quick_layout_find_drop_target(quick_panel_layout_t *layout,
                                          int32_t x,
                                          int32_t y,
                                          quick_drag_target_kind_t *target_kind,
                                          uint8_t *target_index)
{
    quick_drag_target_kind_t best_kind = QUICK_DRAG_TARGET_BRIGHTNESS;
    uint8_t best_index = 0;
    int32_t best_distance = INT32_MAX;
    const quick_drag_target_kind_t kinds[QUICK_PANEL_CONTROL_COUNT] = {
        QUICK_DRAG_TARGET_BRIGHTNESS,
        QUICK_DRAG_TARGET_VOLUME,
        QUICK_DRAG_TARGET_ITEM,
        QUICK_DRAG_TARGET_ITEM,
        QUICK_DRAG_TARGET_ITEM,
        QUICK_DRAG_TARGET_ITEM,
        QUICK_DRAG_TARGET_ITEM,
        QUICK_DRAG_TARGET_ITEM,
    };
    const uint8_t indexes[QUICK_PANEL_CONTROL_COUNT] = {
        0, 0, 0, 1, 2, 3, 4, 5,
    };

    for (uint32_t slot = 0; slot < QUICK_PANEL_CONTROL_COUNT; ++slot) {
        quick_tile_rect_t *rect = quick_layout_rect_for_target(layout, kinds[slot], indexes[slot]);
        if (!rect) {
            continue;
        }
        if (quick_rect_contains_point(rect, x, y)) {
            best_kind = kinds[slot];
            best_index = indexes[slot];
            break;
        }
        const int32_t distance = quick_rect_center_distance_sq(rect, x, y);
        if (distance < best_distance) {
            best_distance = distance;
            best_kind = kinds[slot];
            best_index = indexes[slot];
        }
    }

    if (target_kind) {
        *target_kind = best_kind;
    }
    if (target_index) {
        *target_index = best_index;
    }
}

static void quick_layout_commit_drag_sort(void)
{
    if (!s_ui.quick_drag_obj || !s_ui.quick_drag_moved) {
        quick_layout_apply_current();
        return;
    }

    quick_panel_layout_t *layout = quick_layout_ensure_current();
    quick_tile_rect_t *source =
        quick_layout_rect_for_target(layout, s_ui.quick_drag_target_kind, s_ui.quick_drag_target_index);
    if (!source) {
        quick_layout_apply_current();
        return;
    }

    const int32_t center_x = lv_obj_get_x(s_ui.quick_drag_obj) + (lv_obj_get_width(s_ui.quick_drag_obj) / 2);
    const int32_t center_y = lv_obj_get_y(s_ui.quick_drag_obj) + (lv_obj_get_height(s_ui.quick_drag_obj) / 2);
    quick_drag_target_kind_t target_kind = QUICK_DRAG_TARGET_NONE;
    uint8_t target_index = 0;
    quick_layout_find_drop_target(layout, center_x, center_y, &target_kind, &target_index);

    if (target_kind != s_ui.quick_drag_target_kind ||
        target_index != s_ui.quick_drag_target_index) {
        quick_tile_rect_t *target = quick_layout_rect_for_target(layout, target_kind, target_index);
        if (target) {
            const quick_tile_rect_t moved_rect = *source;
            *source = *target;
            *target = moved_rect;
        }
    }
    quick_layout_apply_current();
}

static void dashboard_soc_wave_x_anim_cb(void *var, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)var, value);
}

static void dashboard_soc_wave_y_anim_cb(void *var, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)var, value);
}

static void dashboard_soc_wave_stop(void)
{
    if (!s_ui.soc_wave) {
        return;
    }
    lv_anim_delete(s_ui.soc_wave, dashboard_soc_wave_x_anim_cb);
    lv_anim_delete(s_ui.soc_wave, dashboard_soc_wave_y_anim_cb);
    s_ui.soc_wave_active = false;
    s_ui.soc_wave_span = 0;
}

static void dashboard_soc_wave_start(bool vertical, int32_t span)
{
    if (!s_ui.soc_wave) {
        return;
    }
    if (s_ui.soc_wave_active &&
        s_ui.soc_wave_vertical == vertical &&
        (!vertical || abs_i32(s_ui.soc_wave_span - span) <= 2)) {
        return;
    }

    dashboard_soc_wave_stop();
    s_ui.soc_wave_active = true;
    s_ui.soc_wave_vertical = vertical;
    s_ui.soc_wave_span = span;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.soc_wave);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    if (vertical) {
        lv_anim_set_values(&anim, span - 2, -DASHBOARD_SOC_WAVE_HEIGHT);
        lv_anim_set_duration(&anim, 1500);
        lv_anim_set_exec_cb(&anim, dashboard_soc_wave_y_anim_cb);
    } else {
        lv_anim_set_values(&anim, -DASHBOARD_SOC_WAVE_PERIOD, 0);
        lv_anim_set_duration(&anim, 850);
        lv_anim_set_exec_cb(&anim, dashboard_soc_wave_x_anim_cb);
    }
    lv_anim_start(&anim);
}

static lv_color_t dashboard_soc_fill_color(uint8_t soc_percent, bool valid, bool charging)
{
    if (!valid) {
        return COLOR_PANEL_ALT;
    }
    if (charging || soc_percent >= 100U) {
        return COLOR_ACCENT;
    }
    if (soc_percent <= 20U) {
        return COLOR_BAD;
    }
    return COLOR_SOC;
}

static void update_dashboard_soc_fill(uint8_t soc_percent, bool valid, bool charging)
{
    if (!s_ui.soc_fill) {
        return;
    }

    lv_obj_t *panel_obj = lv_obj_get_parent(s_ui.soc_fill);
    const int32_t panel_w = lv_obj_get_width(panel_obj);
    const int32_t panel_h = lv_obj_get_height(panel_obj);
    const uint8_t soc = valid ? (soc_percent > 100U ? 100U : soc_percent) : 0U;
    const bool show_fill = valid && soc > 0U;

    lv_obj_set_style_bg_color(s_ui.soc_fill, dashboard_soc_fill_color(soc, valid, charging), LV_PART_MAIN);
    if (s_ui.soc_fill_horizontal) {
        const int32_t fill_w = show_fill ? ((panel_w * (int32_t)soc) / 100) : 0;
        lv_obj_set_pos(s_ui.soc_fill, 0, 0);
        lv_obj_set_size(s_ui.soc_fill, fill_w, panel_h);
        if (s_ui.soc_wave) {
            const bool show_wave = charging && fill_w >= 32;
            set_obj_hidden(s_ui.soc_wave, !show_wave);
            if (show_wave) {
                lv_obj_set_y(s_ui.soc_wave, (panel_h - DASHBOARD_SOC_WAVE_HEIGHT) / 2);
                lv_obj_set_size(s_ui.soc_wave, DASHBOARD_SOC_WAVE_WIDTH, DASHBOARD_SOC_WAVE_HEIGHT);
                dashboard_soc_wave_start(false, 0);
            } else {
                dashboard_soc_wave_stop();
            }
        }
    } else {
        const int32_t fill_h = show_fill ? ((panel_h * (int32_t)soc) / 100) : 0;
        lv_obj_set_pos(s_ui.soc_fill, 0, panel_h - fill_h);
        lv_obj_set_size(s_ui.soc_fill, panel_w, fill_h);
        if (s_ui.soc_wave) {
            const bool show_wave = charging && fill_h >= 24;
            set_obj_hidden(s_ui.soc_wave, !show_wave);
            if (show_wave) {
                lv_obj_set_x(s_ui.soc_wave, -DASHBOARD_SOC_WAVE_PERIOD / 2);
                lv_obj_set_size(s_ui.soc_wave, DASHBOARD_SOC_WAVE_WIDTH, DASHBOARD_SOC_WAVE_HEIGHT);
                dashboard_soc_wave_start(true, fill_h);
            } else {
                dashboard_soc_wave_stop();
            }
        }
    }
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

static bool settings_view_is_visible(void)
{
    return s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
}

static bool quick_pull_start_allowed(const lv_point_t *point)
{
    if (!point || s_ui.quick_panel_open || settings_view_is_visible()) {
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
    s_ui.settings_swipe_consumed = false;
    settings_show_root();
    lv_obj_move_foreground(s_ui.settings_page);
}

static void queue_action(esp_bms_lvgl_action_t action)
{
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        s_ui.pending_event.action = action;
        s_ui.pending_event.committed = true;
        s_ui.pending_event.brightness_percent_valid = false;
        s_ui.pending_event.brightness_percent = 0;
        s_ui.pending_event.volume_percent_valid = false;
        s_ui.pending_event.volume_percent = 0;
        s_ui.pending_event.volume_feedback_valid = false;
        s_ui.pending_event.volume_feedback_percent = 0;
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

static const uint8_t QUICK_BRIGHTNESS_PRESETS[QUICK_LEVEL_PRESET_COUNT] = {
    25U,
    50U,
    75U,
    100U,
};

static const uint8_t QUICK_VOLUME_PRESETS[QUICK_LEVEL_PRESET_COUNT] = {
    0U,
    35U,
    70U,
    100U,
};

static const char *quick_level_icon(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_EYE_OPEN;
}

static const char *quick_level_toast_prefix(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? "VOL" : "BRI";
}

static uint8_t quick_level_current_value(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_percent : s_ui.quick_brightness_percent;
}

static uint8_t quick_level_next_preset(quick_level_kind_t kind)
{
    const uint8_t *presets = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_PRESETS :
                                                          QUICK_BRIGHTNESS_PRESETS;
    const uint8_t current = quick_level_current_value(kind);
    for (uint32_t i = 0; i < QUICK_LEVEL_PRESET_COUNT; ++i) {
        if (current < presets[i]) {
            return presets[i];
        }
    }
    return presets[0];
}

static void quick_level_queue_value(quick_level_kind_t kind, uint8_t value, bool committed)
{
    s_ui.pending_event.action = kind == QUICK_LEVEL_VOLUME ? ESP_BMS_LVGL_ACTION_SET_VOLUME :
                                                             ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS;
    s_ui.pending_event.committed = committed;
    s_ui.pending_event.brightness_percent_valid = kind == QUICK_LEVEL_BRIGHTNESS;
    s_ui.pending_event.brightness_percent = kind == QUICK_LEVEL_BRIGHTNESS ? value : 0;
    s_ui.pending_event.volume_percent_valid = kind == QUICK_LEVEL_VOLUME;
    s_ui.pending_event.volume_percent = kind == QUICK_LEVEL_VOLUME ? value : 0;
    s_ui.pending_event.volume_feedback_valid = kind == QUICK_LEVEL_VOLUME;
    s_ui.pending_event.volume_feedback_percent = kind == QUICK_LEVEL_VOLUME ? value : 0;
}

static uint8_t quick_level_snap_drag_value(quick_level_kind_t kind, int32_t value)
{
    const int32_t min_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const int32_t max_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t clamped = clamp_i32(value, min_value, max_value);
    int32_t snapped = ((clamped + (QUICK_LEVEL_DRAG_STEP / 2)) / QUICK_LEVEL_DRAG_STEP) *
                      QUICK_LEVEL_DRAG_STEP;
    snapped = clamp_i32(snapped, min_value, max_value);
    return (uint8_t)snapped;
}

static void quick_level_overlay_update(quick_level_kind_t kind, uint8_t value)
{
    if (!s_ui.quick_level_overlay_active ||
        s_ui.quick_level_overlay_kind != (uint8_t)kind ||
        !s_ui.quick_level_overlay_track ||
        !s_ui.quick_level_overlay_fill ||
        !s_ui.quick_level_overlay_knob) {
        return;
    }

    const int32_t track_h = lv_obj_get_height(s_ui.quick_level_overlay_track);
    const int32_t track_w = lv_obj_get_width(s_ui.quick_level_overlay_track);
    if (track_h <= 0 || track_w <= 0) {
        return;
    }

    const uint8_t min_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const uint8_t max_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t range = max_value > min_value ? (int32_t)(max_value - min_value) : 1;
    const int32_t clamped = clamp_i32(value, min_value, max_value);

    const int32_t track_x = lv_obj_get_x(s_ui.quick_level_overlay_track);
    const int32_t track_y = lv_obj_get_y(s_ui.quick_level_overlay_track);
    if (s_ui.quick_level_overlay_horizontal) {
        int32_t fill_w = ((clamped - min_value) * track_w) / range;
        fill_w = clamp_i32(fill_w, 4, track_w);
        lv_obj_set_pos(s_ui.quick_level_overlay_fill, 0, 0);
        lv_obj_set_size(s_ui.quick_level_overlay_fill, fill_w, track_h);
        lv_obj_set_pos(s_ui.quick_level_overlay_knob,
                       track_x + fill_w - 10,
                       track_y + (track_h / 2) - 10);
    } else {
        int32_t fill_h = ((clamped - min_value) * track_h) / range;
        fill_h = clamp_i32(fill_h, 4, track_h);
        lv_obj_set_pos(s_ui.quick_level_overlay_fill, 0, track_h - fill_h);
        lv_obj_set_size(s_ui.quick_level_overlay_fill, track_w, fill_h);
        lv_obj_set_pos(s_ui.quick_level_overlay_knob,
                       track_x + (track_w / 2) - 10,
                       track_y + track_h - fill_h - 10);
    }

    if (s_ui.quick_level_overlay_value) {
        label_set_text_fmt_if_changed(s_ui.quick_level_overlay_value,
                                      "%s %u%%",
                                      quick_level_icon(kind),
                                      (unsigned)clamped);
    }
}

static void set_quick_brightness_value(uint8_t brightness_percent, bool queue, bool committed)
{
    const uint8_t clamped = clamp_brightness_percent(brightness_percent);
    s_ui.quick_brightness_percent = clamped;
    if (s_ui.quick_brightness_label) {
        label_set_text_if_changed(s_ui.quick_brightness_label, LV_SYMBOL_EYE_OPEN);
    }
    quick_level_overlay_update(QUICK_LEVEL_BRIGHTNESS, clamped);
    if (queue) {
        quick_level_queue_value(QUICK_LEVEL_BRIGHTNESS, clamped, committed);
    }
}

static void set_quick_volume_value(uint8_t volume_percent, bool queue, bool committed)
{
    const uint8_t clamped = clamp_volume_percent(volume_percent);
    s_ui.quick_volume_percent = clamped;
    if (s_ui.quick_volume_label) {
        label_set_text_if_changed(s_ui.quick_volume_label, LV_SYMBOL_VOLUME_MID);
    }
    quick_level_overlay_update(QUICK_LEVEL_VOLUME, clamped);
    if (queue) {
        quick_level_queue_value(QUICK_LEVEL_VOLUME, clamped, committed);
    }
}

static void refresh_quick_level_layouts(void)
{
    set_quick_brightness_value(s_ui.quick_brightness_percent ? s_ui.quick_brightness_percent : 85U, false, true);
    set_quick_volume_value(s_ui.quick_volume_percent, false, true);
}

static void quick_panel_y_anim_cb(void *var, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)var, value);
}

static void quick_panel_stop_settle_anim(void)
{
    if (s_ui.quick_panel) {
        lv_anim_delete(s_ui.quick_panel, quick_panel_y_anim_cb);
    }
    s_ui.quick_panel_settling = false;
}

static void quick_panel_settle_anim_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    set_quick_panel_open(s_ui.quick_panel_animation_target_open);
}

static int32_t quick_pull_open_threshold(void)
{
    return clamp_i32(s_ui.height / 3, QUICK_PULL_OPEN_DY, s_ui.height / 2);
}

static void quick_panel_animate_to_open_state(bool open)
{
    if (!s_ui.quick_panel) {
        set_quick_panel_open(open);
        return;
    }

    const int32_t target_y = open ? 0 : -s_ui.height;
    const int32_t current_y = lv_obj_get_y(s_ui.quick_panel);
    if (current_y == target_y) {
        set_quick_panel_open(open);
        return;
    }

    lv_anim_delete(s_ui.quick_panel, quick_panel_y_anim_cb);
    s_ui.quick_panel_open = open;
    s_ui.quick_panel_interactive = false;
    s_ui.quick_panel_settling = true;
    s_ui.quick_panel_animation_target_open = open;
    s_ui.quick_pull_drag_dy = 0;
    s_ui.return_swipe_drag_dy = 0;
    s_ui.return_swipe_tracking = false;
    s_ui.return_swipe_cancelled = false;
    set_obj_hidden(s_ui.quick_panel, false);
    set_obj_hidden(s_ui.quick_pull_zone, true);
    lv_obj_move_foreground(s_ui.quick_panel);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.quick_panel);
    lv_anim_set_values(&anim, current_y, target_y);
    lv_anim_set_duration(&anim, QUICK_PANEL_SETTLE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, quick_panel_y_anim_cb);
    lv_anim_set_completed_cb(&anim, quick_panel_settle_anim_completed_cb);
    lv_anim_start(&anim);
}

static void quick_toast_cancel(void)
{
    if (s_ui.quick_toast_timer) {
        lv_timer_delete(s_ui.quick_toast_timer);
        s_ui.quick_toast_timer = NULL;
    }
}

static void quick_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    set_obj_hidden(s_ui.quick_toast, true);
    s_ui.quick_toast_timer = NULL;
}

static void quick_toast_show_text(const char *text)
{
    if (!s_ui.quick_toast) {
        return;
    }

    label_set_text_if_changed(s_ui.quick_toast, text ? text : "");
    set_obj_hidden(s_ui.quick_toast, false);
    lv_obj_move_foreground(s_ui.quick_toast);

    quick_toast_cancel();
    s_ui.quick_toast_timer = lv_timer_create(quick_toast_timer_cb, QUICK_TOAST_MS, NULL);
    if (s_ui.quick_toast_timer) {
        lv_timer_set_repeat_count(s_ui.quick_toast_timer, 1);
    }
}

static void quick_toast_show(quick_level_kind_t kind, uint8_t value)
{
    char text[16];
    (void)snprintf(text,
                   sizeof(text),
                   "%s %u%%",
                   quick_level_toast_prefix(kind),
                   (unsigned)value);
    quick_toast_show_text(text);
}

static void set_quick_panel_open(bool open)
{
    quick_panel_stop_settle_anim();
    s_ui.quick_panel_open = open;
    s_ui.quick_panel_interactive = open;
    s_ui.quick_panel_animation_target_open = open;
    s_ui.quick_pull_drag_dy = 0;
    s_ui.return_swipe_drag_dy = 0;
    if (!open) {
        s_ui.quick_drag_obj = NULL;
        s_ui.quick_drag_moved = false;
        s_ui.quick_long_triggered = false;
        s_ui.quick_level_overlay_active = false;
        s_ui.quick_level_overlay_dragged = false;
        s_ui.quick_level_long_triggered = false;
        quick_toast_cancel();
        set_obj_hidden(s_ui.quick_toast, true);
        set_quick_edit_mode(false);
        set_obj_hidden(s_ui.quick_level_overlay, true);
    }
    if (s_ui.quick_panel) {
        lv_obj_set_y(s_ui.quick_panel, 0);
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
    const bool changed = s_ui.quick_edit_mode != edit_mode;
    s_ui.quick_edit_mode = edit_mode;
    if (s_ui.quick_edit_icon) {
        lv_obj_set_style_text_color(s_ui.quick_edit_icon, edit_mode ? COLOR_SOC : COLOR_MUTED, LV_PART_MAIN);
    }
    if (s_ui.quick_edit_button) {
        lv_obj_set_style_border_width(s_ui.quick_edit_button, edit_mode ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ui.quick_edit_button, COLOR_SOC, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_ui.quick_edit_button, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (changed && edit_mode) {
        quick_toast_show_text(QUICK_TOAST_SORT_HINT);
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

    if (action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY) {
        lv_indev_wait_release(lv_indev_active());
        show_dashboard_view();
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
    if (process_return_swipe_event(code, false)) {
        return;
    }

    esp_bms_lvgl_action_t action = (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    perform_ui_action(action, true);
}

static bool process_return_swipe_event(lv_event_code_t code, bool allow_start)
{
    if (s_ui.quick_level_overlay_active) {
        s_ui.return_swipe_tracking = false;
        s_ui.return_swipe_drag_dy = 0;
        s_ui.return_swipe_cancelled = false;
        return false;
    }

    if (code == LV_EVENT_PRESSED) {
        s_ui.return_swipe_cancelled = false;
        s_ui.return_swipe_drag_dy = 0;
        s_ui.return_swipe_tracking = allow_start &&
                                     get_active_pointer(&s_ui.return_swipe_start) &&
                                     return_home_start_allowed(&s_ui.return_swipe_start);
        return false;
    }

    if (s_ui.return_swipe_cancelled) {
        if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CLICKED) {
            s_ui.return_swipe_cancelled = false;
            s_ui.return_swipe_tracking = false;
        }
        return true;
    }

    if (code == LV_EVENT_PRESSING && s_ui.return_swipe_tracking) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return false;
        }

        const int32_t dx = point.x - s_ui.return_swipe_start.x;
        const int32_t dy = point.y - s_ui.return_swipe_start.y;
        if (dx >= RETURN_HOME_RIGHT_CANCEL_MIN_DX &&
            abs_i32(dy) <= RETURN_HOME_RIGHT_CANCEL_MAX_DY) {
            s_ui.return_swipe_tracking = false;
            s_ui.return_swipe_drag_dy = 0;
            s_ui.return_swipe_cancelled = true;
            s_ui.quick_panel_interactive = s_ui.quick_panel_open && !s_ui.quick_panel_settling;
            if (s_ui.quick_panel_open && s_ui.quick_panel) {
                lv_obj_set_y(s_ui.quick_panel, 0);
            }
            lv_indev_wait_release(lv_indev_active());
            return true;
        }
        if (s_ui.quick_panel_open && dy < 0 && abs_i32(dx) <= RETURN_HOME_SWIPE_MAX_DX) {
            s_ui.return_swipe_drag_dy = clamp_i32(-dy, 0, s_ui.height);
            s_ui.quick_panel_interactive = false;
            if (s_ui.quick_panel) {
                lv_obj_set_y(s_ui.quick_panel, -s_ui.return_swipe_drag_dy);
            }
            return true;
        }
        if (dy <= -RETURN_HOME_SWIPE_MIN_DY && abs_i32(dx) <= RETURN_HOME_SWIPE_MAX_DX) {
            s_ui.return_swipe_tracking = false;
            s_ui.return_swipe_drag_dy = 0;
            if (s_ui.quick_panel_open) {
                quick_panel_animate_to_open_state(false);
            } else if (s_ui.page != ESP_BMS_LVGL_PAGE_BATTERY) {
                move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, true);
            }
            lv_indev_wait_release(lv_indev_active());
            return true;
        }
    }

    if (code == LV_EVENT_RELEASED && s_ui.return_swipe_tracking && s_ui.quick_panel_open) {
        const bool should_return = s_ui.return_swipe_drag_dy >= RETURN_HOME_SWIPE_MIN_DY;
        const bool moved = s_ui.return_swipe_drag_dy > 3;
        s_ui.return_swipe_tracking = false;
        s_ui.return_swipe_drag_dy = 0;
        if (should_return) {
            quick_panel_animate_to_open_state(false);
        } else if (s_ui.quick_panel) {
            quick_panel_animate_to_open_state(true);
        }
        if (moved) {
            s_ui.return_swipe_cancelled = true;
        }
        return moved || should_return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.return_swipe_tracking = false;
        s_ui.return_swipe_drag_dy = 0;
        if (code == LV_EVENT_PRESS_LOST && s_ui.quick_panel_open && s_ui.quick_panel) {
            lv_obj_set_y(s_ui.quick_panel, 0);
            s_ui.quick_panel_interactive = !s_ui.quick_panel_settling;
        }
    }
    return false;
}

static void return_swipe_event_cb(lv_event_t *event)
{
    (void)process_return_swipe_event(lv_event_get_code(event), true);
}

static void quick_pull_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.quick_pull_drag_dy = 0;
        s_ui.quick_pull_tracking = get_active_pointer(&s_ui.quick_pull_start) &&
                                   quick_pull_start_allowed(&s_ui.quick_pull_start);
        if (s_ui.quick_pull_tracking && s_ui.quick_panel) {
            quick_panel_stop_settle_anim();
            s_ui.quick_panel_interactive = false;
            lv_obj_set_y(s_ui.quick_panel, -s_ui.height);
            set_obj_hidden(s_ui.quick_panel, false);
            lv_obj_move_foreground(s_ui.quick_panel);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING && s_ui.quick_pull_tracking) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }

        const int32_t dx = point.x - s_ui.quick_pull_start.x;
        const int32_t dy = point.y - s_ui.quick_pull_start.y;
        if (abs_i32(dx) > QUICK_PULL_MAX_DX) {
            s_ui.quick_pull_tracking = false;
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

    if (code == LV_EVENT_RELEASED && s_ui.quick_pull_tracking) {
        const bool should_open = s_ui.quick_pull_drag_dy >= quick_pull_open_threshold();
        s_ui.quick_pull_tracking = false;
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
        s_ui.quick_pull_tracking = false;
        s_ui.quick_pull_drag_dy = 0;
        if (s_ui.quick_panel && !s_ui.quick_panel_open) {
            quick_panel_animate_to_open_state(false);
        }
        if (s_ui.quick_pull_zone) {
            lv_obj_move_foreground(s_ui.quick_pull_zone);
        }
    }
}

typedef enum {
    QUICK_ITEM_BLUETOOTH = 0,
    QUICK_ITEM_HOTSPOT,
    QUICK_ITEM_WIFI,
    QUICK_ITEM_ROTATE,
    QUICK_ITEM_SPEED,
    QUICK_ITEM_SETTINGS,
} quick_panel_item_kind_t;

typedef struct {
    quick_panel_item_kind_t kind;
    const char *icon;
    esp_bms_lvgl_action_t click_action;
    const char *toast_text;
    bool hotspot_icon;
} quick_panel_item_t;

typedef enum {
    SETTINGS_DETAIL_NONE = 0,
    SETTINGS_DETAIL_WIFI,
    SETTINGS_DETAIL_HOTSPOT,
    SETTINGS_DETAIL_BLUETOOTH,
    SETTINGS_DETAIL_BMS,
    SETTINGS_DETAIL_SYSTEM,
    SETTINGS_DETAIL_ABOUT,
} settings_detail_id_t;

typedef struct {
    settings_detail_id_t detail_id;
    const char *title;
    const char *subtitle;
    const char *icon;
    const lv_font_t *icon_font;
} settings_option_t;

typedef struct {
    const char *title;
    const char *subtitle;
    esp_bms_lvgl_action_t action;
} settings_detail_row_t;

static const quick_panel_item_t QUICK_PANEL_ITEMS[QUICK_PANEL_BUTTON_COUNT] = {
    { QUICK_ITEM_BLUETOOTH, QUICK_BLUETOOTH_SYMBOL, ESP_BMS_LVGL_ACTION_START_BMS_BIND,
      "BLUETOOTH", false },
    { QUICK_ITEM_HOTSPOT, NULL, ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING,
      "HOTSPOT", true },
    { QUICK_ITEM_WIFI, LV_SYMBOL_WIFI, ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING,
      "WIFI", false },
    { QUICK_ITEM_ROTATE, LV_SYMBOL_LOOP, ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY,
      "ROTATE", false },
    { QUICK_ITEM_SPEED, LV_SYMBOL_GPS, ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT,
      "SPEED UNIT", false },
    { QUICK_ITEM_SETTINGS, LV_SYMBOL_SETTINGS, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "SETTINGS", false },
};

static const settings_option_t SETTINGS_OPTIONS[SETTINGS_OPTION_COUNT] = {
    { SETTINGS_DETAIL_WIFI, "无线网络", "WiFi 配置", LV_SYMBOL_WIFI, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_HOTSPOT, "热点共享", "Setup AP", QUICK_HOTSPOT_SYMBOL, &wlanJZ },
    { SETTINGS_DETAIL_BLUETOOTH, "蓝牙", "BLE 绑定", QUICK_BLUETOOTH_SYMBOL, &bluetoothon },
    { SETTINGS_DETAIL_BMS, "保护板设置", "BMS 状态", LV_SYMBOL_CHARGE, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_SYSTEM, "系统", "显示与控制", LV_SYMBOL_SETTINGS, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_ABOUT, "关于本机", "设备信息", "i", &lv_font_montserrat_24 },
};

static const settings_detail_row_t SETTINGS_WIFI_ROWS[] = {
    { "状态", "当前: Setup AP", ESP_BMS_LVGL_ACTION_NONE },
    { "配置入口", "开启配网入口", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING },
    { "手机页面", "192.168.4.1 网页配置", ESP_BMS_LVGL_ACTION_NONE },
};

static const settings_detail_row_t SETTINGS_HOTSPOT_ROWS[] = {
    { "状态", "热点已打开", ESP_BMS_LVGL_ACTION_NONE },
    { "名称", "fuckingBms_xxxxxx", ESP_BMS_LVGL_ACTION_NONE },
    { "密码", "8 位数字", ESP_BMS_LVGL_ACTION_NONE },
    { "二维码", "网页查看", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING },
};

static const settings_detail_row_t SETTINGS_BLUETOOTH_ROWS[] = {
    { "状态", "未连接", ESP_BMS_LVGL_ACTION_NONE },
    { "扫描绑定", "扫描保护板", ESP_BMS_LVGL_ACTION_START_BMS_BIND },
};

static const settings_detail_row_t SETTINGS_BMS_ROWS[] = {
    { "连接状态", "未连接", ESP_BMS_LVGL_ACTION_NONE },
    { "保护信息", "暂无告警", ESP_BMS_LVGL_ACTION_NONE },
    { "扫描绑定", "扫描保护板", ESP_BMS_LVGL_ACTION_START_BMS_BIND },
};

static const settings_detail_row_t SETTINGS_SYSTEM_ROWS[] = {
    { "亮度", "快捷面板调节", ESP_BMS_LVGL_ACTION_NONE },
    { "音量", "快捷面板调节", ESP_BMS_LVGL_ACTION_NONE },
    { "旋转屏幕", "点击操作", ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY },
    { "语言切换", "点击操作", ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE },
    { "恢复默认", "点击操作", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS },
};

static const settings_detail_row_t SETTINGS_ABOUT_ROWS[] = {
    { "设备", "ESP32 BMS GPS", ESP_BMS_LVGL_ACTION_NONE },
    { "固件版本", "本地构建", ESP_BMS_LVGL_ACTION_NONE },
    { "屏幕", "ST7789", ESP_BMS_LVGL_ACTION_NONE },
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
    return active ? COLOR_SOC : COLOR_TEXT;
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
    lv_obj_set_style_text_color(icon, quick_bluetooth_symbol_color(active), LV_PART_MAIN);
}

static void quick_hotspot_icon_set_active(lv_obj_t *icon, bool active)
{
    if (!icon) {
        return;
    }
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, quick_icon_color(active), LV_PART_MAIN);
}

static bool quick_panel_item_can_stay_active(const quick_panel_item_t *item)
{
    return item && (item->kind == QUICK_ITEM_BLUETOOTH ||
                    item->kind == QUICK_ITEM_HOTSPOT ||
                    item->kind == QUICK_ITEM_WIFI);
}

static void quick_panel_item_apply_active(uint32_t index, bool active)
{
    if (index >= QUICK_PANEL_BUTTON_COUNT) {
        return;
    }

    const quick_panel_item_t *item = &QUICK_PANEL_ITEMS[index];
    s_ui.quick_panel_item_active[index] = active;
    if (s_ui.quick_panel_items[index]) {
        lv_obj_set_style_bg_color(s_ui.quick_panel_items[index], COLOR_PANEL_ALT, LV_PART_MAIN);
    }
    if (item->kind == QUICK_ITEM_BLUETOOTH) {
        quick_bluetooth_icon_set_active(s_ui.quick_panel_item_icons[index], active);
    } else if (item->kind == QUICK_ITEM_HOTSPOT) {
        quick_hotspot_icon_set_active(s_ui.quick_panel_item_icons[index], active);
    } else {
        quick_icon_tree_set_color(s_ui.quick_panel_item_icons[index], quick_icon_color(active));
    }
    if (s_ui.quick_panel_item_icons[index]) {
        lv_obj_set_style_opa(s_ui.quick_panel_item_icons[index], LV_OPA_COVER, LV_PART_MAIN);
    }
}

static lv_obj_t *quick_level_tile_for_kind(quick_level_kind_t kind);
static void quick_symbol_icon_recenter(lv_obj_t *icon,
                                       int32_t content_w,
                                       int32_t content_h,
                                       const char *symbol,
                                       const lv_font_t *font);

static int32_t quick_tile_pressed_extent(int32_t extent, bool pressed)
{
    const int32_t inset = pressed ? QUICK_TILE_PRESS_INSET : 0;
    const int32_t min_size = (2 * QUICK_TILE_PRESS_INSET) + 1;
    return extent > min_size ? extent - (2 * inset) : extent;
}

static void quick_tile_apply_press_inset(lv_obj_t *obj, const quick_tile_rect_t *rect, bool pressed)
{
    if (!obj || !rect) {
        return;
    }

    const int32_t inset = pressed ? QUICK_TILE_PRESS_INSET : 0;
    lv_obj_set_pos(obj, rect->x + inset, rect->y + inset);
    lv_obj_set_size(obj, quick_tile_pressed_extent(rect->w, pressed),
                    quick_tile_pressed_extent(rect->h, pressed));
}

static const lv_font_t *quick_panel_item_icon_font(const quick_panel_item_t *item)
{
    if (!item) {
        return &lv_font_montserrat_24;
    }
    if (item->kind == QUICK_ITEM_BLUETOOTH) {
        return &bluetoothon;
    }
    if (item->hotspot_icon) {
        return &wlanJZ;
    }
    return &lv_font_montserrat_24;
}

static const char *quick_panel_item_icon_symbol(const quick_panel_item_t *item)
{
    if (!item) {
        return "";
    }
    if (item->hotspot_icon) {
        return QUICK_HOTSPOT_SYMBOL;
    }
    return item->icon ? item->icon : "";
}

static void quick_panel_item_recenter_icon(uint32_t index, bool pressed)
{
    if (index >= QUICK_PANEL_BUTTON_COUNT) {
        return;
    }

    quick_panel_layout_t *layout = quick_layout_ensure_current();
    const quick_tile_rect_t *rect = &layout->items[index];
    const int32_t w = quick_tile_pressed_extent(rect->w, pressed);
    const int32_t h = quick_tile_pressed_extent(rect->h, pressed);
    const quick_panel_item_t *item = &QUICK_PANEL_ITEMS[index];
    quick_symbol_icon_recenter(s_ui.quick_panel_item_icons[index],
                               w - 8,
                               h - 8,
                               quick_panel_item_icon_symbol(item),
                               quick_panel_item_icon_font(item));
}

static void quick_level_recenter_icon(quick_level_kind_t kind, bool pressed)
{
    quick_panel_layout_t *layout = quick_layout_ensure_current();
    const quick_tile_rect_t *rect = kind == QUICK_LEVEL_VOLUME ? &layout->volume : &layout->brightness;
    const int32_t w = quick_tile_pressed_extent(rect->w, pressed);
    const int32_t h = quick_tile_pressed_extent(rect->h, pressed);
    quick_symbol_icon_recenter(kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_label : s_ui.quick_brightness_label,
                               w - 8,
                               h - 8,
                               quick_level_icon(kind),
                               &lv_font_montserrat_24);
}

static void quick_panel_item_set_pressed(uint32_t index, bool pressed)
{
    if (index >= QUICK_PANEL_BUTTON_COUNT) {
        return;
    }
    quick_panel_layout_t *layout = quick_layout_ensure_current();
    quick_tile_apply_press_inset(s_ui.quick_panel_items[index], &layout->items[index], pressed);
    quick_panel_item_recenter_icon(index, pressed);
}

static void quick_level_set_pressed(quick_level_kind_t kind, bool pressed)
{
    quick_panel_layout_t *layout = quick_layout_ensure_current();
    const quick_tile_rect_t *rect = kind == QUICK_LEVEL_VOLUME ? &layout->volume : &layout->brightness;
    quick_tile_apply_press_inset(quick_level_tile_for_kind(kind), rect, pressed);
    quick_level_recenter_icon(kind, pressed);
}

static void quick_tile_set_scale(lv_obj_t *obj, int32_t scale)
{
    if (!obj) {
        return;
    }
    const bool active = scale != QUICK_TILE_SCALE_NORMAL;
    lv_obj_set_style_border_width(obj, active ? 1 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj,
                                  scale == QUICK_TILE_SCALE_LONG ? COLOR_ACCENT : COLOR_MUTED,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, active ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

static void quick_drag_begin(lv_obj_t *obj, quick_drag_target_kind_t target_kind, uint8_t target_index)
{
    if (!obj || !get_active_pointer(&s_ui.quick_drag_start)) {
        return;
    }
    s_ui.quick_drag_obj = obj;
    s_ui.quick_drag_obj_x = lv_obj_get_x(obj);
    s_ui.quick_drag_obj_y = lv_obj_get_y(obj);
    s_ui.quick_drag_target_kind = target_kind;
    s_ui.quick_drag_target_index = target_index;
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
    quick_layout_commit_drag_sort();
    s_ui.quick_drag_obj = NULL;
    s_ui.quick_drag_target_kind = QUICK_DRAG_TARGET_NONE;
    s_ui.quick_drag_target_index = 0;
    s_ui.quick_drag_moved = false;
    return moved;
}

static void quick_panel_item_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (process_return_swipe_event(code, false)) {
        return;
    }

    const quick_panel_item_t *item = (const quick_panel_item_t *)lv_event_get_user_data(event);
    if (!item) {
        return;
    }

    lv_obj_t *tile = (lv_obj_t *)lv_event_get_target(event);
    const uint32_t index = quick_panel_item_index(item);
    if (!s_ui.quick_panel_interactive) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_panel_item_set_pressed(index, false);
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        if (!s_ui.quick_edit_mode) {
            quick_panel_item_set_pressed(index, true);
        }
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
        if (s_ui.quick_edit_mode) {
            quick_drag_begin(tile, QUICK_DRAG_TARGET_ITEM, (uint8_t)index);
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
        quick_panel_item_set_pressed(index, false);
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
        const bool rebuilds_view = item->click_action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
                                   item->click_action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS ||
                                   item->click_action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD;
        bool should_perform_action = true;
        if (!rebuilds_view && index < QUICK_PANEL_BUTTON_COUNT) {
            if (quick_panel_item_can_stay_active(item)) {
                const bool next_active = !s_ui.quick_panel_item_active[index];
                s_ui.quick_panel_item_local_override[index] = true;
                s_ui.quick_panel_item_local_active[index] = next_active;
                should_perform_action = next_active;
            }
            quick_panel_item_apply_active(index, s_ui.quick_panel_item_local_override[index] ?
                                                 s_ui.quick_panel_item_local_active[index] :
                                                 s_ui.quick_panel_item_active[index]);
        }
        const bool close_panel = item->click_action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
                                 item->click_action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS ||
                                 item->click_action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD;
        if (should_perform_action) {
            perform_ui_action(item->click_action, close_panel);
        }
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

static void quick_symbol_icon_recenter(lv_obj_t *icon,
                                       int32_t content_w,
                                       int32_t content_h,
                                       const char *symbol,
                                       const lv_font_t *font)
{
    if (!icon || !font) {
        return;
    }

    if (content_w < 1) {
        content_w = 1;
    }
    if (content_h < 1) {
        content_h = 1;
    }

    const int32_t label_w = content_w;
    const int32_t label_h = font->line_height;
    int32_t label_x = 0;
    int32_t label_y = (content_h - label_h) / 2;

    const char *text = symbol ? symbol : lv_label_get_text(icon);
    const uint32_t letter = quick_first_utf8_codepoint(text);
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

    lv_obj_set_pos(icon, label_x, label_y);
    lv_obj_set_size(icon, label_w, label_h);
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

    lv_obj_t *icon_label = label(parent, 0, 0, content_w, font->line_height, font);
    lv_label_set_text(icon_label, symbol ? symbol : "");
    quick_symbol_icon_recenter(icon_label, content_w, content_h, symbol, font);
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon_label, LV_OPA_TRANSP, LV_PART_MAIN);
    return icon_label;
}

static void settings_show_root(void)
{
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_NONE;
    s_ui.settings_swipe_tracking = false;
    set_obj_hidden(s_ui.settings_detail, true);
    set_obj_hidden(s_ui.settings_root, false);
    if (s_ui.settings_root) {
        lv_obj_scroll_to_y(s_ui.settings_root, 0, LV_ANIM_OFF);
    }
}

static void settings_swipe_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.settings_swipe_consumed = false;
        s_ui.settings_swipe_tracking = settings_view_is_visible() &&
                                      get_active_pointer(&s_ui.settings_swipe_start);
        return;
    }

    if (code == LV_EVENT_PRESSING && s_ui.settings_swipe_tracking) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }

        const int32_t dx = point.x - s_ui.settings_swipe_start.x;
        const int32_t dy = point.y - s_ui.settings_swipe_start.y;
        if (abs_i32(dy) > SETTINGS_SWIPE_BACK_MAX_DY &&
            abs_i32(dy) > abs_i32(dx)) {
            s_ui.settings_swipe_tracking = false;
            return;
        }

        if (dx >= SETTINGS_SWIPE_BACK_MIN_DX &&
            abs_i32(dy) <= SETTINGS_SWIPE_BACK_MAX_DY) {
            s_ui.settings_swipe_tracking = false;
            s_ui.settings_swipe_consumed = true;
            if (s_ui.settings_detail_id != (uint8_t)SETTINGS_DETAIL_NONE) {
                settings_show_root();
            } else {
                show_dashboard_view();
            }
            lv_indev_wait_release(lv_indev_active());
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.settings_swipe_tracking = false;
    }
}

static void settings_add_swipe_handlers(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_add_event_cb(obj, settings_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, settings_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(obj, settings_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, settings_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

static void settings_detail_action_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.settings_swipe_consumed) {
        return;
    }

    const esp_bms_lvgl_action_t action =
        (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        perform_ui_action(action, false);
    }
}

static const char *settings_detail_title(settings_detail_id_t detail_id)
{
    for (uint32_t index = 0; index < SETTINGS_OPTION_COUNT; ++index) {
        if (SETTINGS_OPTIONS[index].detail_id == detail_id) {
            return SETTINGS_OPTIONS[index].title;
        }
    }
    return "设置";
}

static const settings_detail_row_t *settings_detail_rows_for_id(settings_detail_id_t detail_id,
                                                                size_t *count)
{
    if (count) {
        *count = 0;
    }
    switch (detail_id) {
    case SETTINGS_DETAIL_WIFI:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_WIFI_ROWS);
        }
        return SETTINGS_WIFI_ROWS;
    case SETTINGS_DETAIL_HOTSPOT:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_HOTSPOT_ROWS);
        }
        return SETTINGS_HOTSPOT_ROWS;
    case SETTINGS_DETAIL_BLUETOOTH:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_BLUETOOTH_ROWS);
        }
        return SETTINGS_BLUETOOTH_ROWS;
    case SETTINGS_DETAIL_BMS:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_BMS_ROWS);
        }
        return SETTINGS_BMS_ROWS;
    case SETTINGS_DETAIL_SYSTEM:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_SYSTEM_ROWS);
        }
        return SETTINGS_SYSTEM_ROWS;
    case SETTINGS_DETAIL_ABOUT:
        if (count) {
            *count = ARRAY_SIZE(SETTINGS_ABOUT_ROWS);
        }
        return SETTINGS_ABOUT_ROWS;
    case SETTINGS_DETAIL_NONE:
    default:
        return NULL;
    }
}

static lv_obj_t *settings_detail_row(lv_obj_t *parent,
                                     int32_t x,
                                     int32_t y,
                                     int32_t w,
                                     int32_t h,
                                     const settings_detail_row_t *row)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(box);
    if (row && row->action != ESP_BMS_LVGL_ACTION_NONE) {
        lv_obj_add_event_cb(box, settings_detail_action_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)row->action);
    }

    lv_obj_t *title = label(box, 12, 7, w - 42, 18, &settings_zh_16);
    lv_label_set_text(title, row ? row->title : "");
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_obj_t *subtitle = label(box, 12, 30, w - 42, 16, &settings_zh_16);
    lv_label_set_text(subtitle, row ? row->subtitle : "");
    lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    if (row && row->action != ESP_BMS_LVGL_ACTION_NONE) {
        lv_obj_t *arrow = label(box, w - 24, (h - 16) / 2, 14, 16, &settings_zh_16);
        lv_label_set_text(arrow, ">");
        lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    }
    return box;
}

static void settings_show_detail(settings_detail_id_t detail_id)
{
    if (!s_ui.settings_detail) {
        return;
    }

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)detail_id;
    set_obj_hidden(s_ui.settings_root, true);
    set_obj_hidden(s_ui.settings_detail, false);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    const int32_t card_x = 8;
    const int32_t card_w = s_ui.width - 16;
    const int32_t row_h = s_ui.width < s_ui.height ? 56 : 48;
    const int32_t gap = 8;
    const int32_t first_y = s_ui.width < s_ui.height ? 48 : 42;

    lv_obj_t *title = label(s_ui.settings_detail, 16, 12, s_ui.width - 32, 22, &settings_zh_16);
    lv_label_set_text(title, settings_detail_title(detail_id));
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    size_t row_count = 0;
    const settings_detail_row_t *rows = settings_detail_rows_for_id(detail_id, &row_count);
    for (size_t index = 0; rows && index < row_count; ++index) {
        settings_detail_row(s_ui.settings_detail,
                            card_x,
                            first_y + ((int32_t)index * (row_h + gap)),
                            card_w,
                            row_h,
                            &rows[index]);
    }
}

static void settings_option_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_ui.settings_swipe_consumed) {
        return;
    }

    const settings_detail_id_t detail_id =
        (settings_detail_id_t)(uintptr_t)lv_event_get_user_data(event);
    if (detail_id != SETTINGS_DETAIL_NONE) {
        settings_show_detail(detail_id);
    }
}

static lv_obj_t *settings_option_card(lv_obj_t *parent,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      const settings_option_t *option)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, settings_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(box, settings_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(box, settings_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(box, settings_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(box, settings_option_event_cb, LV_EVENT_CLICKED,
                        option ? (void *)(uintptr_t)option->detail_id : NULL);

    lv_obj_t *icon_box = lv_obj_create(box);
    clear_style(icon_box);
    lv_obj_set_pos(icon_box, 10, 8);
    lv_obj_set_size(icon_box, 36, h - 16);
    lv_obj_set_style_radius(icon_box, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon_box, COLOR_SETTINGS_ICON_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    if (option) {
        lv_obj_t *icon = quick_symbol_icon(icon_box, 36, h - 16, option->icon, option->icon_font);
        lv_obj_set_style_text_color(icon, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    }

    const int32_t text_x = 58;
    lv_obj_t *title = label(box, text_x, 7, w - text_x - 30, 18, &settings_zh_16);
    lv_label_set_text(title, option ? option->title : "");
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_obj_t *subtitle = label(box, text_x, 29, w - text_x - 30, 16, &settings_zh_16);
    lv_label_set_text(subtitle, option ? option->subtitle : "");
    lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    lv_obj_t *arrow = label(box, w - 22, (h - 16) / 2, 14, 16, &settings_zh_16);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    return box;
}

static lv_obj_t *quick_bluetooth_icon(lv_obj_t *parent, int32_t w, int32_t h)
{
    const int32_t content_w = w - 8;
    const int32_t content_h = h - 8;
    lv_obj_t *icon = quick_symbol_icon(parent,
                                       content_w,
                                       content_h,
                                       QUICK_BLUETOOTH_SYMBOL,
                                       &bluetoothon);
    quick_bluetooth_icon_set_active(icon, false);
    return icon;
}

static lv_obj_t *quick_hotspot_icon(lv_obj_t *parent, int32_t w, int32_t h)
{
    const int32_t content_w = w - 8;
    const int32_t content_h = h - 8;
    lv_obj_t *icon = quick_symbol_icon(parent,
                                       content_w,
                                       content_h,
                                       QUICK_HOTSPOT_SYMBOL,
                                       &wlanJZ);
    quick_hotspot_icon_set_active(icon, false);
    return icon;
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
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
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
        const bool snapshot_active = quick_item_active_from_snapshot(&QUICK_PANEL_ITEMS[index], snapshot);
        const bool active = s_ui.quick_panel_item_local_override[index] ?
                                s_ui.quick_panel_item_local_active[index] :
                                snapshot_active;
        quick_panel_item_apply_active(index, active);
    }
}

static lv_obj_t *quick_level_tile_for_kind(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_tile : s_ui.quick_brightness_tile;
}

static bool quick_level_overlay_matches(quick_level_kind_t kind)
{
    return s_ui.quick_level_overlay_active &&
           s_ui.quick_level_overlay_kind == (uint8_t)kind;
}

static void quick_level_overlay_show(quick_level_kind_t kind)
{
    s_ui.quick_level_overlay_kind = (uint8_t)kind;
    s_ui.quick_level_overlay_active = true;
    s_ui.quick_level_overlay_dragged = false;
    s_ui.return_swipe_tracking = false;
    s_ui.return_swipe_drag_dy = 0;
    s_ui.return_swipe_cancelled = false;
    s_ui.quick_panel_interactive = false;
    if (s_ui.quick_level_overlay) {
        set_obj_hidden(s_ui.quick_level_overlay, false);
        lv_obj_move_foreground(s_ui.quick_level_overlay);
    }
    quick_level_overlay_update(kind, quick_level_current_value(kind));
}

static void quick_level_overlay_hide(void)
{
    s_ui.quick_level_overlay_active = false;
    s_ui.quick_level_overlay_dragged = false;
    s_ui.quick_panel_interactive = s_ui.quick_panel_open && !s_ui.quick_panel_settling;
    set_obj_hidden(s_ui.quick_level_overlay, true);
}

static bool quick_level_set_from_pointer(quick_level_kind_t kind, bool committed)
{
    lv_point_t point = { 0 };
    lv_obj_t *track = s_ui.quick_level_overlay_track;
    if (!quick_level_overlay_matches(kind) || !get_active_pointer(&point) || !track) {
        return false;
    }

    lv_area_t area = { 0 };
    lv_obj_get_coords(track, &area);
    const int32_t min_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const int32_t max_value = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t range = max_value - min_value;
    int32_t percent = min_value;
    if (s_ui.quick_level_overlay_horizontal) {
        const int32_t track_w = area.x2 - area.x1 + 1;
        const int32_t offset = point.x - area.x1;
        percent = min_value + ((offset * range) / (track_w > 1 ? track_w - 1 : 1));
    } else {
        const int32_t track_h = area.y2 - area.y1 + 1;
        const int32_t offset = area.y2 - point.y;
        percent = min_value + ((offset * range) / (track_h > 1 ? track_h - 1 : 1));
    }
    const uint8_t snapped = quick_level_snap_drag_value(kind, percent);
    s_ui.quick_level_overlay_dragged = true;

    if (kind == QUICK_LEVEL_VOLUME) {
        set_quick_volume_value(snapped, true, committed);
    } else {
        set_quick_brightness_value(snapped, true, committed);
    }
    return true;
}

static void quick_level_overlay_event_cb(lv_event_t *event)
{
    if (!s_ui.quick_level_overlay_active) {
        return;
    }

    const quick_level_kind_t kind = (quick_level_kind_t)s_ui.quick_level_overlay_kind;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSING) {
        (void)quick_level_set_from_pointer(kind, false);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (code == LV_EVENT_RELEASED && s_ui.quick_level_overlay_dragged) {
            (void)quick_level_set_from_pointer(kind, true);
        }
        quick_level_overlay_hide();
        quick_level_set_pressed(kind, false);
        quick_tile_set_scale(quick_level_tile_for_kind(kind), QUICK_TILE_SCALE_NORMAL);
        s_ui.quick_level_long_triggered = false;
    }
}

static void quick_level_cycle_preset(quick_level_kind_t kind)
{
    const uint8_t next = quick_level_next_preset(kind);
    if (kind == QUICK_LEVEL_VOLUME) {
        set_quick_volume_value(next, true, true);
    } else {
        set_quick_brightness_value(next, true, true);
    }
    quick_toast_show(kind, next);
}

static void quick_level_event_cb(lv_event_t *event)
{
    const quick_level_kind_t kind = (quick_level_kind_t)(uintptr_t)lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *tile = quick_level_tile_for_kind(kind);
    if (!tile) {
        return;
    }

    const bool overlay_for_this_tile = quick_level_overlay_matches(kind);
    if (!overlay_for_this_tile && !s_ui.quick_panel_interactive) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_level_set_pressed(kind, false);
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        }
        return;
    }

    if (!overlay_for_this_tile && process_return_swipe_event(code, false)) {
        return;
    }

    if (s_ui.quick_edit_mode) {
        if (code == LV_EVENT_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
            quick_drag_begin(tile,
                             kind == QUICK_LEVEL_VOLUME ? QUICK_DRAG_TARGET_VOLUME :
                                                          QUICK_DRAG_TARGET_BRIGHTNESS,
                             0);
        } else if (code == LV_EVENT_PRESSING) {
            quick_drag_update();
        } else if (code == LV_EVENT_LONG_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
            (void)quick_drag_end();
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        s_ui.quick_level_long_triggered = false;
        quick_level_set_pressed(kind, true);
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        s_ui.quick_level_long_triggered = true;
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
        quick_toast_cancel();
        set_obj_hidden(s_ui.quick_toast, true);
        quick_level_overlay_show(kind);
    } else if (overlay_for_this_tile && code == LV_EVENT_PRESSING) {
        (void)quick_level_set_from_pointer(kind, false);
    } else if (overlay_for_this_tile &&
               (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)) {
        if (code == LV_EVENT_RELEASED && s_ui.quick_level_overlay_dragged) {
            (void)quick_level_set_from_pointer(kind, true);
        }
        quick_level_overlay_hide();
        quick_level_set_pressed(kind, false);
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        if (code == LV_EVENT_PRESS_LOST) {
            s_ui.quick_level_long_triggered = false;
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        quick_level_set_pressed(kind, false);
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        s_ui.quick_level_long_triggered = false;
    } else if (code == LV_EVENT_RELEASED) {
        quick_level_set_pressed(kind, false);
        quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
    } else if (code == LV_EVENT_CLICKED) {
        if (s_ui.quick_level_long_triggered) {
            s_ui.quick_level_long_triggered = false;
            return;
        }
        quick_level_cycle_preset(kind);
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
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)kind);
    lv_obj_add_event_cb(box, quick_level_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)kind);

    lv_obj_t *level_label = quick_symbol_icon(box,
                                              w - 8,
                                              h - 8,
                                              quick_level_icon(kind),
                                              &lv_font_montserrat_24);
    if (level_label) {
        lv_obj_set_style_text_color(level_label, COLOR_TEXT, LV_PART_MAIN);
    }

    if (kind == QUICK_LEVEL_VOLUME) {
        s_ui.quick_volume_tile = box;
        s_ui.quick_volume_label = level_label;
        s_ui.quick_volume_track = NULL;
        s_ui.quick_volume_fill = NULL;
        s_ui.quick_volume_knob = NULL;
        set_quick_volume_value(value, false, true);
    } else {
        s_ui.quick_brightness_tile = box;
        s_ui.quick_brightness_label = level_label;
        s_ui.quick_brightness_track = NULL;
        s_ui.quick_brightness_fill = NULL;
        s_ui.quick_brightness_knob = NULL;
        set_quick_brightness_value(value, false, true);
    }
    return box;
}

static void quick_level_overlay_create(lv_obj_t *parent)
{
    s_ui.quick_level_overlay = lv_obj_create(parent);
    clear_style(s_ui.quick_level_overlay);
    lv_obj_set_pos(s_ui.quick_level_overlay, 0, 0);
    lv_obj_set_size(s_ui.quick_level_overlay, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_flag(s_ui.quick_level_overlay, LV_OBJ_FLAG_HIDDEN);

    s_ui.quick_level_overlay_horizontal = s_ui.width > s_ui.height;
    const int32_t track_w = s_ui.quick_level_overlay_horizontal ?
                            clamp_i32((s_ui.width * 2) / 3, 120, s_ui.width - 48) :
                            22;
    const int32_t track_h = s_ui.quick_level_overlay_horizontal ?
                            22 :
                            clamp_i32((s_ui.height * 2) / 3, 112, s_ui.height - 64);
    const int32_t track_x = (s_ui.width - track_w) / 2;
    const int32_t track_y = (s_ui.height - track_h) / 2;
    const int32_t value_y = track_y > 34 ? track_y - 34 : 4;

    s_ui.quick_level_overlay_value = label(s_ui.quick_level_overlay,
                                           0,
                                           value_y,
                                           s_ui.width,
                                           26,
                                           &lv_font_montserrat_24);
    lv_obj_set_style_text_align(s_ui.quick_level_overlay_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.quick_level_overlay_value, COLOR_ACCENT, LV_PART_MAIN);

    s_ui.quick_level_overlay_track = lv_obj_create(s_ui.quick_level_overlay);
    clear_style(s_ui.quick_level_overlay_track);
    lv_obj_set_pos(s_ui.quick_level_overlay_track, track_x, track_y);
    lv_obj_set_size(s_ui.quick_level_overlay_track, track_w, track_h);
    lv_obj_set_style_radius(s_ui.quick_level_overlay_track,
                            (s_ui.quick_level_overlay_horizontal ? track_h : track_w) / 2,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay_track, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_ui.quick_level_overlay_track, true, LV_PART_MAIN);

    s_ui.quick_level_overlay_fill = lv_obj_create(s_ui.quick_level_overlay_track);
    clear_style(s_ui.quick_level_overlay_fill);
    lv_obj_set_style_radius(s_ui.quick_level_overlay_fill,
                            (s_ui.quick_level_overlay_horizontal ? track_h : track_w) / 2,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay_fill, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_fill, LV_OPA_COVER, LV_PART_MAIN);

    s_ui.quick_level_overlay_knob = lv_obj_create(s_ui.quick_level_overlay);
    clear_style(s_ui.quick_level_overlay_knob);
    lv_obj_set_size(s_ui.quick_level_overlay_knob, 20, 20);
    lv_obj_set_style_radius(s_ui.quick_level_overlay_knob, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay_knob, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_knob, LV_OPA_COVER, LV_PART_MAIN);
}

static void quick_toast_create(lv_obj_t *parent)
{
    const int32_t toast_w = s_ui.width < 150 ? s_ui.width - 24 : 126;
    const int32_t toast_h = 28;
    s_ui.quick_toast = label(parent,
                             (s_ui.width - toast_w) / 2,
                             s_ui.height - toast_h - 18,
                             toast_w,
                             toast_h,
                             &lv_font_montserrat_14);
    lv_obj_set_style_radius(s_ui.quick_toast, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_toast, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.quick_toast, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_ui.quick_toast, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_ui.quick_toast, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_ui.quick_toast, 4, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_toast, LV_OBJ_FLAG_HIDDEN);
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
    if (s_ui.setup_ap_info) {
        const char *ssid = snapshot->setup_ap_ssid[0] != '\0' ? snapshot->setup_ap_ssid : "--";
        const char *password = snapshot->setup_ap_password[0] != '\0' ? snapshot->setup_ap_password : "--";
        label_set_text_fmt_if_changed(s_ui.setup_ap_info, "SETUP %s\nSSID %.31s\nPW %.8s",
                                      snapshot->setup_ap_enabled ? "ON" : "OFF",
                                      ssid,
                                      password);
    }

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
    char delta_cell[16];
    char t1[8];
    char t2[8];
    char t3[8];
    char t4[8];
    char mos[8];
    char bal[8];
    uint8_t soc_percent = 0U;
    bool soc_valid = false;

    if (snapshot->soc_valid) {
        const uint16_t soc = snapshot->soc_percent > 100 ? 100 : snapshot->soc_percent;
        soc_percent = (uint8_t)soc;
        soc_valid = true;
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
        label_set_text_fmt_if_changed(s_ui.soc, "%u%%", soc);
    } else {
        (void)snprintf(ah, sizeof(ah), "--/--Ah");
        label_set_text_if_changed(s_ui.soc, "--");
    }
    label_set_text_if_changed(s_ui.capacity, ah);

    format_mv(voltage, sizeof(voltage), snapshot->pack_voltage_valid, snapshot->pack_voltage_mv);
    format_deci_amps(current, sizeof(current), snapshot->current_valid, snapshot->current_deci_amps);
    update_dashboard_soc_fill(soc_percent,
                              soc_valid,
                              snapshot->current_valid && snapshot->current_deci_amps > 0);
    label_set_text_if_changed(s_ui.pack_voltage, voltage);
    label_set_text_if_changed(s_ui.current, current);

    format_cell_v(min_cell, sizeof(min_cell), snapshot->min_cell_valid, snapshot->min_cell_voltage_mv);
    format_cell_v(avg_cell, sizeof(avg_cell), snapshot->average_cell_valid, snapshot->average_cell_voltage_mv);
    format_cell_v(max_cell, sizeof(max_cell), snapshot->max_cell_valid, snapshot->max_cell_voltage_mv);
    if (snapshot->delta_cell_valid) {
        (void)snprintf(delta_cell, sizeof(delta_cell), "%umV", snapshot->delta_cell_voltage_mv);
    } else {
        (void)snprintf(delta_cell, sizeof(delta_cell), "--");
    }
    label_set_text_if_changed(s_ui.cell_stat_values[0], max_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[1], min_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[2], delta_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[3], avg_cell);

    if (snapshot->bms_protection_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_BAD);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\nPROT %.7s", snapshot->bms_protection_codes[0]);
    } else if (snapshot->bms_warning_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_BAD);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\nWARN %.7s", snapshot->bms_warning_codes[0]);
    } else if (snapshot->bms_info_text[0] != '\0') {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_WARN);
        label_set_text_fmt_if_changed(s_ui.bms_error, "BMS INFO\n%.15s", snapshot->bms_info_text);
    } else {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_ACCENT);
        label_set_text_if_changed(s_ui.bms_error, "BMS INFO\nOK");
    }

    format_temp_c(t1, sizeof(t1), snapshot->bms_temperature_valid[0], snapshot->bms_temperature_celsius[0]);
    format_temp_c(t2, sizeof(t2), snapshot->bms_temperature_valid[1], snapshot->bms_temperature_celsius[1]);
    format_temp_c(t3, sizeof(t3), snapshot->bms_temperature_valid[2], snapshot->bms_temperature_celsius[2]);
    format_temp_c(t4, sizeof(t4), snapshot->bms_temperature_valid[3], snapshot->bms_temperature_celsius[3]);
    format_temp_c(mos, sizeof(mos), snapshot->bms_temperature_valid[4], snapshot->bms_temperature_celsius[4]);
    format_temp_c(bal, sizeof(bal), snapshot->bms_temperature_valid[5], snapshot->bms_temperature_celsius[5]);
    label_set_text_if_changed(s_ui.temperature_values[0], t1);
    label_set_text_if_changed(s_ui.temperature_values[1], t2);
    label_set_text_if_changed(s_ui.temperature_values[2], t3);
    label_set_text_if_changed(s_ui.temperature_values[3], t4);
    label_set_text_if_changed(s_ui.temperature_values[4], bal);
    label_set_text_if_changed(s_ui.temperature_values[5], mos);

    set_setup_ap(snapshot);
    set_quick_brightness_value(snapshot->brightness_percent, false, true);
    set_quick_volume_value(snapshot->volume_percent, false, true);
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
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESS_LOST, NULL);

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
        lv_obj_t *soc_panel = panel(s_ui.battery_page, 8, 8, 108, 112, COLOR_PANEL);
        s_ui.soc_fill_horizontal = false;
        dashboard_soc_fill_create(soc_panel);
        s_ui.soc = label(soc_panel, 4, 38, 100, 30, &lv_font_montserrat_24);
        s_ui.capacity = label(soc_panel, 4, 70, 100, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = panel(s_ui.battery_page, 124, 8, 108, 112, COLOR_PANEL);
        s_ui.pack_voltage = label(pack_panel, 4, 26, 100, 30, &lv_font_montserrat_24);
        s_ui.current = label(pack_panel, 4, 62, 100, 30, &lv_font_montserrat_24);

        s_ui.bms_error = panel_label(s_ui.battery_page, 8, 128, 108, 120, COLOR_PANEL, &lv_font_montserrat_14);

        lv_obj_t *cell_panel = panel(s_ui.battery_page, 124, 128, 108, 120, COLOR_PANEL);
        for (uint8_t index = 0; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
            const int32_t row_y = 6 + ((int32_t)index * 26);
            lv_obj_t *key = dashboard_cell_key(cell_panel, 11, row_y + 2, index);
            if (index == 0U) {
                s_ui.cell_stats = key;
            }
            s_ui.cell_stat_values[index] = label(cell_panel, 55, row_y, 42, 20, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        }

        lv_obj_t *temp_panel = panel(s_ui.battery_page, 8, 256, content_w, 56, COLOR_PANEL);
        const int32_t temp_col_w = content_w / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
        for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            const int32_t col_x = (int32_t)index * temp_col_w;
            lv_obj_t *key = label(temp_panel, col_x, 7, temp_col_w, 18, &lv_font_montserrat_14);
            lv_label_set_text(key, DASHBOARD_TEMP_KEYS[index]);
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (index == 0U) {
                s_ui.temperature = key;
            }
            s_ui.temperature_values[index] = label(temp_panel, col_x, 31, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
    } else {
        lv_obj_t *soc_panel = panel(s_ui.battery_page, 8, 8, 148, 84, COLOR_PANEL);
        s_ui.soc_fill_horizontal = true;
        dashboard_soc_fill_create(soc_panel);
        s_ui.soc = label(soc_panel, 4, 25, 140, 30, &lv_font_montserrat_24);
        s_ui.capacity = label(soc_panel, 4, 54, 140, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = panel(s_ui.battery_page, 164, 8, 148, 84, COLOR_PANEL);
        s_ui.pack_voltage = label(pack_panel, 4, 15, 140, 30, &lv_font_montserrat_24);
        s_ui.current = label(pack_panel, 4, 47, 140, 30, &lv_font_montserrat_24);

        s_ui.bms_error = panel_label(s_ui.battery_page, 8, 100, 148, 70, COLOR_PANEL, &lv_font_montserrat_14);

        lv_obj_t *cell_panel = panel(s_ui.battery_page, 164, 100, 148, 70, COLOR_PANEL);
        for (uint8_t index = 0; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
            const int32_t row_y = 2 + ((int32_t)index * 16);
            lv_obj_t *key = dashboard_cell_key(cell_panel, 20, row_y, index);
            if (index == 0U) {
                s_ui.cell_stats = key;
            }
            s_ui.cell_stat_values[index] = label(cell_panel, 86, row_y, 42, 16, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        }

        lv_obj_t *temp_panel = panel(s_ui.battery_page, 8, 178, 304, 54, COLOR_PANEL);
        const int32_t temp_col_w = 304 / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
        const int32_t temp_left = (304 - (temp_col_w * (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT)) / 2;
        for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            const int32_t col_x = temp_left + ((int32_t)index * temp_col_w);
            lv_obj_t *key = label(temp_panel, col_x, 6, temp_col_w, 18, &lv_font_montserrat_14);
            lv_label_set_text(key, DASHBOARD_TEMP_KEYS[index]);
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (index == 0U) {
                s_ui.temperature = key;
            }
            s_ui.temperature_values[index] = label(temp_panel, col_x, 29, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
    }
    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.current, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_ui.settings_page = lv_obj_create(screen);
    clear_style(s_ui.settings_page);
    lv_obj_set_pos(s_ui.settings_page, 0, settings_y);
    lv_obj_set_size(s_ui.settings_page, s_ui.width, settings_h);
    lv_obj_set_style_bg_color(s_ui.settings_page, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(s_ui.settings_page);

    s_ui.settings_root = lv_obj_create(s_ui.settings_page);
    clear_style(s_ui.settings_root);
    lv_obj_set_pos(s_ui.settings_root, 0, 0);
    lv_obj_set_size(s_ui.settings_root, s_ui.width, settings_h);
    lv_obj_set_style_bg_color(s_ui.settings_root, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ui.settings_root, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                          LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(s_ui.settings_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.settings_root, LV_SCROLLBAR_MODE_AUTO);
    settings_add_swipe_handlers(s_ui.settings_root);

    lv_obj_t *settings_title = label(s_ui.settings_root, 16, 12, s_ui.width - 32, 22,
                                     &settings_zh_16);
    lv_label_set_text(settings_title, "设置");
    lv_obj_set_style_text_color(settings_title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    const int32_t card_x = 8;
    const int32_t card_w = s_ui.width - 16;
    const int32_t card_h = portrait ? 56 : 48;
    const int32_t card_gap = 8;
    const int32_t first_card_y = portrait ? 48 : 42;
    for (uint32_t index = 0; index < SETTINGS_OPTION_COUNT; ++index) {
        settings_option_card(s_ui.settings_root,
                             card_x,
                             first_card_y + ((int32_t)index * (card_h + card_gap)),
                             card_w,
                             card_h,
                             &SETTINGS_OPTIONS[index]);
    }
    s_ui.settings_detail = lv_obj_create(s_ui.settings_page);
    clear_style(s_ui.settings_detail);
    lv_obj_set_pos(s_ui.settings_detail, 0, 0);
    lv_obj_set_size(s_ui.settings_detail, s_ui.width, settings_h);
    lv_obj_set_style_bg_color(s_ui.settings_detail, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_detail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_detail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ui.settings_detail, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                            LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(s_ui.settings_detail, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.settings_detail, LV_SCROLLBAR_MODE_AUTO);
    settings_add_swipe_handlers(s_ui.settings_detail);
    lv_obj_add_flag(s_ui.settings_detail, LV_OBJ_FLAG_HIDDEN);
    s_ui.setup_ap_info = NULL;
    s_ui.setup_ap_qr = NULL;
    s_ui.current_setup_ap_qr_payload[0] = '\0';
    settings_show_root();
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);

    quick_panel_layout_t *quick_layout = quick_layout_ensure_current();

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

    (void)quick_level_tile(s_ui.quick_panel,
                           quick_layout->brightness.x,
                           quick_layout->brightness.y,
                           quick_layout->brightness.w,
                           quick_layout->brightness.h,
                           QUICK_LEVEL_BRIGHTNESS,
                           85U);
    (void)quick_level_tile(s_ui.quick_panel,
                           quick_layout->volume.x,
                           quick_layout->volume.y,
                           quick_layout->volume.w,
                           quick_layout->volume.h,
                           QUICK_LEVEL_VOLUME,
                           65U);

    s_ui.quick_edit_button = panel(s_ui.quick_panel,
                                   s_ui.width - QUICK_EDIT_BUTTON_SIZE - 8,
                                   8,
                                   QUICK_EDIT_BUTTON_SIZE,
                                   QUICK_EDIT_BUTTON_SIZE,
                                   COLOR_PANEL_ALT);
    lv_obj_set_style_radius(s_ui.quick_edit_button, 8, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_edit_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.quick_edit_icon = quick_symbol_icon(s_ui.quick_edit_button, 20, 20, LV_SYMBOL_EDIT, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_ui.quick_edit_icon, COLOR_MUTED, LV_PART_MAIN);

    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        const quick_panel_item_t *item = &QUICK_PANEL_ITEMS[index];
        const quick_tile_rect_t *rect = &quick_layout->items[index];
        s_ui.quick_panel_items[index] = quick_panel_tile(s_ui.quick_panel,
                                                         rect->x,
                                                         rect->y,
                                                         rect->w,
                                                         rect->h,
                                                         index,
                                                         item);
    }
    set_quick_edit_mode(false);
    lv_obj_add_flag(s_ui.quick_panel, LV_OBJ_FLAG_HIDDEN);
    quick_level_overlay_create(screen);
    quick_toast_create(screen);

    const int32_t pull_w = s_ui.width;
    const int32_t pull_h = portrait ? QUICK_PULL_ZONE_PORTRAIT_H : QUICK_PULL_ZONE_LANDSCAPE_H;
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
    const quick_panel_layout_t quick_layouts[QUICK_LAYOUT_COUNT] = {
        s_ui.quick_layouts[QUICK_LAYOUT_PORTRAIT],
        s_ui.quick_layouts[QUICK_LAYOUT_LANDSCAPE],
    };
    lv_display_t *display = s_ui.display;

    lv_indev_reset(NULL, NULL);
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
    }
    quick_toast_cancel();
    dashboard_soc_wave_stop();
    if (old_root) {
        lv_obj_delete(old_root);
    }

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.display = display;
    s_ui.pending_event = pending_event;
    memcpy(s_ui.quick_layouts, quick_layouts, sizeof(s_ui.quick_layouts));
    s_ui.initialized = true;
    create_screen(display);
    move_to_page(page, false);
    if (settings_visible) {
        show_settings_view();
    } else {
        show_dashboard_view();
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
