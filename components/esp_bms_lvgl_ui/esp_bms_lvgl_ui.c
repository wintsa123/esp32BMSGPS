#include "esp_bms_lvgl_ui.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_lvgl_contract.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "widgets/canvas/lv_canvas.h"
#include "widgets/line/lv_line.h"

static const char *TAG = "bms_lvgl_ui";

LV_FONT_DECLARE(bluetoothon);
LV_FONT_DECLARE(wlanJZ);
LV_FONT_DECLARE(controller_digits_72);
LV_FONT_DECLARE(settings_zh_10);
LV_FONT_DECLARE(settings_zh_13);
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
#define SETUP_AP_INFO_LINE_SPACE 4
#define QUICK_PULL_OPEN_DY 34
#define QUICK_PULL_MAX_DX 64
#define QUICK_PULL_ZONE_PORTRAIT_H 88
#define QUICK_PULL_ZONE_LANDSCAPE_H 64
#define QUICK_PULL_START_MAX_Y_NUM 4
#define QUICK_PULL_START_MAX_Y_DEN 5
#define QUICK_PANEL_SETTLE_MS 140
#define QUICK_TILE_REORDER_MS 120
#define RETURN_HOME_SWIPE_MIN_DY 58
#define RETURN_HOME_SWIPE_MAX_DX 46
#define RETURN_HOME_START_MIN_Y_NUM 3
#define RETURN_HOME_START_MIN_Y_DEN 4
#define RETURN_HOME_RIGHT_CANCEL_MIN_DX 42
#define RETURN_HOME_RIGHT_CANCEL_MAX_DY 42
#define SETTINGS_SWIPE_BACK_MIN_DX 36
#define SETTINGS_SWIPE_BACK_MAX_DY 64
#define SETTINGS_SWIPE_EDGE_WIDTH 56
#define SETTINGS_SWIPE_INDICATOR_SIZE 42
#define SETTINGS_SWIPE_INDICATOR_SETTLE_MS 140
#define SETTINGS_DETAIL_HEADER_H 38
#define SETTINGS_NAV_SCROLL_THRESHOLD 12
#define SETTINGS_NAV_ANIM_MS 160
#define SETTINGS_LIST_ROW_H_PORTRAIT 44
#define SETTINGS_LIST_ROW_H_LANDSCAPE 38
#define SETTINGS_LIST_PAD_Y 4
#define QUICK_BRIGHTNESS_MIN 10
#define QUICK_BRIGHTNESS_MAX 100
#define QUICK_VOLUME_MIN 0
#define QUICK_VOLUME_MAX 100
#define QUICK_LEVEL_DRAG_STEP 5
#define QUICK_LEVEL_SAVE_DELAY_MS 2000U
#define QUICK_LEVEL_OVERLAY_FADE_MS 140U
#define QUICK_TOAST_MS 950
#define QUICK_ROTATE_TOAST_TICK_MS 1000U
#define QUICK_TOAST_SORT_HINT "快捷面板调节"
#define QUICK_BRIGHTNESS_TOAST_HINT "亮度调节"
#define QUICK_VOLUME_TOAST_HINT "音量调节"
#define QUICK_ROTATE_TOAST_TITLE "自动保存"
#define QUICK_PANEL_ITEM_COUNT QUICK_PANEL_BUTTON_COUNT
#define QUICK_TILE_PRESS_INSET 4
#define QUICK_TILE_SCALE_NORMAL 256
#define QUICK_TILE_SCALE_PRESSED 270
#define QUICK_TILE_SCALE_LONG 292
#define SCREEN_LOCK_PROMPT_TIMEOUT_MS 3000U
#define SCREEN_LOCK_TAP_MAX_MOVE 14
#define SCREEN_UNLOCK_THRESHOLD_PERCENT 85
#define SCREEN_UNLOCK_TRACK_H 56
#define SCREEN_UNLOCK_KNOB_SIZE 48
#define SCREEN_UNLOCK_TOUCH_MARGIN 20
#define QUICK_LOCK_ICON_W 24
#define QUICK_LOCK_ICON_H 28
#define DASHBOARD_CELL_STAT_COUNT 4U
#define SPEED_DASHBOARD_SEGMENT_COUNT 32U
#define SPEED_DASHBOARD_SCALE_LABEL_COUNT 6U
#define DASHBOARD_CELL_KEY_BITMAP_W 28
#define DASHBOARD_CELL_KEY_BITMAP_H 16
#define DASHBOARD_CELL_KEY_BITMAP_BYTES \
    (((DASHBOARD_CELL_KEY_BITMAP_W * DASHBOARD_CELL_KEY_BITMAP_H) + 7U) / 8U)

typedef enum {
    QUICK_LAYOUT_PORTRAIT = 0,
    QUICK_LAYOUT_LANDSCAPE = 1,
    QUICK_LAYOUT_COUNT = 2,
} quick_layout_orientation_t;

_Static_assert(QUICK_PANEL_CONTROL_COUNT <= QUICK_PANEL_GRID_SLOT_COUNT,
               "quick panel defaults must fit in a 2x4 grid");
_Static_assert(QUICK_PANEL_BUTTON_COUNT <= 8,
               "quick panel item masks are stored in uint8_t");
_Static_assert(DASHBOARD_CELL_STAT_COUNT <= 8,
               "dashboard cell draw buffer masks are stored in uint8_t");
_Static_assert((ESP_BMS_LVGL_ROTATE_SAVE_DELAY_MS % QUICK_ROTATE_TOAST_TICK_MS) == 0U,
               "rotate toast countdown expects whole-second ticks");

typedef enum {
    UI_STATE_FLAG_DRAGGING = UINT32_C(1) << 0,
    UI_STATE_FLAG_SETTLING = UINT32_C(1) << 1,
    UI_STATE_FLAG_DEFERRED_SNAPSHOT_VALID = UINT32_C(1) << 2,
    UI_STATE_FLAG_LAST_SNAPSHOT_VALID = UINT32_C(1) << 3,
    UI_STATE_FLAG_QUICK_PANEL_OPEN = UINT32_C(1) << 4,
    UI_STATE_FLAG_QUICK_PANEL_INTERACTIVE = UINT32_C(1) << 5,
    UI_STATE_FLAG_QUICK_PANEL_SETTLING = UINT32_C(1) << 6,
    UI_STATE_FLAG_QUICK_PANEL_ANIMATION_TARGET_OPEN = UINT32_C(1) << 7,
    UI_STATE_FLAG_QUICK_PULL_TRACKING = UINT32_C(1) << 8,
    UI_STATE_FLAG_RETURN_SWIPE_TRACKING = UINT32_C(1) << 9,
    UI_STATE_FLAG_RETURN_SWIPE_CANCELLED = UINT32_C(1) << 10,
    UI_STATE_FLAG_SETTINGS_SWIPE_TRACKING = UINT32_C(1) << 11,
    UI_STATE_FLAG_SETTINGS_SWIPE_CONSUMED = UINT32_C(1) << 12,
    UI_STATE_FLAG_QUICK_EDIT_MODE = UINT32_C(1) << 13,
    UI_STATE_FLAG_QUICK_DRAG_MOVED = UINT32_C(1) << 14,
    UI_STATE_FLAG_QUICK_LONG_TRIGGERED = UINT32_C(1) << 15,
    UI_STATE_FLAG_SCREEN_LOCKED = UINT32_C(1) << 16,
    UI_STATE_FLAG_SCREEN_UNLOCK_PROMPT_VISIBLE = UINT32_C(1) << 17,
    UI_STATE_FLAG_SCREEN_UNLOCK_DRAGGING = UINT32_C(1) << 18,
    UI_STATE_FLAG_QUICK_LEVEL_OVERLAY_ACTIVE = UINT32_C(1) << 19,
    UI_STATE_FLAG_QUICK_LEVEL_OVERLAY_DRAGGED = UINT32_C(1) << 20,
    UI_STATE_FLAG_QUICK_LEVEL_OVERLAY_HORIZONTAL = UINT32_C(1) << 21,
    UI_STATE_FLAG_QUICK_LEVEL_LONG_TRIGGERED = UINT32_C(1) << 22,
    UI_STATE_FLAG_INITIALIZED = UINT32_C(1) << 26,
    UI_STATE_FLAG_QUICK_ROTATE_TOAST_ACTIVE = UINT32_C(1) << 27,
    UI_STATE_FLAG_QUICK_LEVEL_POINTER_ACTIVE = UINT32_C(1) << 28,
} ui_state_flag_t;

typedef enum {
    QUICK_DRAG_TARGET_NONE = 0,
    QUICK_DRAG_TARGET_BRIGHTNESS,
    QUICK_DRAG_TARGET_VOLUME,
    QUICK_DRAG_TARGET_ITEM,
} quick_drag_target_kind_t;

typedef enum {
    SETTINGS_BLE_SOURCE_BMS = 0,
    SETTINGS_BLE_SOURCE_CONTROLLER,
} settings_ble_source_t;

typedef enum {
    SETTINGS_CONTROLLER_VIEW_ROOT = 0,
    SETTINGS_CONTROLLER_VIEW_BLE_LIST,
    SETTINGS_CONTROLLER_VIEW_TIRE_EDIT,
    SETTINGS_CONTROLLER_VIEW_RATIO_EDIT,
} settings_controller_view_t;

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

static uint8_t ui_flag_bit(uint32_t index)
{
    return (uint8_t)(1U << index);
}

static bool ui_flag_get(uint8_t flags, uint32_t index)
{
    return (flags & ui_flag_bit(index)) != 0U;
}

static void ui_flag_set(uint8_t *flags, uint32_t index, bool enabled)
{
    const uint8_t bit = ui_flag_bit(index);
    if (enabled) {
        *flags |= bit;
    } else {
        *flags &= (uint8_t)~bit;
    }
}

#define SNAPSHOT_FLAG(snapshot, name) \
    esp_bms_dashboard_snapshot_flag_get((snapshot), ESP_BMS_DASHBOARD_FLAG_##name)
#define ACTION_EVENT_SET_FLAG(event, name, enabled) \
    esp_bms_lvgl_action_event_flag_set((event), ESP_BMS_LVGL_ACTION_EVENT_FLAG_##name, (enabled))

_Static_assert(sizeof(esp_bms_lvgl_action_t) == 4,
               "esp_bms_lvgl_action_t ABI size changed; update C action consumers too");
_Static_assert(sizeof(esp_bms_lvgl_data_source_t) == 4,
               "esp_bms_lvgl_data_source_t ABI size changed; update C data-source consumers too");
_Static_assert(ESP_BMS_LVGL_DATA_SOURCE_NONE == 0 &&
                   ESP_BMS_LVGL_DATA_SOURCE_BMS == 1 &&
                   ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER == 2 &&
                   ESP_BMS_LVGL_DATA_SOURCE_GPS == 3 &&
                   ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD == 4,
               "esp_bms_lvgl_data_source_t value changed; update runtime consumers too");
_Static_assert(sizeof(esp_bms_speed_source_t) == 4 &&
                   ESP_BMS_SPEED_SOURCE_GPS == 0 &&
                   ESP_BMS_SPEED_SOURCE_CONTROLLER == 1,
               "esp_bms_speed_source_t ABI changed; update runtime and Web consumers too");
_Static_assert(sizeof(esp_bms_dashboard_snapshot_t) == 1032,
               "dashboard snapshot ABI size changed; update all C consumers too");
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
_Static_assert(ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT == 13,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SELECT_BMS_JK == 14,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD == 15,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY == 16,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING == 17,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION == 18,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION == 19,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE == 20,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION == 21,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE == 27,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO == 28,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE == 29,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");

typedef struct {
    lv_display_t *display;
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *pages;
    lv_obj_t *battery_page;
    lv_obj_t *gps_page;
    lv_obj_t *cast_page;
    lv_obj_t *cast_qr;
    lv_obj_t *controller_page;
    lv_obj_t *settings_page;
    lv_obj_t *settings_root;
    lv_obj_t *settings_carousel;
    lv_obj_t *settings_detail;
    lv_obj_t *settings_detail_header;
    lv_obj_t *settings_detail_title;
    lv_obj_t *settings_detail_edge_zone;
    lv_obj_t *settings_bms_popup;
    lv_obj_t *settings_bms_ble_status;
    lv_obj_t *settings_controller_tire_rollers[3];
    lv_obj_t *settings_controller_ratio_roller;
    lv_obj_t *settings_restore_popup;
    lv_obj_t *settings_system_value;
    lv_obj_t *settings_system_slider;
    lv_obj_t *settings_system_slider_fill;
    lv_obj_t *settings_system_slider_knob;
    lv_obj_t *settings_calibration_target;
    lv_obj_t *settings_calibration_status;
    char settings_bms_confirm_mac[18];
    char settings_bms_confirm_name[ESP_BMS_BMS_SCAN_NAME_LEN + 1U];
    lv_obj_t *settings_swipe_indicator;
    bool settings_bms_ble_popup_open;
    bool quick_connecting_toast_active;
    bool settings_nav_hidden;
    bool settings_nav_layout_updating;
    bool setup_ap_qr_ready;
    bool setup_ap_qr_encode_attempted;
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
    lv_obj_t *quick_toast_text;
    lv_obj_t *quick_toast_rotate_title;
    lv_obj_t *quick_toast_rotate_icon;
    lv_obj_t *quick_toast_rotate_countdown;
    lv_timer_t *quick_toast_timer;
    lv_timer_t *quick_level_save_timer;
    lv_obj_t *quick_edit_button;
    lv_obj_t *quick_edit_icon;
    lv_obj_t *quick_panel_item_icons[QUICK_PANEL_BUTTON_COUNT];
    lv_obj_t *screen_lock_guard;
    lv_obj_t *screen_unlock_card;
    lv_obj_t *screen_unlock_track;
    lv_obj_t *screen_unlock_fill;
    lv_obj_t *screen_unlock_knob;
    lv_obj_t *screen_unlock_hint;
    lv_timer_t *screen_unlock_timer;
    uint8_t quick_panel_item_active_flags;
    uint8_t quick_panel_item_local_active_flags;
    uint8_t quick_panel_item_local_override_flags;
    quick_panel_layout_t quick_layouts[QUICK_LAYOUT_COUNT];

    lv_obj_t *speed;
    lv_obj_t *gps_state;
    lv_obj_t *bms_state;
    lv_obj_t *ap_state;
    lv_obj_t *soc;
    lv_obj_t *soc_battery_level;
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
    lv_obj_t *gps_speed_unit;
    lv_obj_t *speed_art;
    lv_obj_t *speed_soc;
    lv_obj_t *speed_consumption;
    lv_obj_t *speed_controller_temp;
    lv_obj_t *speed_motor_temp;
    lv_obj_t *speed_gear;
    lv_obj_t *speed_scale_labels[SPEED_DASHBOARD_SCALE_LABEL_COUNT];
    lv_obj_t *controller_speed;
    lv_obj_t *controller_speed_unit;
    lv_obj_t *controller_gear;
    lv_obj_t *controller_power;
    lv_obj_t *controller_rpm;
    lv_obj_t *controller_temp;
    lv_obj_t *controller_motor_temp;
    char controller_speed_buf[12];
    char controller_speed_unit_buf[8];
    char controller_gear_buf[8];
    char controller_power_buf[16];
    char controller_rpm_buf[16];
    char controller_temp_buf[12];
    char controller_motor_temp_buf[12];
    char gps_speed_buf[12];
    char gps_speed_unit_buf[8];
    char gps_uptime_buf[24];
    char speed_soc_buf[8];
    char speed_consumption_buf[20];
    char speed_controller_temp_buf[16];
    char speed_motor_temp_buf[16];
    char speed_gear_buf[8];
    char speed_scale_buf[SPEED_DASHBOARD_SCALE_LABEL_COUNT][8];
    uint32_t speed_art_signature;
    bool speed_art_signature_valid;
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
    uint32_t speed_art_draw_count;
    uint32_t speed_art_draw_max_us;
    uint64_t speed_art_draw_elapsed_us;
    int64_t drag_diagnostic_start_us;
#endif
    lv_obj_t *setup_ap_control_row;
    lv_obj_t *setup_ap_info;
    lv_obj_t *setup_ap_qr_panel;
    lv_obj_t *setup_ap_qr;

    int32_t width;
    int32_t height;
    bool dragging;
    bool settling;
    bool controller_page_enabled;
    bool page_scroll_gesture_active;
    bool page_scroll_throw_frozen;
    int32_t drag_start_pages_x;
    int32_t drag_pages_dx;
    int32_t drag_release_pages_dx;
    uint32_t drag_last_sample_log_ms;
    lv_point_t quick_pull_start;
    lv_point_t return_swipe_start;
    lv_point_t settings_swipe_start;
    lv_point_t settings_calibration_observed;
    lv_point_t settings_calibration_expected;
    lv_point_t quick_drag_start;
    lv_point_t screen_lock_press_start;
    int32_t quick_pull_drag_dy;
    int32_t return_swipe_drag_dy;
    int32_t settings_swipe_drag_dx;
    int32_t settings_nav_drag_anchor_y;
    int32_t settings_nav_scroll_anchor_y;
    int32_t screen_lock_drag_dx;
    int32_t screen_lock_drag_dy;
    int32_t screen_unlock_knob_x;
    esp_bms_lvgl_page_t page;
    esp_bms_lvgl_page_t drag_start_page;
    esp_bms_lvgl_action_event_t pending_event;
    lv_obj_t *quick_drag_obj;
    int32_t quick_drag_obj_x;
    int32_t quick_drag_obj_y;
    quick_drag_target_kind_t quick_drag_target_kind;
    uint8_t quick_drag_target_index;
    uint8_t settings_detail_id;
    uint8_t settings_bms_view;
    uint8_t settings_ble_source;
    uint8_t settings_controller_view;
    uint8_t settings_system_view;
    uint8_t settings_system_slider_kind;
    uint8_t settings_calibration_target_index;
    uint8_t quick_level_overlay_kind;
    uint8_t quick_level_position;
    uint8_t quick_brightness_percent;
    uint8_t quick_volume_percent;
    uint8_t quick_rotate_toast_remaining_s;
    uint32_t flags;
    esp_bms_dashboard_snapshot_t last_snapshot;
    esp_bms_dashboard_snapshot_t deferred_snapshot;
} esp_bms_lvgl_ui_t;

static esp_bms_lvgl_ui_t s_ui;

static bool ui_state_flag_get(ui_state_flag_t flag)
{
    return (s_ui.flags & (uint32_t)flag) != 0U;
}

static void ui_state_flag_set(ui_state_flag_t flag, bool enabled)
{
    if (enabled) {
        s_ui.flags |= (uint32_t)flag;
    } else {
        s_ui.flags &= ~(uint32_t)flag;
    }
}

#define UI_FLAG(name) ui_state_flag_get(UI_STATE_FLAG_##name)
#define UI_SET_FLAG(name, enabled) ui_state_flag_set(UI_STATE_FLAG_##name, (enabled))
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
static uint8_t s_dashboard_cell_key_draw_buf_initialized_flags;

static void finish_page_scroll_state(bool flush_snapshot);
static void move_to_page(esp_bms_lvgl_page_t page, bool animated);
static void show_dashboard_view(void);
static void settings_bms_popup_close(void);
static void settings_swipe_indicator_hide(void);
static void settings_show_root(void);
static void set_quick_panel_open(bool open);
static void set_quick_edit_mode(bool edit_mode);
static void quick_edit_set_pressed(bool pressed);
static void quick_tile_set_scale(lv_obj_t *obj, int32_t scale);
static void refresh_quick_level_layouts(void);
static const char *quick_level_position_text(void);
static void quick_level_overlay_layout(void);
static void quick_level_overlay_hide(void);
static bool process_return_swipe_event(lv_event_code_t code, bool allow_start);
static void screen_lock_enter(void);
static void screen_lock_reapply(void);
static void screen_lock_create(lv_obj_t *screen);
static void screen_unlock_timer_cancel(void);

static const lv_color_t COLOR_BG = LV_COLOR_MAKE(0x08, 0x0a, 0x0e);
static const lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(0x16, 0x20, 0x29);
static const lv_color_t COLOR_SOC = LV_COLOR_MAKE(0x00, 0x56, 0xbe);
static const lv_color_t COLOR_WHITE = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xe8, 0xf1, 0xff);
static const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0xa9, 0xb4, 0xc8);
static const lv_color_t COLOR_ACCENT = LV_COLOR_MAKE(0x74, 0xd6, 0xb5);
static const lv_color_t COLOR_WARN = LV_COLOR_MAKE(0xff, 0xc8, 0x57);
static const lv_color_t COLOR_BAD = LV_COLOR_MAKE(0xff, 0x6b, 0x6b);
static const lv_color_t COLOR_DASHBOARD_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t COLOR_DASHBOARD_PANEL = LV_COLOR_MAKE(0x09, 0x0c, 0x10);
static const lv_color_t COLOR_DASHBOARD_SOC_PANEL = LV_COLOR_MAKE(0x06, 0x32, 0x70);
static const lv_color_t COLOR_DASHBOARD_BORDER = LV_COLOR_MAKE(0x3e, 0x42, 0x47);
static const lv_color_t COLOR_DASHBOARD_SOC_BORDER = LV_COLOR_MAKE(0x4a, 0x9b, 0xf5);
static const lv_color_t COLOR_DASHBOARD_VALUE = LV_COLOR_MAKE(0x2d, 0x8a, 0x66);
static const lv_color_t COLOR_CONTROLLER_VALUE = LV_COLOR_MAKE(0x20, 0xd7, 0x83);
static const lv_color_t COLOR_SPEED_BAND_DARK = LV_COLOR_MAKE(0x00, 0x55, 0x94);
static const lv_color_t COLOR_SPEED_BAND_BLUE = LV_COLOR_MAKE(0x00, 0xb8, 0xf0);
static const lv_color_t COLOR_SPEED_BAND_IDLE = LV_COLOR_MAKE(0x27, 0x29, 0x2d);
static const lv_color_t COLOR_SPEED_BAND_DANGER = LV_COLOR_MAKE(0xc8, 0x24, 0x2f);
static const lv_color_t COLOR_SPEED_DIVIDER = LV_COLOR_MAKE(0x00, 0xc8, 0xf2);
static const lv_color_t COLOR_SPEED_GPS_OK = LV_COLOR_MAKE(0x43, 0xe3, 0x36);
static const lv_color_t COLOR_DASHBOARD_BATTERY_LEVEL = LV_COLOR_MAKE(0x66, 0xbf, 0xf2);
static const lv_color_t COLOR_SETTINGS_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t COLOR_SETTINGS_CARD = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t COLOR_SETTINGS_LIST = LV_COLOR_MAKE(0x24, 0x24, 0x24);
static const lv_color_t COLOR_SETTINGS_BORDER = LV_COLOR_MAKE(0x32, 0x32, 0x32);
static const lv_color_t COLOR_SETTINGS_TEXT = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_SETTINGS_MUTED = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_SETTINGS_ACCENT = LV_COLOR_MAKE(0xff, 0xff, 0xff);
static const lv_color_t COLOR_SWITCH_ACTIVE = LV_COLOR_MAKE(0x34, 0xc7, 0x59);
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

static lv_obj_t *dashboard_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t w,
                                 int32_t h,
                                 lv_color_t color,
                                 lv_color_t border_color)
{
    lv_obj_t *obj = panel(parent, x, y, w, h, color);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_post(obj, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *dashboard_separator(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *line = lv_obj_create(parent);
    clear_style(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, 1);
    lv_obj_set_style_bg_color(line, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    return line;
}

static void dashboard_battery_icon(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h)
{
    lv_obj_t *body = lv_obj_create(parent);
    clear_style(body);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, w, h);
    lv_obj_set_style_radius(body, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(body, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_post(body, true, LV_PART_MAIN);

    s_ui.soc_battery_level = lv_obj_create(body);
    clear_style(s_ui.soc_battery_level);
    lv_obj_set_pos(s_ui.soc_battery_level, 3, 3);
    lv_obj_set_size(s_ui.soc_battery_level, 0, h - 6);
    lv_obj_set_style_radius(s_ui.soc_battery_level, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.soc_battery_level, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.soc_battery_level, LV_OPA_COVER, LV_PART_MAIN);

    for (int32_t index = 1; index < 5; ++index) {
        lv_obj_set_height(dashboard_separator(body, (w * index) / 5, 3, 1), h - 6);
    }

    lv_obj_t *terminal = lv_obj_create(parent);
    clear_style(terminal);
    lv_obj_set_pos(terminal, x + w, y + ((h - 8) / 2));
    lv_obj_set_size(terminal, 4, 8);
    lv_obj_set_style_radius(terminal, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(terminal, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, LV_PART_MAIN);
}

static void update_dashboard_battery_icon(uint8_t soc_percent, bool valid, bool charging)
{
    (void)charging;
    if (!s_ui.soc_battery_level) {
        return;
    }

    lv_obj_t *body = lv_obj_get_parent(s_ui.soc_battery_level);
    const int32_t inner_w = lv_obj_get_width(body) - 6;
    const uint8_t soc = valid ? (soc_percent > 100U ? 100U : soc_percent) : 0U;
    lv_obj_set_width(s_ui.soc_battery_level, (inner_w * (int32_t)soc) / 100);
    lv_obj_set_style_bg_color(s_ui.soc_battery_level,
                              valid ? COLOR_DASHBOARD_BATTERY_LEVEL : COLOR_MUTED,
                              LV_PART_MAIN);
}

static void dashboard_thermometer_icon(lv_obj_t *parent, int32_t center_x, int32_t y)
{
    lv_obj_t *stem = lv_obj_create(parent);
    clear_style(stem);
    lv_obj_set_pos(stem, center_x - 1, y);
    lv_obj_set_size(stem, 3, 10);
    lv_obj_set_style_radius(stem, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(stem, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *bulb = lv_obj_create(parent);
    clear_style(bulb);
    lv_obj_set_pos(bulb, center_x - 3, y + 7);
    lv_obj_set_size(bulb, 7, 7);
    lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bulb, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, LV_PART_MAIN);
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

static lv_draw_buf_t *dashboard_cell_key_draw_buf(uint8_t index)
{
    switch (index) {
    case 0:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 0U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_0_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 0U, true);
        }
        return &s_dashboard_cell_key_0_draw_buf;
    case 1:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 1U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_1_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 1U, true);
        }
        return &s_dashboard_cell_key_1_draw_buf;
    case 2:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 2U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_2_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 2U, true);
        }
        return &s_dashboard_cell_key_2_draw_buf;
    case 3:
    default:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 3U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_3_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 3U, true);
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
    layout->valid = true;
    layout->tools_vertical = tools_vertical;

    const int32_t gap = 8;
    const int32_t quick_pad = 16;
    const int32_t grid_cols = tools_vertical ? 2 : QUICK_PANEL_GRID_COLS;
    const int32_t grid_rows = QUICK_PANEL_GRID_SLOT_COUNT / grid_cols;
    const int32_t tile_w = (width - (quick_pad * 2) -
                            ((grid_cols - 1) * gap)) / grid_cols;
    const int32_t tile_h = (height - (quick_pad * 2) -
                            ((grid_rows - 1) * gap)) / grid_rows;
    int32_t quick_tile = tile_w < tile_h ? tile_w : tile_h;
    if (quick_tile < 1) {
        quick_tile = 1;
    }
    const int32_t quick_grid_w = (grid_cols * quick_tile) +
                                 ((grid_cols - 1) * gap);
    const int32_t quick_grid_h = (grid_rows * quick_tile) +
                                 ((grid_rows - 1) * gap);
    const int32_t quick_left = (width - quick_grid_w) / 2;
    const int32_t quick_top = (height - quick_grid_h) / 2;

    quick_tile_rect_t slots[QUICK_PANEL_GRID_SLOT_COUNT] = { 0 };
    for (uint32_t slot = 0; slot < QUICK_PANEL_GRID_SLOT_COUNT; ++slot) {
        const int32_t col = (int32_t)(slot % grid_cols);
        const int32_t row = (int32_t)(slot / grid_cols);
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

static void quick_tile_x_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void quick_tile_y_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void quick_obj_stop_reorder_anim(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_anim_delete(obj, quick_tile_x_anim_cb);
    lv_anim_delete(obj, quick_tile_y_anim_cb);
}

static void quick_obj_apply_rect(lv_obj_t *obj, const quick_tile_rect_t *rect)
{
    if (!obj || !rect) {
        return;
    }
    quick_obj_stop_reorder_anim(obj);
    lv_obj_set_pos(obj, rect->x, rect->y);
    lv_obj_set_size(obj, rect->w, rect->h);
}

static void quick_obj_animate_to_rect(lv_obj_t *obj, const quick_tile_rect_t *rect)
{
    if (!obj || !rect) {
        return;
    }

    const int32_t current_x = lv_obj_get_x(obj);
    const int32_t current_y = lv_obj_get_y(obj);
    quick_obj_stop_reorder_anim(obj);
    lv_obj_set_size(obj, rect->w, rect->h);

    if (current_x != rect->x) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_values(&anim, current_x, rect->x);
        lv_anim_set_duration(&anim, QUICK_TILE_REORDER_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, quick_tile_x_anim_cb);
        lv_anim_start(&anim);
    }
    if (current_y != rect->y) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_values(&anim, current_y, rect->y);
        lv_anim_set_duration(&anim, QUICK_TILE_REORDER_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, quick_tile_y_anim_cb);
        lv_anim_start(&anim);
    }
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

static lv_obj_t *quick_layout_obj_for_target(quick_drag_target_kind_t target_kind,
                                             uint8_t target_index)
{
    if (target_kind == QUICK_DRAG_TARGET_BRIGHTNESS) {
        return s_ui.quick_brightness_tile;
    }
    if (target_kind == QUICK_DRAG_TARGET_VOLUME) {
        return s_ui.quick_volume_tile;
    }
    if (target_kind == QUICK_DRAG_TARGET_ITEM && target_index < QUICK_PANEL_BUTTON_COUNT) {
        return s_ui.quick_panel_items[target_index];
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

static bool quick_layout_update_drag_sort(void)
{
    if (!s_ui.quick_drag_obj || !UI_FLAG(QUICK_DRAG_MOVED)) {
        return false;
    }

    quick_panel_layout_t *layout = quick_layout_ensure_current();
    quick_tile_rect_t *source =
        quick_layout_rect_for_target(layout, s_ui.quick_drag_target_kind, s_ui.quick_drag_target_index);
    if (!source) {
        return false;
    }

    const int32_t center_x = lv_obj_get_x(s_ui.quick_drag_obj) + (lv_obj_get_width(s_ui.quick_drag_obj) / 2);
    const int32_t center_y = lv_obj_get_y(s_ui.quick_drag_obj) + (lv_obj_get_height(s_ui.quick_drag_obj) / 2);
    quick_drag_target_kind_t target_kind = QUICK_DRAG_TARGET_NONE;
    uint8_t target_index = 0;
    quick_layout_find_drop_target(layout, center_x, center_y, &target_kind, &target_index);

    if (target_kind == s_ui.quick_drag_target_kind &&
        target_index == s_ui.quick_drag_target_index) {
        return false;
    }

    quick_tile_rect_t *target = quick_layout_rect_for_target(layout, target_kind, target_index);
    lv_obj_t *target_obj = quick_layout_obj_for_target(target_kind, target_index);
    if (!target || !target_obj) {
        return false;
    }

    const quick_tile_rect_t moved_rect = *source;
    *source = *target;
    *target = moved_rect;
    quick_obj_animate_to_rect(target_obj, target);
    return true;
}

static void quick_layout_commit_drag_sort(void)
{
    if (!s_ui.quick_drag_obj) {
        quick_layout_apply_current();
        return;
    }

    (void)quick_layout_update_drag_sort();
    quick_panel_layout_t *layout = quick_layout_ensure_current();
    quick_tile_rect_t *target =
        quick_layout_rect_for_target(layout, s_ui.quick_drag_target_kind, s_ui.quick_drag_target_index);
    if (target) {
        quick_obj_animate_to_rect(s_ui.quick_drag_obj, target);
    } else {
        quick_layout_apply_current();
    }
    refresh_quick_level_layouts();
}

static lv_color_t dashboard_soc_fill_color(uint8_t soc_percent, bool valid, bool charging)
{
    (void)charging;
    if (!valid) {
        return COLOR_PANEL_ALT;
    }
    if (soc_percent >= 80U) {
        return COLOR_ACCENT;
    }
    if (soc_percent <= 20U) {
        return COLOR_BAD;
    }
    return COLOR_SOC;
}

static void update_dashboard_soc_fill(uint8_t soc_percent, bool valid, bool charging)
{
    if (!s_ui.soc) {
        return;
    }

    const uint8_t soc = valid ? (soc_percent > 100U ? 100U : soc_percent) : 0U;
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_ui.soc),
                              dashboard_soc_fill_color(soc, valid, charging),
                              LV_PART_MAIN);
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

static void show_dashboard_view(void)
{
    settings_bms_popup_close();
    if (s_ui.settings_swipe_drag_dx == 0) {
        settings_swipe_indicator_hide();
    }
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
    UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, false);
    settings_show_root();
    lv_obj_move_foreground(s_ui.settings_page);
}

static void queue_action_with_commit(esp_bms_lvgl_action_t action, bool committed)
{
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
        s_ui.pending_event.action = action;
        ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, committed);
    }
}

static void queue_action(esp_bms_lvgl_action_t action)
{
    queue_action_with_commit(action, true);
}

static void queue_bms_bind_action(const char *mac)
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

static void queue_controller_bind_action(const char *mac)
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

static void queue_touch_calibration_sample(uint8_t target_index,
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

typedef enum {
    QUICK_LEVEL_POSITION_MIDDLE = 0,
    QUICK_LEVEL_POSITION_END = 1,
    QUICK_LEVEL_POSITION_START = 2,
    QUICK_LEVEL_POSITION_COUNT = 3,
} quick_level_position_t;

static const char *quick_level_icon(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_EYE_OPEN;
}

static uint8_t quick_level_current_value(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_percent : s_ui.quick_brightness_percent;
}

static quick_level_position_t quick_level_position(void)
{
    return s_ui.quick_level_position < QUICK_LEVEL_POSITION_COUNT ?
               (quick_level_position_t)s_ui.quick_level_position :
               QUICK_LEVEL_POSITION_MIDDLE;
}

static const char *quick_level_position_text(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    switch (quick_level_position()) {
    case QUICK_LEVEL_POSITION_START:
        return portrait ? "左边" : "上面";
    case QUICK_LEVEL_POSITION_END:
        return portrait ? "右边" : "下面";
    case QUICK_LEVEL_POSITION_MIDDLE:
    default:
        return "中间";
    }
}

static void quick_level_queue_value(quick_level_kind_t kind, uint8_t value, bool committed)
{
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    s_ui.pending_event.action = kind == QUICK_LEVEL_VOLUME ? ESP_BMS_LVGL_ACTION_SET_VOLUME :
                                                             ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, committed);
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, BRIGHTNESS_PERCENT_VALID, kind == QUICK_LEVEL_BRIGHTNESS);
    s_ui.pending_event.brightness_percent = kind == QUICK_LEVEL_BRIGHTNESS ? value : 0;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, VOLUME_PERCENT_VALID, kind == QUICK_LEVEL_VOLUME);
    s_ui.pending_event.volume_percent = kind == QUICK_LEVEL_VOLUME ? value : 0;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, VOLUME_FEEDBACK_VALID, kind == QUICK_LEVEL_VOLUME);
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
    if (!UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE) ||
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
    if (UI_FLAG(QUICK_LEVEL_OVERLAY_HORIZONTAL)) {
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
    UI_SET_FLAG(QUICK_PANEL_SETTLING, false);
}

static void quick_panel_settle_anim_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    set_quick_panel_open(UI_FLAG(QUICK_PANEL_ANIMATION_TARGET_OPEN));
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
    UI_SET_FLAG(QUICK_PANEL_OPEN, open);
    UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, false);
    UI_SET_FLAG(QUICK_PANEL_SETTLING, true);
    UI_SET_FLAG(QUICK_PANEL_ANIMATION_TARGET_OPEN, open);
    s_ui.quick_pull_drag_dy = 0;
    s_ui.return_swipe_drag_dy = 0;
    UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
    UI_SET_FLAG(RETURN_SWIPE_CANCELLED, false);
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

static void quick_connecting_spinner_anim_cb(void *obj, int32_t angle)
{
    lv_obj_set_style_transform_rotation((lv_obj_t *)obj, angle, LV_PART_MAIN);
}

static void quick_toast_stop_connecting(void)
{
    if (!s_ui.quick_connecting_toast_active) {
        return;
    }
    if (s_ui.quick_toast_rotate_icon) {
        lv_anim_delete(s_ui.quick_toast_rotate_icon, quick_connecting_spinner_anim_cb);
        lv_obj_set_style_transform_rotation(s_ui.quick_toast_rotate_icon, 0, LV_PART_MAIN);
    }
    s_ui.quick_connecting_toast_active = false;
}

static void quick_toast_cancel(void)
{
    quick_toast_stop_connecting();
    if (s_ui.quick_toast_timer) {
        lv_timer_delete(s_ui.quick_toast_timer);
        s_ui.quick_toast_timer = NULL;
    }
}

static void quick_toast_apply_normal_style(void)
{
    if (!s_ui.quick_toast || !s_ui.quick_toast_text) {
        return;
    }

    const int32_t toast_w = s_ui.width < 150 ? s_ui.width - 24 : 126;
    const int32_t toast_h = 28;
    lv_obj_set_pos(s_ui.quick_toast, (s_ui.width - toast_w) / 2, s_ui.height - toast_h - 18);
    lv_obj_set_size(s_ui.quick_toast, toast_w, toast_h);
    lv_obj_set_style_radius(s_ui.quick_toast, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_toast, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.quick_toast, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.quick_toast, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_pos(s_ui.quick_toast_text, 4, (toast_h - (int32_t)settings_zh_16.line_height) / 2);
    lv_obj_set_size(s_ui.quick_toast_text, toast_w - 8, settings_zh_16.line_height);
    lv_obj_set_style_text_color(s_ui.quick_toast_text, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_text, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    set_obj_hidden(s_ui.quick_toast_text, false);
    set_obj_hidden(s_ui.quick_toast_rotate_title, true);
    set_obj_hidden(s_ui.quick_toast_rotate_icon, true);
    set_obj_hidden(s_ui.quick_toast_rotate_countdown, true);
}

static void quick_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    set_obj_hidden(s_ui.quick_toast, true);
    UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, false);
    s_ui.quick_toast_timer = NULL;
}

static void quick_rotate_toast_set_countdown(void)
{
    if (!s_ui.quick_toast_rotate_countdown) {
        return;
    }
    label_set_text_fmt_if_changed(s_ui.quick_toast_rotate_countdown,
                                  "%us",
                                  (unsigned int)s_ui.quick_rotate_toast_remaining_s);
}

static void quick_rotate_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui.quick_rotate_toast_remaining_s > 1U) {
        --s_ui.quick_rotate_toast_remaining_s;
        quick_rotate_toast_set_countdown();
        return;
    }

    s_ui.quick_rotate_toast_remaining_s = 0;
    set_obj_hidden(s_ui.quick_toast, true);
    UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, false);
    s_ui.quick_toast_timer = NULL;
}

static void quick_toast_show_text(const char *text)
{
    if (!s_ui.quick_toast || !s_ui.quick_toast_text) {
        return;
    }

    UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, false);
    quick_toast_apply_normal_style();
    label_set_text_if_changed(s_ui.quick_toast_text, text ? text : "");
    set_obj_hidden(s_ui.quick_toast, false);
    lv_obj_move_foreground(s_ui.quick_toast);

    quick_toast_cancel();
    s_ui.quick_toast_timer = lv_timer_create(quick_toast_timer_cb, QUICK_TOAST_MS, NULL);
    if (s_ui.quick_toast_timer) {
        lv_timer_set_repeat_count(s_ui.quick_toast_timer, 1);
    }
}

static void quick_toast_show_connecting(void)
{
    if (!s_ui.quick_toast || !s_ui.quick_toast_text || !s_ui.quick_toast_rotate_icon) {
        return;
    }

    quick_toast_cancel();
    UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, false);

    const int32_t toast_w = s_ui.width < 150 ? s_ui.width - 24 : 126;
    const int32_t toast_h = 36;
    lv_obj_set_pos(s_ui.quick_toast, (s_ui.width - toast_w) / 2, s_ui.height - toast_h - 18);
    lv_obj_set_size(s_ui.quick_toast, toast_w, toast_h);
    lv_obj_set_style_radius(s_ui.quick_toast, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_toast, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.quick_toast, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.quick_toast, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_pos(s_ui.quick_toast_rotate_icon, 8, 6);
    lv_obj_set_size(s_ui.quick_toast_rotate_icon, 24, 24);
    lv_obj_set_style_transform_pivot_x(s_ui.quick_toast_rotate_icon, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_ui.quick_toast_rotate_icon, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.quick_toast_rotate_icon, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_rotate_icon, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_rotate_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    label_set_text_if_changed(s_ui.quick_toast_rotate_icon, LV_SYMBOL_LOOP);

    lv_obj_set_pos(s_ui.quick_toast_text, 34, (toast_h - (int32_t)settings_zh_16.line_height) / 2);
    lv_obj_set_size(s_ui.quick_toast_text, toast_w - 40, settings_zh_16.line_height);
    lv_obj_set_style_text_color(s_ui.quick_toast_text, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_text, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    label_set_text_if_changed(s_ui.quick_toast_text, "连接...");

    set_obj_hidden(s_ui.quick_toast_rotate_title, true);
    set_obj_hidden(s_ui.quick_toast_rotate_countdown, true);
    set_obj_hidden(s_ui.quick_toast_rotate_icon, false);
    set_obj_hidden(s_ui.quick_toast_text, false);
    set_obj_hidden(s_ui.quick_toast, false);
    lv_obj_move_foreground(s_ui.quick_toast);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.quick_toast_rotate_icon);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_duration(&anim, 850);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_exec_cb(&anim, quick_connecting_spinner_anim_cb);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    s_ui.quick_connecting_toast_active = true;
    lv_anim_start(&anim);
}

static void quick_rotate_toast_show(void)
{
    if (!s_ui.quick_toast ||
        !s_ui.quick_toast_rotate_title ||
        !s_ui.quick_toast_rotate_icon ||
        !s_ui.quick_toast_rotate_countdown) {
        return;
    }

    const int32_t shortest = s_ui.width < s_ui.height ? s_ui.width : s_ui.height;
    int32_t toast_size = clamp_i32(shortest / 3, 72, 92);
    if (toast_size > shortest - 24) {
        toast_size = shortest - 24;
    }
    if (toast_size < 60) {
        toast_size = 60;
    }

    UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, true);
    lv_obj_set_pos(s_ui.quick_toast, (s_ui.width - toast_size) / 2, (s_ui.height - toast_size) / 2);
    lv_obj_set_size(s_ui.quick_toast, toast_size, toast_size);
    lv_obj_set_style_radius(s_ui.quick_toast, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_toast, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_toast, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.quick_toast, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_toast, LV_OBJ_FLAG_CLICKABLE);

    const int32_t title_h = settings_zh_13.line_height;
    const int32_t icon_h = lv_font_montserrat_24.line_height;
    const int32_t countdown_h = lv_font_montserrat_14.line_height;
    const int32_t gap = 2;
    const int32_t content_h = title_h + icon_h + countdown_h + (gap * 2);
    int32_t y = (toast_size - content_h) / 2;
    if (y < 4) {
        y = 4;
    }

    lv_obj_set_pos(s_ui.quick_toast_rotate_title, 4, y);
    lv_obj_set_size(s_ui.quick_toast_rotate_title, toast_size - 8, title_h);
    lv_obj_set_style_text_color(s_ui.quick_toast_rotate_title, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_rotate_title, &settings_zh_13, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_rotate_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    label_set_text_if_changed(s_ui.quick_toast_rotate_title, QUICK_ROTATE_TOAST_TITLE);

    y += title_h + gap;
    lv_obj_set_pos(s_ui.quick_toast_rotate_icon, 0, y);
    lv_obj_set_size(s_ui.quick_toast_rotate_icon, toast_size, icon_h);
    lv_obj_set_style_text_color(s_ui.quick_toast_rotate_icon, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_rotate_icon, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_rotate_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    label_set_text_if_changed(s_ui.quick_toast_rotate_icon, LV_SYMBOL_LOOP);

    y += icon_h + gap;
    lv_obj_set_pos(s_ui.quick_toast_rotate_countdown, 0, y);
    lv_obj_set_size(s_ui.quick_toast_rotate_countdown, toast_size, countdown_h);
    lv_obj_set_style_text_color(s_ui.quick_toast_rotate_countdown, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.quick_toast_rotate_countdown, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.quick_toast_rotate_countdown, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_ui.quick_rotate_toast_remaining_s =
        (uint8_t)(ESP_BMS_LVGL_ROTATE_SAVE_DELAY_MS / QUICK_ROTATE_TOAST_TICK_MS);
    quick_rotate_toast_set_countdown();

    set_obj_hidden(s_ui.quick_toast_text, true);
    set_obj_hidden(s_ui.quick_toast_rotate_title, false);
    set_obj_hidden(s_ui.quick_toast_rotate_icon, false);
    set_obj_hidden(s_ui.quick_toast_rotate_countdown, false);
    set_obj_hidden(s_ui.quick_toast, false);
    lv_obj_move_foreground(s_ui.quick_toast);

    quick_toast_cancel();
    s_ui.quick_toast_timer = lv_timer_create(quick_rotate_toast_timer_cb,
                                             QUICK_ROTATE_TOAST_TICK_MS,
                                             NULL);
    if (s_ui.quick_toast_timer) {
        lv_timer_set_repeat_count(s_ui.quick_toast_timer,
                                  s_ui.quick_rotate_toast_remaining_s);
    }
}

static void set_quick_panel_open(bool open)
{
    quick_panel_stop_settle_anim();
    UI_SET_FLAG(QUICK_PANEL_OPEN, open);
    UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, open);
    UI_SET_FLAG(QUICK_PANEL_ANIMATION_TARGET_OPEN, open);
    s_ui.quick_pull_drag_dy = 0;
    s_ui.return_swipe_drag_dy = 0;
    if (!open) {
        s_ui.quick_drag_obj = NULL;
        UI_SET_FLAG(QUICK_DRAG_MOVED, false);
        UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
        UI_SET_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE, false);
        UI_SET_FLAG(QUICK_LEVEL_OVERLAY_DRAGGED, false);
        UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, false);
        quick_toast_cancel();
        UI_SET_FLAG(QUICK_ROTATE_TOAST_ACTIVE, false);
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
    const bool changed = UI_FLAG(QUICK_EDIT_MODE) != edit_mode;
    UI_SET_FLAG(QUICK_EDIT_MODE, edit_mode);
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
        UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
        quick_edit_set_pressed(true);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        quick_toast_show_text(QUICK_TOAST_SORT_HINT);
        UI_SET_FLAG(QUICK_LONG_TRIGGERED, true);
        lv_indev_wait_release(lv_indev_active());
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        quick_edit_set_pressed(false);
        if (code == LV_EVENT_PRESS_LOST) {
            UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (UI_FLAG(QUICK_LONG_TRIGGERED)) {
            UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
            return;
        }
        set_quick_edit_mode(!UI_FLAG(QUICK_EDIT_MODE));
    }
}

static void perform_ui_action(esp_bms_lvgl_action_t action, bool close_quick_panel)
{
    if (close_quick_panel) {
        set_quick_panel_open(false);
    }

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

static void return_swipe_event_cb(lv_event_t *event)
{
    (void)process_return_swipe_event(lv_event_get_code(event), true);
}

static void quick_pull_event_cb(lv_event_t *event)
{
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

typedef enum {
    QUICK_ITEM_BLUETOOTH = 0,
    QUICK_ITEM_HOTSPOT,
    QUICK_ITEM_ROTATE,
    QUICK_ITEM_SPEED,
    QUICK_ITEM_SETTINGS,
    QUICK_ITEM_LOCK,
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
    SETTINGS_DETAIL_HOTSPOT,
    SETTINGS_DETAIL_BLUETOOTH,
    SETTINGS_DETAIL_BMS,
    SETTINGS_DETAIL_CONTROLLER,
    SETTINGS_DETAIL_SYSTEM,
    SETTINGS_DETAIL_ABOUT,
} settings_detail_id_t;

typedef enum {
    SETTINGS_BMS_VIEW_ROOT = 0,
    SETTINGS_BMS_VIEW_BLE_LIST,
    SETTINGS_BMS_VIEW_TYPE_LIST,
} settings_bms_view_t;

typedef enum {
    SETTINGS_SYSTEM_VIEW_ROOT = 0,
    SETTINGS_SYSTEM_VIEW_BRIGHTNESS,
    SETTINGS_SYSTEM_VIEW_VOLUME,
    SETTINGS_SYSTEM_VIEW_LEVEL_POSITION,
    SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION,
} settings_system_view_t;

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
    settings_system_view_t system_view;
} settings_detail_row_t;

static void settings_show_detail(settings_detail_id_t detail_id);
static void settings_detail_chrome_show(settings_detail_id_t detail_id);
static void settings_detail_action_event_cb(lv_event_t *event);
static void settings_bms_type_button_event_cb(lv_event_t *event);
static void settings_bms_type_option_event_cb(lv_event_t *event);
static void settings_bms_ble_candidate_event_cb(lv_event_t *event);
static void settings_bms_ble_refresh_event_cb(lv_event_t *event);
static void settings_restore_confirm_show(void);
static lv_obj_t *settings_detail_row(lv_obj_t *parent,
                                     int32_t x,
                                     int32_t y,
                                     int32_t w,
                                     int32_t h,
                                     const settings_detail_row_t *row);
static void settings_show_bluetooth_detail(void);
static void settings_show_bms_detail(void);
static void settings_show_controller_detail(void);
static void settings_show_system_view(settings_system_view_t view);
static void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot);

static const quick_panel_item_t QUICK_PANEL_ITEMS[QUICK_PANEL_BUTTON_COUNT] = {
    { QUICK_ITEM_BLUETOOTH, QUICK_BLUETOOTH_SYMBOL, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "蓝牙设置", false },
    { QUICK_ITEM_HOTSPOT, NULL, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "热点设置", true },
    { QUICK_ITEM_ROTATE, LV_SYMBOL_LOOP, ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY,
      "旋转屏幕", false },
    { QUICK_ITEM_SPEED, LV_SYMBOL_GPS, ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT,
      "点击切换", false },
    { QUICK_ITEM_SETTINGS, LV_SYMBOL_SETTINGS, ESP_BMS_LVGL_ACTION_SHOW_SETTINGS,
      "设备设置", false },
    { QUICK_ITEM_LOCK, NULL, ESP_BMS_LVGL_ACTION_NONE,
      "LOCK", false },
};

static const settings_option_t SETTINGS_OPTIONS[SETTINGS_OPTION_COUNT] = {
    { SETTINGS_DETAIL_HOTSPOT, "热点共享", "Setup AP", QUICK_HOTSPOT_SYMBOL, &wlanJZ },
    { SETTINGS_DETAIL_BLUETOOTH, "蓝牙", "附近可见", QUICK_BLUETOOTH_SYMBOL, &bluetoothon },
    { SETTINGS_DETAIL_BMS, "保护板设置", "扫描绑定", LV_SYMBOL_CHARGE, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_CONTROLLER, "速度仪表", "GPS / FarDriver", "C", &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_SYSTEM, "系统", "显示与控制", LV_SYMBOL_SETTINGS, &lv_font_montserrat_24 },
    { SETTINGS_DETAIL_ABOUT, "关于本机", "设备信息", "i", &lv_font_montserrat_24 },
};

static const settings_detail_row_t SETTINGS_HOTSPOT_ROWS[] = {
    { "状态", "热点已打开", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "名称", "fuckingBms_xxxxxx", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "密码", "8 DIGITS", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "手机页面", "192.168.4.1 网页配置", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "配置入口", "开启配网入口", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING, SETTINGS_SYSTEM_VIEW_ROOT },
    { "二维码", "网页查看", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING, SETTINGS_SYSTEM_VIEW_ROOT },
};

static const settings_detail_row_t SETTINGS_BLUETOOTH_ROWS[] = {
    { "状态", "未连接", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "名称", "ESP32 BMS GPS", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "可被发现", "附近可见", ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING, SETTINGS_SYSTEM_VIEW_ROOT },
};

static const settings_detail_row_t SETTINGS_BMS_ROWS[] = {
    { "蓝牙连接", "扫描绑定", ESP_BMS_LVGL_ACTION_START_BMS_BIND, SETTINGS_SYSTEM_VIEW_ROOT },
};

static const char *const SETTINGS_BMS_TYPE_LABELS[] = {
    "蚂蚁 ANT",
    "极空 JK",
    "嘉佰达 JBD",
    "达锂 Daly",
};

static const esp_bms_lvgl_action_t SETTINGS_BMS_TYPE_ACTIONS[] = {
    ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JK,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD,
    ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY,
};

_Static_assert(ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS) == ARRAY_SIZE(SETTINGS_BMS_TYPE_ACTIONS),
               "BMS type labels must match runtime BMS type count");

static const settings_detail_row_t SETTINGS_SYSTEM_ROWS[] = {
    { "亮度", "调节屏幕亮度", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_BRIGHTNESS },
    { "音量", "调节提示音量", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_VOLUME },
    { "调节条位置", "中间", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_LEVEL_POSITION },
    { "屏幕校准", "校准触摸位置", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION },
    { "旋转屏幕", "点击操作", ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY, SETTINGS_SYSTEM_VIEW_ROOT },
    { "语言切换", "点击操作", ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "恢复默认", "清除设置与校准", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS, SETTINGS_SYSTEM_VIEW_ROOT },
};

static const settings_detail_row_t SETTINGS_ABOUT_ROWS[] = {
    { "设备", "ESP32 BMS GPS", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "固件版本", "本地构建", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    { "屏幕", "ST7789", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
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
                    item->kind == QUICK_ITEM_HOTSPOT);
}

static settings_detail_id_t quick_panel_item_detail_id(const quick_panel_item_t *item)
{
    if (!item) {
        return SETTINGS_DETAIL_NONE;
    }
    switch (item->kind) {
    case QUICK_ITEM_BLUETOOTH:
        return SETTINGS_DETAIL_BLUETOOTH;
    case QUICK_ITEM_HOTSPOT:
        return SETTINGS_DETAIL_HOTSPOT;
    case QUICK_ITEM_ROTATE:
    case QUICK_ITEM_SPEED:
    case QUICK_ITEM_SETTINGS:
    case QUICK_ITEM_LOCK:
    default:
        return SETTINGS_DETAIL_NONE;
    }
}

static void quick_panel_item_apply_active(uint32_t index, bool active)
{
    if (index >= QUICK_PANEL_BUTTON_COUNT) {
        return;
    }

    const quick_panel_item_t *item = &QUICK_PANEL_ITEMS[index];
    ui_flag_set(&s_ui.quick_panel_item_active_flags, index, active);
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

static void quick_lock_icon_recenter(lv_obj_t *icon, int32_t content_w, int32_t content_h)
{
    if (!icon) {
        return;
    }
    lv_obj_set_pos(icon,
                   (content_w - QUICK_LOCK_ICON_W) / 2,
                   (content_h - QUICK_LOCK_ICON_H) / 2);
    lv_obj_set_size(icon, QUICK_LOCK_ICON_W, QUICK_LOCK_ICON_H);
}

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

static void quick_edit_set_pressed(bool pressed)
{
    const quick_tile_rect_t rect = {
        .x = s_ui.width - QUICK_EDIT_BUTTON_SIZE - 8,
        .y = 8,
        .w = QUICK_EDIT_BUTTON_SIZE,
        .h = QUICK_EDIT_BUTTON_SIZE,
    };
    quick_tile_apply_press_inset(s_ui.quick_edit_button, &rect, pressed);
    const int32_t extent = quick_tile_pressed_extent(QUICK_EDIT_BUTTON_SIZE, pressed);
    quick_symbol_icon_recenter(s_ui.quick_edit_icon,
                               extent - 8,
                               extent - 8,
                               LV_SYMBOL_EDIT,
                               &lv_font_montserrat_14);
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
    if (item->kind == QUICK_ITEM_LOCK) {
        quick_lock_icon_recenter(s_ui.quick_panel_item_icons[index], w - 8, h - 8);
        return;
    }
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
    UI_SET_FLAG(QUICK_DRAG_MOVED, false);
    quick_obj_stop_reorder_anim(obj);
    lv_obj_move_foreground(obj);
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
        UI_SET_FLAG(QUICK_DRAG_MOVED, true);
    }
    const int32_t max_x = s_ui.width - lv_obj_get_width(s_ui.quick_drag_obj);
    const int32_t max_y = s_ui.height - lv_obj_get_height(s_ui.quick_drag_obj);
    lv_obj_set_pos(s_ui.quick_drag_obj,
                   clamp_i32(s_ui.quick_drag_obj_x + dx, 0, max_x),
                   clamp_i32(s_ui.quick_drag_obj_y + dy, 0, max_y));
    (void)quick_layout_update_drag_sort();
}

static bool quick_drag_end(void)
{
    const bool moved = UI_FLAG(QUICK_DRAG_MOVED);
    quick_layout_commit_drag_sort();
    s_ui.quick_drag_obj = NULL;
    s_ui.quick_drag_target_kind = QUICK_DRAG_TARGET_NONE;
    s_ui.quick_drag_target_index = 0;
    UI_SET_FLAG(QUICK_DRAG_MOVED, false);
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
    if (!UI_FLAG(QUICK_PANEL_INTERACTIVE)) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_panel_item_set_pressed(index, false);
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
        if (UI_FLAG(QUICK_EDIT_MODE)) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
        } else {
            quick_panel_item_set_pressed(index, true);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING && UI_FLAG(QUICK_EDIT_MODE)) {
        quick_drag_update();
        return;
    }

    if (code == LV_EVENT_LONG_PRESSED) {
        if (UI_FLAG(QUICK_EDIT_MODE)) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
            quick_drag_begin(tile, QUICK_DRAG_TARGET_ITEM, (uint8_t)index);
        } else {
            quick_toast_show_text(item->toast_text);
            UI_SET_FLAG(QUICK_LONG_TRIGGERED, true);
            lv_indev_wait_release(lv_indev_active());
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (UI_FLAG(QUICK_EDIT_MODE)) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
            (void)quick_drag_end();
            return;
        }
        quick_panel_item_set_pressed(index, false);
        if (code == LV_EVENT_PRESS_LOST) {
            UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (UI_FLAG(QUICK_EDIT_MODE) || UI_FLAG(QUICK_LONG_TRIGGERED)) {
            UI_SET_FLAG(QUICK_LONG_TRIGGERED, false);
            return;
        }
        if (item->kind == QUICK_ITEM_LOCK) {
            screen_lock_enter();
            return;
        }
        const settings_detail_id_t detail_id = quick_panel_item_detail_id(item);
        if (detail_id != SETTINGS_DETAIL_NONE) {
            show_settings_view();
            settings_show_detail(detail_id);
            return;
        }
        const bool rebuilds_view = item->click_action == ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY ||
                                   item->click_action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS ||
                                   item->click_action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD;
        bool should_perform_action = true;
        if (!rebuilds_view && index < QUICK_PANEL_BUTTON_COUNT) {
            if (quick_panel_item_can_stay_active(item)) {
                const bool next_active = !ui_flag_get(s_ui.quick_panel_item_active_flags, index);
                ui_flag_set(&s_ui.quick_panel_item_local_override_flags, index, true);
                ui_flag_set(&s_ui.quick_panel_item_local_active_flags, index, next_active);
                should_perform_action = next_active || item->kind == QUICK_ITEM_BLUETOOTH;
            }
            quick_panel_item_apply_active(index, ui_flag_get(s_ui.quick_panel_item_local_override_flags, index) ?
                                                 ui_flag_get(s_ui.quick_panel_item_local_active_flags, index) :
                                                 ui_flag_get(s_ui.quick_panel_item_active_flags, index));
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
        const int32_t glyph_top = (int32_t)font->line_height - (int32_t)font->base_line -
                                  (int32_t)glyph.box_h - (int32_t)glyph.ofs_y;
        if (font == &bluetoothon || font == &wlanJZ) {
            label_x = ((content_w - (int32_t)glyph.box_w) / 2) - (int32_t)glyph.ofs_x;
            label_y = ((content_h - (int32_t)glyph.box_h) / 2) - glyph_top;
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        } else {
            const int32_t glyph_center_x2 = label_w - (int32_t)glyph.adv_w +
                                            (2 * (int32_t)glyph.ofs_x) + (int32_t)glyph.box_w;
            const int32_t glyph_center_y2 = (2 * glyph_top) + (int32_t)glyph.box_h;
            label_x = (content_w - glyph_center_x2) / 2;
            label_y = (content_h - glyph_center_y2) / 2;
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
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
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    quick_symbol_icon_recenter(icon_label, content_w, content_h, symbol, font);
    lv_obj_set_style_bg_opa(icon_label, LV_OPA_TRANSP, LV_PART_MAIN);
    return icon_label;
}

static void settings_navigation_apply_offset(int32_t offset)
{
    if (!s_ui.settings_page || !s_ui.settings_carousel || !s_ui.settings_detail ||
        !s_ui.settings_detail_header || !s_ui.settings_detail_edge_zone) {
        return;
    }

    offset = clamp_i32(offset, 0, SETTINGS_DETAIL_HEADER_H);
    const int32_t content_top = SETTINGS_DETAIL_HEADER_H - offset;
    const int32_t page_h = lv_obj_get_height(s_ui.settings_page);
    const bool keep_layout_guard = s_ui.settings_nav_layout_updating;

    s_ui.settings_nav_layout_updating = true;
    lv_obj_set_y(s_ui.settings_detail_header, -offset);
    lv_obj_set_style_pad_top(s_ui.settings_carousel, content_top, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_ui.settings_detail, content_top, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ui.settings_carousel, offset, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ui.settings_detail, 16 + offset, LV_PART_MAIN);
    lv_obj_set_pos(s_ui.settings_detail_edge_zone, 0, content_top);
    lv_obj_set_size(s_ui.settings_detail_edge_zone,
                    SETTINGS_SWIPE_EDGE_WIDTH,
                    page_h - content_top);
    lv_obj_update_layout(s_ui.settings_page);
    lv_obj_t *scroll_target = s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_NONE ?
                                  s_ui.settings_carousel : s_ui.settings_detail;
    s_ui.settings_nav_scroll_anchor_y = lv_obj_get_scroll_y(scroll_target);
    s_ui.settings_nav_layout_updating = keep_layout_guard;
}

static void settings_navigation_offset_anim_cb(void *obj, int32_t value)
{
    (void)obj;
    settings_navigation_apply_offset(value);
}

static void settings_navigation_offset_anim_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    s_ui.settings_nav_layout_updating = false;
}

static void settings_navigation_set_hidden(bool hidden, bool animated)
{
    if (!s_ui.settings_detail_header) {
        return;
    }

    const int32_t target = hidden ? SETTINGS_DETAIL_HEADER_H : 0;
    if (animated && s_ui.settings_nav_hidden == hidden) {
        return;
    }
    lv_anim_delete(s_ui.settings_detail_header, settings_navigation_offset_anim_cb);
    s_ui.settings_nav_layout_updating = false;

    if (!animated) {
        s_ui.settings_nav_hidden = hidden;
        settings_navigation_apply_offset(target);
        return;
    }

    const int32_t current = clamp_i32(-lv_obj_get_y(s_ui.settings_detail_header),
                                      0,
                                      SETTINGS_DETAIL_HEADER_H);
    s_ui.settings_nav_hidden = hidden;
    if (current == target) {
        settings_navigation_apply_offset(target);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.settings_detail_header);
    lv_anim_set_values(&anim, current, target);
    lv_anim_set_duration(&anim, SETTINGS_NAV_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, settings_navigation_offset_anim_cb);
    lv_anim_set_completed_cb(&anim, settings_navigation_offset_anim_completed_cb);
    s_ui.settings_nav_layout_updating = true;
    lv_anim_start(&anim);
}

static void settings_navigation_scroll_event_cb(lv_event_t *event)
{
    if (s_ui.settings_nav_layout_updating) {
        return;
    }

    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(event);
    if ((target == s_ui.settings_carousel &&
         s_ui.settings_detail_id != (uint8_t)SETTINGS_DETAIL_NONE) ||
        (target == s_ui.settings_detail &&
         s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_NONE)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    const int32_t scroll_y = lv_obj_get_scroll_y(target);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, true);
        s_ui.settings_nav_scroll_anchor_y = scroll_y;
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        s_ui.settings_nav_scroll_anchor_y = scroll_y;
        return;
    }
    if (code != LV_EVENT_SCROLL) {
        return;
    }

    const int32_t delta = scroll_y - s_ui.settings_nav_scroll_anchor_y;
    lv_indev_t *indev = lv_indev_active();
    const bool pointer_pressed = indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (delta >= SETTINGS_NAV_SCROLL_THRESHOLD) {
        settings_navigation_set_hidden(true, true);
        s_ui.settings_nav_scroll_anchor_y = scroll_y;
    } else if (delta <= -SETTINGS_NAV_SCROLL_THRESHOLD && pointer_pressed) {
        settings_navigation_set_hidden(false, true);
        s_ui.settings_nav_scroll_anchor_y = scroll_y;
    }
}

static void settings_navigation_track_drag(const lv_point_t *point)
{
    if (!point || !settings_view_is_visible() || s_ui.settings_bms_popup) {
        return;
    }

    const int32_t total_dx = point->x - s_ui.settings_swipe_start.x;
    const int32_t total_dy = point->y - s_ui.settings_swipe_start.y;
    if (abs_i32(total_dy) <= abs_i32(total_dx)) {
        return;
    }

    const int32_t drag_dy = point->y - s_ui.settings_nav_drag_anchor_y;
    if (abs_i32(drag_dy) < SETTINGS_NAV_SCROLL_THRESHOLD) {
        return;
    }

    UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, true);
    settings_navigation_set_hidden(drag_dy < 0, true);
    s_ui.settings_nav_drag_anchor_y = point->y;
}

static void settings_show_root(void)
{
    settings_bms_popup_close();
    if (s_ui.settings_swipe_drag_dx == 0) {
        settings_swipe_indicator_hide();
    }
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_NONE;
    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_ROOT;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    s_ui.settings_bms_ble_status = NULL;
    UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
    set_obj_hidden(s_ui.settings_detail, true);
    set_obj_hidden(s_ui.settings_root, false);
    if (s_ui.settings_carousel) {
        lv_obj_scroll_to_y(s_ui.settings_carousel, 0, LV_ANIM_OFF);
    }
    settings_detail_chrome_show(SETTINGS_DETAIL_NONE);
}

static const char *settings_detail_title_text(settings_detail_id_t detail_id)
{
    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_OPTIONS); ++index) {
        if (SETTINGS_OPTIONS[index].detail_id == detail_id) {
            return SETTINGS_OPTIONS[index].title;
        }
    }
    return "设置";
}

static void settings_detail_chrome_show(settings_detail_id_t detail_id)
{
    label_set_text_if_changed(s_ui.settings_detail_title,
                              settings_detail_title_text(detail_id));
    set_obj_hidden(s_ui.settings_detail_header, false);
    set_obj_hidden(s_ui.settings_detail_edge_zone, false);
    settings_navigation_set_hidden(false, false);
    s_ui.settings_nav_scroll_anchor_y = 0;
    lv_obj_move_foreground(s_ui.settings_detail_edge_zone);
    lv_obj_move_foreground(s_ui.settings_detail_header);
}

static void settings_navigate_back(void)
{
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_NONE) {
        show_dashboard_view();
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BMS &&
        s_ui.settings_bms_view != (uint8_t)SETTINGS_BMS_VIEW_ROOT) {
        settings_show_bms_detail();
        settings_navigation_set_hidden(false, false);
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_CONTROLLER &&
        s_ui.settings_controller_view != (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT) {
        settings_show_controller_detail();
        settings_navigation_set_hidden(false, false);
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_SYSTEM &&
        s_ui.settings_system_view != (uint8_t)SETTINGS_SYSTEM_VIEW_ROOT) {
        if (s_ui.settings_system_view == (uint8_t)SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION) {
            queue_action(ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION);
        }
        settings_show_detail(SETTINGS_DETAIL_SYSTEM);
        settings_navigation_set_hidden(false, false);
        return;
    }
    settings_show_root();
}

static void settings_detail_back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, true);
    settings_navigate_back();
    lv_indev_wait_release(lv_indev_active());
}

static void settings_swipe_indicator_x_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void settings_swipe_indicator_hide_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    set_obj_hidden(s_ui.settings_swipe_indicator, true);
}

static void settings_swipe_indicator_hide(void)
{
    if (!s_ui.settings_swipe_indicator) {
        return;
    }
    lv_anim_delete(s_ui.settings_swipe_indicator, settings_swipe_indicator_x_anim_cb);
    lv_obj_set_x(s_ui.settings_swipe_indicator, -SETTINGS_SWIPE_INDICATOR_SIZE);
    set_obj_hidden(s_ui.settings_swipe_indicator, true);
}

static void settings_swipe_indicator_set_drag(int32_t dx)
{
    if (!s_ui.settings_swipe_indicator) {
        return;
    }
    const int32_t max_x = SETTINGS_SWIPE_INDICATOR_SIZE / 2;
    const int32_t x = clamp_i32(dx - SETTINGS_SWIPE_INDICATOR_SIZE, -SETTINGS_SWIPE_INDICATOR_SIZE, max_x);
    lv_anim_delete(s_ui.settings_swipe_indicator, settings_swipe_indicator_x_anim_cb);
    lv_obj_set_x(s_ui.settings_swipe_indicator, x);
    set_obj_hidden(s_ui.settings_swipe_indicator, false);
    lv_obj_move_foreground(s_ui.settings_swipe_indicator);
}

static void settings_swipe_indicator_settle(bool committed)
{
    if (!s_ui.settings_swipe_indicator ||
        lv_obj_has_flag(s_ui.settings_swipe_indicator, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.settings_swipe_indicator);
    lv_anim_set_values(&anim,
                       lv_obj_get_x(s_ui.settings_swipe_indicator),
                       committed ? s_ui.width : -SETTINGS_SWIPE_INDICATOR_SIZE);
    lv_anim_set_duration(&anim, SETTINGS_SWIPE_INDICATOR_SETTLE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, settings_swipe_indicator_x_anim_cb);
    lv_anim_set_completed_cb(&anim, settings_swipe_indicator_hide_completed_cb);
    lv_anim_start(&anim);
}

static void settings_swipe_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, false);
        s_ui.settings_swipe_drag_dx = 0;
        const bool pointer_ready = get_active_pointer(&s_ui.settings_swipe_start);
        s_ui.settings_nav_drag_anchor_y = pointer_ready ? s_ui.settings_swipe_start.y : 0;
        const bool edge_start = pointer_ready &&
                                s_ui.settings_swipe_start.x <= SETTINGS_SWIPE_EDGE_WIDTH;
        UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, settings_view_is_visible() && edge_start);
        if (!edge_start) {
            settings_swipe_indicator_hide();
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        lv_point_t point = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }
        settings_navigation_track_drag(&point);
        if (!UI_FLAG(SETTINGS_SWIPE_TRACKING)) {
            return;
        }

        const int32_t dx = point.x - s_ui.settings_swipe_start.x;
        const int32_t dy = point.y - s_ui.settings_swipe_start.y;
        if (abs_i32(dy) > SETTINGS_SWIPE_BACK_MAX_DY &&
            abs_i32(dy) > abs_i32(dx)) {
            UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
            settings_swipe_indicator_settle(false);
            return;
        }

        if (dx > 3 && abs_i32(dy) <= SETTINGS_SWIPE_BACK_MAX_DY) {
            s_ui.settings_swipe_drag_dx = clamp_i32(dx, 0, s_ui.width);
            UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, true);
            settings_swipe_indicator_set_drag(s_ui.settings_swipe_drag_dx);
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const bool committed = code == LV_EVENT_RELEASED &&
                               s_ui.settings_swipe_drag_dx >= SETTINGS_SWIPE_BACK_MIN_DX;
        UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
        settings_swipe_indicator_settle(committed);
        if (committed) {
            ESP_LOGI(TAG,
                     "[settings] edge back committed: detail=%u dx=%ld",
                     (unsigned)s_ui.settings_detail_id,
                     (long)s_ui.settings_swipe_drag_dx);
            settings_navigate_back();
            lv_indev_wait_release(lv_indev_active());
        }
        s_ui.settings_swipe_drag_dx = 0;
        s_ui.settings_nav_drag_anchor_y = 0;
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

static lv_obj_t *settings_icon_action_button(lv_obj_t *parent,
                                             int32_t x,
                                             int32_t y,
                                             int32_t w,
                                             int32_t h,
                                             const char *symbol,
                                             const lv_font_t *font,
                                             lv_event_cb_t cb,
                                             void *user_data)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(box);
    if (cb) {
        lv_obj_add_event_cb(box, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *icon = label(box, 0, 0, w, h, font ? font : &lv_font_montserrat_24);
    lv_label_set_text(icon, symbol ? symbol : "");
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    quick_symbol_icon_recenter(icon, w, h, symbol ? symbol : "", font ? font : &lv_font_montserrat_24);
    return box;
}

static const esp_bms_dashboard_snapshot_t *settings_current_snapshot(void)
{
    static const esp_bms_dashboard_snapshot_t empty_snapshot = { 0 };
    return UI_FLAG(LAST_SNAPSHOT_VALID) ? &s_ui.last_snapshot : &empty_snapshot;
}

static lv_obj_t *settings_list_card(lv_obj_t *parent,
                                    int32_t x,
                                    int32_t y,
                                    int32_t w,
                                    int32_t row_h,
                                    size_t row_count)
{
    lv_obj_t *card = panel(parent,
                           x,
                           y,
                           w,
                           row_h * (int32_t)row_count,
                           COLOR_SETTINGS_LIST);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN);
    return card;
}

static const char *settings_bms_type_label(uint8_t type)
{
    return type < ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS) ? SETTINGS_BMS_TYPE_LABELS[type] :
                                                          SETTINGS_BMS_TYPE_LABELS[0];
}

static void settings_bms_popup_close(void)
{
    if (s_ui.settings_bms_popup) {
        lv_obj_delete(s_ui.settings_bms_popup);
        s_ui.settings_bms_popup = NULL;
        s_ui.settings_bms_ble_status = NULL;
        s_ui.settings_bms_ble_popup_open = false;
    }
}

static bool settings_bms_popup_click_ready(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return false;
    }
    UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
    return true;
}

static void settings_show_bms_type_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = 8;
    const int32_t card_w = s_ui.width - 16;
    const int32_t row_h = portrait ? 48 : 38;
    const int32_t gap = portrait ? 7 : 5;
    const int32_t first_y = 12;
    const uint8_t current = settings_current_snapshot()->bms_type;

    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_TYPE_LIST;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "保护板类型");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS); ++index) {
        const bool active = index == current;
        lv_obj_t *row = panel(s_ui.settings_detail,
                              card_x,
                              first_y + ((int32_t)index * (row_h + gap)),
                              card_w,
                              row_h,
                              COLOR_SETTINGS_CARD);
        lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, active ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row,
                                      active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        settings_add_swipe_handlers(row);
        lv_obj_add_event_cb(row,
                            settings_bms_type_option_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        const lv_font_t *text_font = &settings_zh_13;
        const int32_t text_h = (int32_t)text_font->line_height + 4;
        lv_obj_t *text = label(row, 12, (row_h - text_h) / 2, card_w - 52, text_h,
                               text_font);
        lv_label_set_text(text, SETTINGS_BMS_TYPE_LABELS[index]);
        lv_obj_set_style_text_color(text,
                                    active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_TEXT,
                                    LV_PART_MAIN);
        if (active) {
            lv_obj_t *check = label(row, card_w - 36, (row_h - text_h) / 2, 24, text_h,
                                    &lv_font_montserrat_14);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);
        }
    }
}

static void settings_bms_ble_format_status(char *out,
                                           size_t out_len,
                                           const esp_bms_dashboard_snapshot_t *snapshot,
                                           settings_ble_source_t source,
                                           bool scan_requested)
{
    if (!out || out_len == 0U) {
        return;
    }
    if (scan_requested ||
        (source == SETTINGS_BLE_SOURCE_CONTROLLER && snapshot->controller_scan_active != 0U) ||
        (source == SETTINGS_BLE_SOURCE_BMS &&
         strcmp(snapshot->bms_info_text, "BMS SCAN") == 0)) {
        (void)snprintf(out, out_len, "扫描...");
    } else if (source == SETTINGS_BLE_SOURCE_CONTROLLER &&
               SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE)) {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       snapshot->controller_bound_name[0] != '\0'
                           ? snapshot->controller_bound_name
                           : "已连接");
    } else if (source == SETTINGS_BLE_SOURCE_BMS && SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       snapshot->bms_bound_name[0] != '\0' ? snapshot->bms_bound_name : "已连接");
    } else if ((source == SETTINGS_BLE_SOURCE_BMS ? snapshot->bms_scan_candidate_count
                                                  : snapshot->controller_scan_candidate_count) > 0U) {
        const uint8_t count = source == SETTINGS_BLE_SOURCE_BMS
                                  ? snapshot->bms_scan_candidate_count
                                  : snapshot->controller_scan_candidate_count;
        (void)snprintf(out, out_len, "发现 %u", (unsigned)count);
    } else if (source == SETTINGS_BLE_SOURCE_CONTROLLER &&
               snapshot->controller_bound_name[0] != '\0') {
        (void)snprintf(out, out_len, "%s", snapshot->controller_bound_name);
    } else if (source == SETTINGS_BLE_SOURCE_BMS && snapshot->bms_info_text[0] != '\0') {
        (void)snprintf(out, out_len, "%.15s", snapshot->bms_info_text);
    } else {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       source == SETTINGS_BLE_SOURCE_BMS ? "未发现保护板" : "未发现控制器");
    }
}

static void settings_bms_ble_start_scan(void)
{
    if (s_ui.settings_bms_ble_status) {
        label_set_text_if_changed(s_ui.settings_bms_ble_status, "扫描...");
    }
    const settings_ble_source_t source = (settings_ble_source_t)s_ui.settings_ble_source;
    ESP_LOGI(TAG, "[ble-ui] queue %s scan from list page",
             source == SETTINGS_BLE_SOURCE_BMS ? "BMS" : "controller");
    queue_action(source == SETTINGS_BLE_SOURCE_BMS ? ESP_BMS_LVGL_ACTION_START_BMS_BIND
                                                    : ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND);
}

static void settings_show_bms_ble_popup(settings_ble_source_t source, bool start_scan)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = 8;
    const int32_t card_w = s_ui.width - 16;
    const int32_t status_h = portrait ? 48 : 38;
    const int32_t refresh_w = status_h;
    const int32_t gap = portrait ? 7 : 5;
    const int32_t status_w = card_w - refresh_w - gap;
    const int32_t first_y = 12;
    const int32_t list_y = first_y + status_h + gap;
    const int32_t row_h = portrait ? 48 : 42;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    char status_text[24] = { 0 };

    s_ui.settings_ble_source = (uint8_t)source;
    if (source == SETTINGS_BLE_SOURCE_BMS) {
        s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST;
    } else {
        s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST;
    }
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "蓝牙连接");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    lv_obj_t *status = panel(s_ui.settings_detail,
                             card_x,
                             first_y,
                             status_w,
                             status_h,
                             COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(status, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(status, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(status, LV_OPA_COVER, LV_PART_MAIN);
    settings_bms_ble_format_status(status_text,
                                   sizeof(status_text),
                                   snapshot,
                                   source,
                                   start_scan);
    s_ui.settings_bms_ble_status = label(status,
                                         10,
                                         (status_h - ((int32_t)settings_zh_13.line_height + 4)) / 2,
                                         status_w - 20,
                                         (int32_t)settings_zh_13.line_height + 4,
                                         &settings_zh_13);
    lv_label_set_text(s_ui.settings_bms_ble_status, status_text);
    lv_obj_set_style_text_color(s_ui.settings_bms_ble_status, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    settings_icon_action_button(s_ui.settings_detail,
                                card_x + status_w + gap,
                                first_y,
                                refresh_w,
                                status_h,
                                LV_SYMBOL_REFRESH,
                                &lv_font_montserrat_24,
                                settings_bms_ble_refresh_event_cb,
                                NULL);

    const uint8_t source_count = source == SETTINGS_BLE_SOURCE_BMS
                                     ? snapshot->bms_scan_candidate_count
                                     : snapshot->controller_scan_candidate_count;
    const esp_bms_bms_scan_candidate_t *candidates =
        source == SETTINGS_BLE_SOURCE_BMS ? snapshot->bms_scan_candidates
                                          : snapshot->controller_scan_candidates;
    const uint8_t count = source_count > ESP_BMS_BMS_SCAN_MAX_CANDIDATES
                              ? ESP_BMS_BMS_SCAN_MAX_CANDIDATES
                              : source_count;
    if (count == 0U || start_scan) {
        const int32_t empty_h = (int32_t)settings_zh_13.line_height + 8;
        lv_obj_t *empty = label(s_ui.settings_detail,
                                card_x,
                                list_y + 18,
                                card_w,
                                empty_h,
                                &settings_zh_13);
        lv_label_set_text(empty,
                          start_scan
                              ? "扫描..."
                              : source == SETTINGS_BLE_SOURCE_BMS ? "未发现保护板"
                                                                  : "未发现控制器");
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    } else {
        for (uint8_t index = 0; index < count; ++index) {
            const esp_bms_bms_scan_candidate_t *candidate = &candidates[index];
            lv_obj_t *row = panel(s_ui.settings_detail,
                                  card_x,
                                  list_y + ((int32_t)index * (row_h + gap)),
                                  card_w,
                                  row_h,
                                  COLOR_SETTINGS_CARD);
            lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(row, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
            lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            settings_add_swipe_handlers(row);
            lv_obj_add_event_cb(row,
                                settings_bms_ble_candidate_event_cb,
                                LV_EVENT_CLICKED,
                                (void *)candidate);

            char fallback_name[16] = { 0 };
            const bool has_name = candidate->has_name && candidate->name[0] != '\0';
            if (!has_name) {
                (void)snprintf(fallback_name, sizeof(fallback_name), "设备 %u", (unsigned)index + 1U);
            }
            const char *name = has_name ? candidate->name : fallback_name;
            const lv_font_t *name_font = &settings_zh_13;
            const lv_font_t *metadata_font = &settings_zh_10;
            const int32_t name_h = (int32_t)name_font->line_height + 2;
            const int32_t text_y = (row_h - name_h) / 2;
            lv_obj_t *name_label = label(row, 10, text_y, card_w - 88, name_h, name_font);
            lv_label_set_text(name_label, name);
            lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(name_label, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

            char rssi[12] = { 0 };
            if (candidate->rssi > INT8_MIN) {
                (void)snprintf(rssi, sizeof(rssi), "%d dBm", (int)candidate->rssi);
            } else {
                (void)snprintf(rssi, sizeof(rssi), "--");
            }
            const int32_t metadata_h = (int32_t)metadata_font->line_height + 4;
            lv_obj_t *rssi_label = label(row, card_w - 70, (row_h - metadata_h) / 2, 58, metadata_h,
                                         metadata_font);
            lv_label_set_text(rssi_label, rssi);
            lv_obj_set_style_text_align(rssi_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_set_style_text_color(rssi_label, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
        }
    }

    if (start_scan) {
        settings_bms_ble_start_scan();
    }
}

static void settings_show_hotspot_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();

    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = 12;
    const int32_t card_w = portrait ? s_ui.width - 24 : (s_ui.width / 2) - 16;
    const int32_t row_h = portrait ? 56 : 48;
    const int32_t gap = 8;
    const int32_t info_y = 12 + row_h + gap;
    const int32_t info_h = portrait ? 78 : 96;
    const settings_detail_row_t control_row = {
        "热点共享",
        SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "热点已打开" : "未打开",
        ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *list_card = settings_list_card(s_ui.settings_detail,
                                             card_x,
                                             12,
                                             card_w,
                                             row_h,
                                             1);
    s_ui.setup_ap_control_row =
        settings_detail_row(list_card, 0, 0, card_w, row_h, &control_row);

    lv_obj_t *info = panel(s_ui.settings_detail, card_x, info_y, card_w, info_h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(info, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(info, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(info, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(info, LV_OPA_COVER, LV_PART_MAIN);
    s_ui.setup_ap_info = label(info,
                               8,
                               8,
                               card_w - 16,
                               info_h - 16,
                               &settings_zh_13);
    lv_obj_set_style_text_color(s_ui.setup_ap_info, COLOR_SETTINGS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_ui.setup_ap_info, SETUP_AP_INFO_LINE_SPACE, LV_PART_MAIN);

#if LV_USE_QRCODE
    const int32_t qr_size = portrait ? clamp_i32(s_ui.width - 104, 96, 140) :
                                      clamp_i32(s_ui.height - 96, 80, 120);
    const int32_t qr_panel_w = qr_size + 18;
    const int32_t qr_panel_h = qr_size + 18;
    const int32_t qr_x = portrait ? (s_ui.width - qr_panel_w) / 2 : (s_ui.width - qr_panel_w - 12);
    const int32_t qr_y = portrait ? (info_y + info_h + 10) : 58;
    s_ui.setup_ap_qr_panel = panel(s_ui.settings_detail,
                                  qr_x,
                                  qr_y,
                                  qr_panel_w,
                                  qr_panel_h,
                                  COLOR_WHITE);
    lv_obj_set_style_radius(s_ui.setup_ap_qr_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.setup_ap_qr_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.setup_ap_qr_panel, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_ui.setup_ap_qr_panel, LV_OPA_COVER, LV_PART_MAIN);
    s_ui.setup_ap_qr = lv_qrcode_create(s_ui.setup_ap_qr_panel);
    if (s_ui.setup_ap_qr) {
        lv_qrcode_set_size(s_ui.setup_ap_qr, qr_size);
        lv_qrcode_set_dark_color(s_ui.setup_ap_qr, COLOR_SETTINGS_BG);
        lv_qrcode_set_light_color(s_ui.setup_ap_qr, COLOR_WHITE);
        lv_qrcode_set_quiet_zone(s_ui.setup_ap_qr, true);
        lv_obj_center(s_ui.setup_ap_qr);
    }
    set_obj_hidden(s_ui.setup_ap_qr_panel, true);
#endif

    set_setup_ap(snapshot);
}

static const char *bluetooth_status_text(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "附近不可见";
    }
    if (SNAPSHOT_FLAG(snapshot, BLUETOOTH_CONNECTED)) {
        return "已连接";
    }
    if (SNAPSHOT_FLAG(snapshot, BLUETOOTH_ADVERTISING)) {
        return "可被发现";
    }
    return "附近不可见";
}

static void settings_show_bluetooth_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();

    const int32_t card_x = 12;
    const int32_t card_w = s_ui.width - 24;
    const int32_t row_h = s_ui.width < s_ui.height ? 56 : 48;
    const int32_t first_y = 12;

    const settings_detail_row_t rows[] = {
        { "状态", bluetooth_status_text(snapshot), ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "名称", snapshot->bluetooth_name[0] != '\0' ? snapshot->bluetooth_name : "ESP32 BMS GPS",
          ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "可被发现", "附近可见", ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING,
          SETTINGS_SYSTEM_VIEW_ROOT },
    };

    lv_obj_t *list_card = settings_list_card(s_ui.settings_detail,
                                             card_x,
                                             first_y,
                                             card_w,
                                             row_h,
                                             ARRAY_SIZE(rows));
    for (size_t index = 0; index < ARRAY_SIZE(rows); ++index) {
        settings_detail_row(list_card,
                            0,
                            (int32_t)index * row_h,
                            card_w,
                            row_h,
                            &rows[index]);
    }
}

static void settings_show_bms_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const int32_t card_x = 12;
    const int32_t card_w = s_ui.width - 24;
    const int32_t row_h = s_ui.width < s_ui.height ? 56 : 48;
    char ble_status[ESP_BMS_BMS_SCAN_NAME_LEN + 1U] = { 0 };

    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_ROOT;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    memset(s_ui.settings_controller_tire_rollers,
           0,
           sizeof(s_ui.settings_controller_tire_rollers));
    s_ui.settings_controller_ratio_roller = NULL;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "保护板设置");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    settings_bms_ble_format_status(ble_status,
                                   sizeof(ble_status),
                                   snapshot,
                                   SETTINGS_BLE_SOURCE_BMS,
                                   false);
    const settings_detail_row_t ble_row = {
        "蓝牙连接",
        ble_status,
        ESP_BMS_LVGL_ACTION_START_BMS_BIND,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    const settings_detail_row_t type_row = {
        "保护板类型",
        settings_bms_type_label(snapshot->bms_type),
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *list_card = settings_list_card(s_ui.settings_detail,
                                             card_x,
                                             12,
                                             card_w,
                                             row_h,
                                             2);
    settings_detail_row(list_card,
                        0,
                        0,
                        card_w,
                        row_h,
                        &ble_row);

    lv_obj_t *type_box = settings_detail_row(list_card,
                                              0,
                                              row_h,
                                              card_w,
                                              row_h,
                                              &type_row);
    lv_obj_add_event_cb(type_box, settings_bms_type_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = label(type_box, card_w - 24, 0, 14, 15, &settings_zh_13);
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
}

static const char CONTROLLER_RIM_OPTIONS[] =
    "8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24";
static const char CONTROLLER_ASPECT_OPTIONS[] =
    "30\n35\n40\n45\n50\n55\n60\n65\n70\n75\n80\n85\n90\n95\n100";
static const char CONTROLLER_WIDTH_OPTIONS[] =
    "50\n55\n60\n65\n70\n75\n80\n85\n90\n95\n100\n105\n110\n115\n120\n125\n130\n135\n140\n145\n150\n155\n160\n165\n170\n175\n180\n185\n190\n195\n200";

static lv_obj_t *settings_controller_roller(lv_obj_t *parent,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h,
                                            const char *options,
                                            uint32_t selected)
{
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
    lv_obj_set_pos(roller, x, y);
    lv_obj_set_size(roller, w, h);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, COLOR_WHITE, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller, COLOR_SETTINGS_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, COLOR_SWITCH_ACTIVE, LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, COLOR_WHITE, LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller, 1, LV_PART_SELECTED);
    lv_obj_set_style_border_side(roller,
                                 LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM,
                                 LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 6, LV_PART_SELECTED);
    /* Roller vertical drags select values; the persistent edge zone owns back navigation. */
    return roller;
}

static char *settings_controller_ratio_options(void)
{
    const size_t capacity = 6000U;
    char *options = lv_malloc(capacity);
    if (!options) {
        return NULL;
    }
    size_t used = 0U;
    for (uint16_t value = ESP_BMS_CONTROLLER_RATIO_CENTI_MIN;
         value <= ESP_BMS_CONTROLLER_RATIO_CENTI_MAX;
         ++value) {
        const int written = lv_snprintf(options + used,
                                        capacity - used,
                                        value == ESP_BMS_CONTROLLER_RATIO_CENTI_MAX
                                            ? "%u.%02u"
                                            : "%u.%02u\n",
                                        value / 100U,
                                        value % 100U);
        if (written < 0 || (size_t)written >= capacity - used) {
            lv_free(options);
            return NULL;
        }
        used += (size_t)written;
    }
    return options;
}

static void settings_controller_confirm_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    if (s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_TIRE_EDIT) {
        if (!s_ui.settings_controller_tire_rollers[0] ||
            !s_ui.settings_controller_tire_rollers[1] ||
            !s_ui.settings_controller_tire_rollers[2]) {
            return;
        }
        s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE;
        s_ui.pending_event.controller_tire_rim_inch =
            (uint8_t)(ESP_BMS_CONTROLLER_TIRE_RIM_MIN +
                      lv_roller_get_selected(s_ui.settings_controller_tire_rollers[0]));
        s_ui.pending_event.controller_tire_aspect_percent =
            (uint8_t)(ESP_BMS_CONTROLLER_TIRE_ASPECT_MIN +
                      lv_roller_get_selected(s_ui.settings_controller_tire_rollers[1]) *
                          ESP_BMS_CONTROLLER_TIRE_ASPECT_STEP);
        s_ui.pending_event.controller_tire_width_mm =
            (uint16_t)(ESP_BMS_CONTROLLER_TIRE_WIDTH_MIN +
                       lv_roller_get_selected(s_ui.settings_controller_tire_rollers[2]) *
                           ESP_BMS_CONTROLLER_TIRE_WIDTH_STEP);
    } else if (s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_RATIO_EDIT &&
               s_ui.settings_controller_ratio_roller) {
        s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO;
        s_ui.pending_event.controller_gear_ratio_centi =
            (uint16_t)(ESP_BMS_CONTROLLER_RATIO_CENTI_MIN +
                       lv_roller_get_selected(s_ui.settings_controller_ratio_roller));
    } else {
        return;
    }
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, CONTROLLER_SETTING_VALID, true);
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, true);
    settings_show_controller_detail();
    lv_indev_wait_release(lv_indev_active());
}

static void settings_controller_confirm_button(lv_obj_t *parent, int32_t y)
{
    const int32_t button_w = clamp_i32(s_ui.width - 64, 160, 240);
    lv_obj_t *button = panel(parent,
                             (s_ui.width - button_w) / 2,
                             y,
                             button_w,
                             42,
                             COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(button);
    lv_obj_add_event_cb(button, settings_controller_confirm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *text = label(button, 0, 10, button_w, 20, &settings_zh_16);
    lv_label_set_text(text, "确认");
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, COLOR_WHITE, LV_PART_MAIN);
}

static void settings_show_controller_tire_edit(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const bool portrait = s_ui.width < s_ui.height;
    const uint8_t rim = snapshot->controller_fallback_tire_rim_inch >=
                                    ESP_BMS_CONTROLLER_TIRE_RIM_MIN &&
                                snapshot->controller_fallback_tire_rim_inch <=
                                    ESP_BMS_CONTROLLER_TIRE_RIM_MAX
                            ? snapshot->controller_fallback_tire_rim_inch
                            : 12U;
    const uint8_t aspect = snapshot->controller_fallback_tire_aspect_percent >=
                                       ESP_BMS_CONTROLLER_TIRE_ASPECT_MIN &&
                                   snapshot->controller_fallback_tire_aspect_percent <=
                                       ESP_BMS_CONTROLLER_TIRE_ASPECT_MAX
                               ? snapshot->controller_fallback_tire_aspect_percent
                               : 70U;
    const uint16_t width = snapshot->controller_fallback_tire_width_mm >=
                                       ESP_BMS_CONTROLLER_TIRE_WIDTH_MIN &&
                                   snapshot->controller_fallback_tire_width_mm <=
                                       ESP_BMS_CONTROLLER_TIRE_WIDTH_MAX
                               ? snapshot->controller_fallback_tire_width_mm
                               : 90U;

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_TIRE_EDIT;
    label_set_text_if_changed(s_ui.settings_detail_title, "轮胎规格");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
    const int32_t card_x = 12;
    const int32_t card_w = s_ui.width - 24;
    const int32_t card_h = portrait ? 188 : 118;
    lv_obj_t *card = panel(s_ui.settings_detail, card_x, 12, card_w, card_h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    const char *const titles[] = { "轮辋", "扁平比", "胎宽" };
    const char *const options[] = {
        CONTROLLER_RIM_OPTIONS,
        CONTROLLER_ASPECT_OPTIONS,
        CONTROLLER_WIDTH_OPTIONS,
    };
    const uint32_t selected[] = {
        rim - ESP_BMS_CONTROLLER_TIRE_RIM_MIN,
        (aspect - ESP_BMS_CONTROLLER_TIRE_ASPECT_MIN) /
            ESP_BMS_CONTROLLER_TIRE_ASPECT_STEP,
        (width - ESP_BMS_CONTROLLER_TIRE_WIDTH_MIN) /
            ESP_BMS_CONTROLLER_TIRE_WIDTH_STEP,
    };
    const int32_t gap = 6;
    const int32_t roller_w = (card_w - 24 - gap * 2) / 3;
    const int32_t roller_h = card_h - 42;
    for (uint8_t index = 0; index < 3U; ++index) {
        const int32_t x = 12 + index * (roller_w + gap);
        lv_obj_t *title = label(card, x, 7, roller_w, 20, &settings_zh_13);
        lv_label_set_text(title, titles[index]);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s_ui.settings_controller_tire_rollers[index] =
            settings_controller_roller(card,
                                       x,
                                       30,
                                       roller_w,
                                       roller_h,
                                       options[index],
                                       selected[index]);
    }
    s_ui.settings_controller_ratio_roller = NULL;
    settings_controller_confirm_button(s_ui.settings_detail, 20 + card_h);
}

static void settings_show_controller_ratio_edit(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const uint16_t ratio = snapshot->controller_fallback_gear_ratio_centi >=
                                       ESP_BMS_CONTROLLER_RATIO_CENTI_MIN &&
                                   snapshot->controller_fallback_gear_ratio_centi <=
                                       ESP_BMS_CONTROLLER_RATIO_CENTI_MAX
                               ? snapshot->controller_fallback_gear_ratio_centi
                               : ESP_BMS_CONTROLLER_RATIO_CENTI_DEFAULT;
    char *options = settings_controller_ratio_options();
    if (!options) {
        return;
    }
    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_RATIO_EDIT;
    label_set_text_if_changed(s_ui.settings_detail_title, "传动比");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
    const int32_t roller_w = clamp_i32(s_ui.width - 96, 128, 220);
    const int32_t roller_h = s_ui.width < s_ui.height ? 178 : 112;
    s_ui.settings_controller_ratio_roller =
        settings_controller_roller(s_ui.settings_detail,
                                   (s_ui.width - roller_w) / 2,
                                   12,
                                   roller_w,
                                   roller_h,
                                   options,
                                   ratio - ESP_BMS_CONTROLLER_RATIO_CENTI_MIN);
    lv_free(options);
    memset(s_ui.settings_controller_tire_rollers,
           0,
           sizeof(s_ui.settings_controller_tire_rollers));
    settings_controller_confirm_button(s_ui.settings_detail, 20 + roller_h);
}

static void settings_controller_value_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    const settings_controller_view_t view =
        (settings_controller_view_t)(uintptr_t)lv_event_get_user_data(event);
    if (view == SETTINGS_CONTROLLER_VIEW_TIRE_EDIT) {
        settings_show_controller_tire_edit();
    } else if (view == SETTINGS_CONTROLLER_VIEW_RATIO_EDIT) {
        settings_show_controller_ratio_edit();
    }
}

static void settings_controller_value_row(lv_obj_t *parent,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          const char *title,
                                          const char *value,
                                          settings_controller_view_t view,
                                          bool editable)
{
    const settings_detail_row_t descriptor = {
        title,
        value,
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *box = settings_detail_row(parent, 0, y, w, h, &descriptor);
    if (!editable) {
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(box, LV_STATE_DISABLED);
        lv_obj_set_style_opa(box, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
        return;
    }
    lv_obj_add_event_cb(box,
                        settings_controller_value_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)view);
    lv_obj_t *arrow = label(box, w - 24, 0, 14, 15, &settings_zh_13);
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
}

static void settings_show_controller_detail(void)
{
    const int32_t card_x = 12;
    const int32_t card_w = s_ui.width - 24;
    const int32_t row_h = s_ui.width < s_ui.height ? 56 : 48;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const bool online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    const bool controller_synced =
        snapshot->controller_param_source ==
        (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_CONTROLLER;
    const bool values_editable = online && !controller_synced;
    const size_t visible_row_count = online ? 7U : 4U;
    char ble_status[ESP_BMS_BMS_SCAN_NAME_LEN + 1U] = { 0 };
    char speed_source[40] = { 0 };
    char tire[48] = { 0 };
    char ratio[40] = { 0 };

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_CONTROLLER;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    s_ui.settings_bms_ble_status = NULL;
    label_set_text_if_changed(s_ui.settings_detail_title, "速度仪表");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    settings_bms_ble_format_status(ble_status,
                                   sizeof(ble_status),
                                   snapshot,
                                   SETTINGS_BLE_SOURCE_CONTROLLER,
                                   false);
    if (snapshot->speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER) {
        (void)snprintf(speed_source,
                       sizeof(speed_source),
                       online ? "控制器" : "控制器 / 离线·当前 GPS");
    } else {
        (void)snprintf(speed_source, sizeof(speed_source), "GPS");
    }
    const settings_detail_row_t rows[] = {
        { "速度来源", speed_source,
          ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "控制器连接", "连接远驱控制器",
          ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION, SETTINGS_SYSTEM_VIEW_ROOT },
        { "蓝牙绑定", ble_status,
          ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND, SETTINGS_SYSTEM_VIEW_ROOT },
        { "速度单位", snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h",
          ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT, SETTINGS_SYSTEM_VIEW_ROOT },
        { "控制器类型", "远驱", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    };

    lv_obj_t *card = settings_list_card(s_ui.settings_detail,
                                        card_x,
                                        12,
                                        card_w,
                                        row_h,
                                        visible_row_count);
    size_t visible_index = 0U;
    for (size_t index = 0; index < ARRAY_SIZE(rows); ++index) {
        if (!online && rows[index].action == ESP_BMS_LVGL_ACTION_NONE) {
            continue;
        }
        settings_detail_row(card,
                            0,
                            (int32_t)visible_index * row_h,
                            card_w,
                            row_h,
                            &rows[index]);
        visible_index++;
    }

    if (online) {
        if (snapshot->controller_param_source ==
                (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_CONTROLLER ||
            snapshot->controller_param_source ==
                (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LOCAL) {
            (void)snprintf(tire,
                           sizeof(tire),
                           controller_synced ? "%u-%u-%u 控制器同步" : "%u-%u-%u",
                           snapshot->controller_tire_rim_inch,
                           snapshot->controller_tire_aspect_percent,
                           snapshot->controller_tire_width_mm);
        } else if (snapshot->controller_param_source ==
                   (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LEGACY_WHEEL) {
            (void)snprintf(tire,
                           sizeof(tire),
                           "旧周长 %u mm",
                           snapshot->controller_wheel_circumference_mm);
        } else {
            (void)snprintf(tire, sizeof(tire), "未设置");
        }
        (void)snprintf(ratio,
                       sizeof(ratio),
                       controller_synced ? "%u.%02u 控制器同步" : "%u.%02u",
                       snapshot->controller_gear_ratio_centi / 100U,
                       snapshot->controller_gear_ratio_centi % 100U);
        settings_controller_value_row(card,
                                      (int32_t)visible_index++ * row_h,
                                      card_w,
                                      row_h,
                                      "轮胎规格",
                                      tire,
                                      SETTINGS_CONTROLLER_VIEW_TIRE_EDIT,
                                      values_editable);
        settings_controller_value_row(card,
                                      (int32_t)visible_index * row_h,
                                      card_w,
                                      row_h,
                                      "传动比",
                                      ratio,
                                      SETTINGS_CONTROLLER_VIEW_RATIO_EDIT,
                                      values_editable);
    }
    lv_obj_update_layout(s_ui.settings_detail);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
}

static bool settings_detail_action_uses_switch(esp_bms_lvgl_action_t action)
{
    return action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING ||
           action == ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE;
}

static bool settings_detail_action_switch_on(esp_bms_lvgl_action_t action)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    if (!UI_FLAG(LAST_SNAPSHOT_VALID)) {
        return false;
    }
    if (action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING) {
        return SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) || snapshot->wifi == ESP_BMS_WIFI_SETUP_AP;
    }
    if (action == ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING) {
        return SNAPSHOT_FLAG(snapshot, BLUETOOTH_ADVERTISING);
    }
    if (action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION) {
        return SNAPSHOT_FLAG(snapshot, CONTROLLER_CONNECTION_ENABLED);
    }
    if (action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE) {
        return SNAPSHOT_FLAG(snapshot, CONTROLLER_PAGE_ENABLED);
    }
    return false;
}

static void settings_detail_switch(lv_obj_t *parent, int32_t x, int32_t y, bool enabled)
{
    const int32_t w = 34;
    const int32_t h = 18;
    const int32_t knob = 14;
    lv_obj_t *track = lv_obj_create(parent);
    clear_style(track);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, w, h);
    lv_obj_set_style_radius(track, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(track, enabled ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(track, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(track, enabled ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *thumb = lv_obj_create(track);
    clear_style(thumb);
    lv_obj_set_size(thumb, knob, knob);
    lv_obj_set_pos(thumb, enabled ? (w - knob - 2) : 2, 2);
    lv_obj_set_style_radius(thumb, knob / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(thumb, enabled ? COLOR_WHITE : COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void settings_restore_popup_close(void)
{
    if (!s_ui.settings_restore_popup) {
        return;
    }
    lv_obj_delete(s_ui.settings_restore_popup);
    s_ui.settings_restore_popup = NULL;
}

static void settings_restore_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        settings_restore_popup_close();
    }
}

static void settings_restore_accept_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    queue_action(ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS);
    settings_restore_popup_close();
}

static void settings_restore_confirm_show(void)
{
    settings_restore_popup_close();
    s_ui.settings_restore_popup = lv_obj_create(lv_layer_top());
    clear_style(s_ui.settings_restore_popup);
    lv_obj_set_pos(s_ui.settings_restore_popup, 0, 0);
    lv_obj_set_size(s_ui.settings_restore_popup, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(s_ui.settings_restore_popup, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_restore_popup, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_restore_popup, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_ui.settings_restore_popup, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t dialog_w = clamp_i32(s_ui.width - 32, 208, 288);
    const int32_t dialog_h = 136;
    lv_obj_t *dialog = panel(s_ui.settings_restore_popup,
                             (s_ui.width - dialog_w) / 2,
                             (s_ui.height - dialog_h) / 2,
                             dialog_w,
                             dialog_h,
                             COLOR_PANEL_ALT);
    lv_obj_set_style_radius(dialog, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);

    lv_obj_t *title = label(dialog, 12, 12, dialog_w - 24, 22, &settings_zh_16);
    lv_label_set_text(title, "恢复默认");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);
    lv_obj_t *message = label(dialog, 12, 42, dialog_w - 24, 34, &settings_zh_13);
    lv_label_set_text(message, "清除设置与屏幕校准？");
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(message, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    const int32_t gap = 12;
    const int32_t button_w = (dialog_w - 36 - gap) / 2;
    lv_obj_t *cancel = panel(dialog, 12, 86, button_w, 38, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(cancel, 7, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, settings_restore_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_icon = label(cancel, 0, 7, button_w, 24, &lv_font_montserrat_24);
    lv_label_set_text(cancel_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_align(cancel_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_icon, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_obj_t *confirm = panel(dialog, 12 + button_w + gap, 86, button_w, 38, COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(confirm, 7, LV_PART_MAIN);
    lv_obj_add_flag(confirm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confirm, settings_restore_accept_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_icon = label(confirm, 0, 7, button_w, 24, &lv_font_montserrat_24);
    lv_label_set_text(confirm_icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_align(confirm_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(confirm_icon, COLOR_WHITE, LV_PART_MAIN);
}

static void settings_detail_action_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }

    const uintptr_t route = (uintptr_t)lv_event_get_user_data(event);
    const esp_bms_lvgl_action_t action = (esp_bms_lvgl_action_t)(route & UINT8_MAX);
    const settings_system_view_t system_view = (settings_system_view_t)(route >> 8);
    if (system_view != SETTINGS_SYSTEM_VIEW_ROOT) {
        settings_show_system_view(system_view);
        return;
    }

    if (action == ESP_BMS_LVGL_ACTION_START_BMS_BIND) {
        ESP_LOGI(TAG, "[bms-ui] open BLE list page and start scan");
        settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_BMS, true);
        return;
    }
    if (action == ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND) {
        ESP_LOGI(TAG, "[controller-ui] open BLE list page and start scan");
        settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_CONTROLLER, true);
        return;
    }
    if (action == ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS) {
        settings_restore_confirm_show();
        return;
    }
    if (action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION ||
        action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE ||
        action == ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL ||
        action == ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO) {
        queue_action_with_commit(action, true);
        return;
    }
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        perform_ui_action(action, false);
    }
}

static void settings_bms_type_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    ESP_LOGI(TAG, "[bms-ui] open BMS type list page");
    settings_show_bms_type_picker();
}

static void settings_bms_type_option_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }

    const size_t selected = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (selected >= ARRAY_SIZE(SETTINGS_BMS_TYPE_ACTIONS)) {
        return;
    }

    ESP_LOGI(TAG, "[bms-ui] BMS type selected: %s", SETTINGS_BMS_TYPE_LABELS[selected]);
    if (!UI_FLAG(LAST_SNAPSHOT_VALID) || settings_current_snapshot()->bms_type != selected) {
        queue_action(SETTINGS_BMS_TYPE_ACTIONS[selected]);
    }
    settings_show_bms_detail();
    settings_navigation_set_hidden(false, false);
    lv_indev_wait_release(lv_indev_active());
}

static void settings_bms_bind_confirm_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    settings_bms_popup_close();
    settings_show_bms_ble_popup((settings_ble_source_t)s_ui.settings_ble_source, false);
}

static void settings_bms_bind_confirm_accept_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        s_ui.settings_bms_confirm_mac[0] == '\0') {
        return;
    }

    char mac[sizeof(s_ui.settings_bms_confirm_mac)] = { 0 };
    (void)snprintf(mac, sizeof(mac), "%s", s_ui.settings_bms_confirm_mac);
    const settings_ble_source_t source = (settings_ble_source_t)s_ui.settings_ble_source;
    lv_indev_wait_release(lv_indev_active());
    settings_bms_popup_close();
    if (source == SETTINGS_BLE_SOURCE_BMS) {
        queue_bms_bind_action(mac);
        settings_show_bms_detail();
    } else {
        queue_controller_bind_action(mac);
        settings_show_controller_detail();
    }
    settings_navigation_set_hidden(false, false);
    quick_toast_show_connecting();
    ESP_LOGI(TAG,
             "[ble-ui] %s bind confirmed: mac=%s",
             source == SETTINGS_BLE_SOURCE_BMS ? "BMS" : "controller",
             mac);
}

static void settings_show_bms_bind_confirm(const esp_bms_bms_scan_candidate_t *candidate)
{
    if (!candidate || candidate->mac[0] == '\0') {
        return;
    }

    settings_bms_popup_close();
    (void)snprintf(s_ui.settings_bms_confirm_mac,
                   sizeof(s_ui.settings_bms_confirm_mac),
                   "%s",
                   candidate->mac);
    (void)snprintf(s_ui.settings_bms_confirm_name,
                   sizeof(s_ui.settings_bms_confirm_name),
                   "%s",
                   candidate->has_name && candidate->name[0] != '\0' ? candidate->name : "设备");

    UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
    UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, false);
    s_ui.settings_bms_popup = lv_obj_create(lv_layer_top());
    clear_style(s_ui.settings_bms_popup);
    lv_obj_set_pos(s_ui.settings_bms_popup, 0, 0);
    lv_obj_set_size(s_ui.settings_bms_popup, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(s_ui.settings_bms_popup, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_bms_popup, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_bms_popup, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_ui.settings_bms_popup, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t dialog_w = clamp_i32(s_ui.width - 32, 200, 280);
    const int32_t dialog_h = 132;
    lv_obj_t *dialog = panel(s_ui.settings_bms_popup,
                             (s_ui.width - dialog_w) / 2,
                             (s_ui.height - dialog_h) / 2,
                             dialog_w,
                             dialog_h,
                             COLOR_PANEL_ALT);
    lv_obj_set_style_radius(dialog, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 0, LV_PART_MAIN);

    lv_obj_t *title = label(dialog, 12, 10, dialog_w - 24, 20, &settings_zh_16);
    lv_label_set_text(title, "蓝牙连接");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_obj_t *name = label(dialog, 12, 40, dialog_w - 24, 22, &settings_zh_13);
    lv_label_set_text_fmt(name, "连接 %s ?", s_ui.settings_bms_confirm_name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    const int32_t button_gap = 12;
    const int32_t button_w = (dialog_w - 36 - button_gap) / 2;
    const int32_t button_y = 78;
    const int32_t button_h = 40;
    lv_obj_t *cancel = panel(dialog, 12, button_y, button_w, button_h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(cancel, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, settings_bms_bind_confirm_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_icon = label(cancel, 0, 8, button_w, 24, &lv_font_montserrat_24);
    lv_label_set_text(cancel_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_align(cancel_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_icon, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    lv_obj_t *confirm = panel(dialog,
                             12 + button_w + button_gap,
                             button_y,
                             button_w,
                             button_h,
                             COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(confirm, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(confirm, 0, LV_PART_MAIN);
    lv_obj_add_flag(confirm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confirm, settings_bms_bind_confirm_accept_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_icon = label(confirm, 0, 8, button_w, 24, &lv_font_montserrat_24);
    lv_label_set_text(confirm_icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_align(confirm_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(confirm_icon, COLOR_WHITE, LV_PART_MAIN);
}

static void settings_bms_ble_candidate_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }

    const esp_bms_bms_scan_candidate_t *candidate =
        (const esp_bms_bms_scan_candidate_t *)lv_event_get_user_data(event);
    if (!candidate || candidate->mac[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG,
             "[ble-ui] %s candidate confirmation opened: mac=%s",
             s_ui.settings_ble_source == (uint8_t)SETTINGS_BLE_SOURCE_BMS ? "BMS"
                                                                          : "controller",
             candidate->mac);
    settings_show_bms_bind_confirm(candidate);
}

static void settings_bms_ble_refresh_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }
    settings_bms_ble_start_scan();
}

static const settings_detail_row_t *settings_detail_rows_for_id(settings_detail_id_t detail_id,
                                                                size_t *count)
{
    if (count) {
        *count = 0;
    }
    switch (detail_id) {
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
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_SETTINGS_LIST);
    lv_obj_set_style_radius(box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(box);
    if (row && (row->action != ESP_BMS_LVGL_ACTION_NONE ||
                row->system_view != SETTINGS_SYSTEM_VIEW_ROOT)) {
        const uintptr_t route = (uintptr_t)row->action | ((uintptr_t)row->system_view << 8);
        lv_obj_add_event_cb(box, settings_detail_action_event_cb, LV_EVENT_CLICKED,
                            (void *)route);
    }

    const bool has_action = row && (row->action != ESP_BMS_LVGL_ACTION_NONE ||
                                    row->system_view != SETTINGS_SYSTEM_VIEW_ROOT);
    const bool has_switch = has_action && settings_detail_action_uses_switch(row->action);
    const bool has_subtitle = row && row->subtitle && row->subtitle[0] != '\0';
    const lv_font_t *title_font = &settings_zh_13;
    const lv_font_t *subtitle_font = &settings_zh_10;
    const int32_t title_h = (int32_t)title_font->line_height + 4;
    const int32_t subtitle_h = (int32_t)subtitle_font->line_height + 4;
    const int32_t text_gap = has_subtitle ? 1 : 0;
    const int32_t total_text_h = title_h + (has_subtitle ? text_gap + subtitle_h : 0);
    const int32_t text_y = total_text_h < h ? (h - total_text_h) / 2 : 0;
    const int32_t action_w = has_action ? (has_switch ? 54 : 42) : 24;
    const int32_t text_w = w - 12 - action_w;

    lv_obj_t *title = label(box, 12, text_y, text_w, title_h, title_font);
    lv_label_set_text(title, row ? row->title : "");
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    if (has_subtitle) {
        const char *subtitle_text =
            row->system_view == SETTINGS_SYSTEM_VIEW_LEVEL_POSITION ? quick_level_position_text() : row->subtitle;
        lv_obj_t *subtitle = label(box,
                                   12,
                                   text_y + title_h + text_gap,
                                   text_w,
                                   subtitle_h,
                                   subtitle_font);
        lv_label_set_text(subtitle, subtitle_text);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    }

    if (has_switch) {
        const int32_t switch_w = 34;
        const int32_t switch_h = 18;
        const int32_t switch_slot_w = 54;
        settings_detail_switch(box,
                               w - switch_slot_w + ((switch_slot_w - switch_w) / 2),
                               (h - switch_h) / 2,
                               settings_detail_action_switch_on(row->action));
    } else if (has_action) {
        lv_obj_t *arrow = label(box, w - 24, 0, 14, 15, &settings_zh_13);
        lv_label_set_text(arrow, ">");
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    }
    return box;
}

static void settings_system_slider_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED) {
        return;
    }

    lv_point_t point = { 0 };
    if (!get_active_pointer(&point) || !s_ui.settings_system_slider) {
        return;
    }
    lv_area_t coordinates;
    lv_obj_get_coords(s_ui.settings_system_slider, &coordinates);
    const quick_level_kind_t kind =
        (quick_level_kind_t)s_ui.settings_system_slider_kind;
    const int32_t minimum = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const int32_t maximum = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t width = coordinates.x2 - coordinates.x1 + 1;
    const int32_t raw_value = minimum +
                              ((point.x - coordinates.x1) * (maximum - minimum)) /
                                  (width > 0 ? width : 1);
    const uint8_t value = quick_level_snap_drag_value(kind, raw_value);
    const int32_t fill_w = ((value - minimum) * lv_obj_get_width(s_ui.settings_system_slider)) /
                           (maximum - minimum);
    lv_obj_set_width(s_ui.settings_system_slider_fill, fill_w);
    lv_obj_set_x(s_ui.settings_system_slider_knob,
                 clamp_i32(fill_w - 10, 0, lv_obj_get_width(s_ui.settings_system_slider) - 20));
    lv_label_set_text_fmt(s_ui.settings_system_value, "%u%%", (unsigned)value);
    quick_level_queue_value(kind, value, code == LV_EVENT_RELEASED);
    if (code == LV_EVENT_RELEASED) {
        lv_indev_wait_release(lv_indev_active());
    }
}

static void settings_show_system_slider(quick_level_kind_t kind)
{
    const int32_t page_w = s_ui.width - 24;
    const int32_t card_h = s_ui.width < s_ui.height ? 150 : 130;
    lv_obj_t *card = panel(s_ui.settings_detail, 12, 16, page_w, card_h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);

    const char *title_text = kind == QUICK_LEVEL_VOLUME ? "提示音量" : "屏幕亮度";
    lv_obj_t *title = label(card, 12, 14, page_w - 24, 22, &settings_zh_16);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    s_ui.settings_system_value = label(card, 12, 44, page_w - 24, 28, &lv_font_montserrat_24);
    lv_label_set_text_fmt(s_ui.settings_system_value,
                          "%u%%",
                          (unsigned)quick_level_current_value(kind));
    lv_obj_set_style_text_align(s_ui.settings_system_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_system_value, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);

    const int32_t slider_w = page_w - 36;
    const uint8_t value = quick_level_current_value(kind);
    const int32_t minimum = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MIN : QUICK_BRIGHTNESS_MIN;
    const int32_t maximum = kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_MAX : QUICK_BRIGHTNESS_MAX;
    const int32_t fill_w = ((value - minimum) * slider_w) / (maximum - minimum);
    s_ui.settings_system_slider_kind = (uint8_t)kind;
    s_ui.settings_system_slider = panel(card, 18, card_h - 50, slider_w, 28, COLOR_SETTINGS_BORDER);
    lv_obj_set_style_radius(s_ui.settings_system_slider, 14, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_system_slider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ui.settings_system_slider, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.settings_system_slider_fill = panel(s_ui.settings_system_slider,
                                             0,
                                             0,
                                             fill_w,
                                             28,
                                             COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(s_ui.settings_system_slider_fill, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.settings_system_slider_fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_ui.settings_system_slider_knob = panel(s_ui.settings_system_slider,
                                             clamp_i32(fill_w - 10, 0, slider_w - 20),
                                             4,
                                             20,
                                             20,
                                             COLOR_WHITE);
    lv_obj_set_style_radius(s_ui.settings_system_slider_knob, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.settings_system_slider_knob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.settings_system_slider, settings_system_slider_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.settings_system_slider, settings_system_slider_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.settings_system_slider, settings_system_slider_event_cb, LV_EVENT_RELEASED, NULL);
}

static void settings_system_position_option_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    s_ui.quick_level_position =
        (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    refresh_quick_level_layouts();
    settings_show_system_view(SETTINGS_SYSTEM_VIEW_LEVEL_POSITION);
}

static void settings_show_system_position(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const char *labels[QUICK_LEVEL_POSITION_COUNT] = {
        "中间",
        portrait ? "右边" : "下面",
        portrait ? "左边" : "上面",
    };
    lv_obj_t *description = label(s_ui.settings_detail, 12, 18, s_ui.width - 24, 24, &settings_zh_13);
    lv_label_set_text(description, "选择快捷调节条出现的位置");
    lv_label_set_long_mode(description, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(description, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    const int32_t gap = 8;
    const int32_t x = 12;
    const int32_t button_w = (s_ui.width - 24 - (gap * 2)) / 3;
    for (uint8_t index = 0; index < QUICK_LEVEL_POSITION_COUNT; ++index) {
        lv_obj_t *button = panel(s_ui.settings_detail,
                                 x + ((button_w + gap) * index),
                                 64,
                                 button_w,
                                 56,
                                 index == s_ui.quick_level_position ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_CARD);
        lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button,
                                      index == s_ui.quick_level_position ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                      LV_PART_MAIN);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button,
                            settings_system_position_option_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        lv_obj_t *button_label = label(button, 2, 17, button_w - 4, 22, &settings_zh_13);
        lv_label_set_text(button_label, labels[index]);
        lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(button_label, COLOR_SETTINGS_TEXT, LV_PART_MAIN);
    }
}

static void settings_calibration_target_place(void)
{
    if (!s_ui.settings_calibration_target || s_ui.settings_calibration_target_index >= 4U) {
        return;
    }
    const int32_t margin = 52;
    const int32_t target_x[4] = { margin, s_ui.width - margin, s_ui.width - margin, margin };
    const int32_t content_h = s_ui.height - SETTINGS_DETAIL_HEADER_H;
    const int32_t target_y[4] = { margin, margin, content_h - margin, content_h - margin };
    const uint8_t index = s_ui.settings_calibration_target_index;
    lv_obj_set_pos(s_ui.settings_calibration_target, target_x[index] - 18, target_y[index] - 18);
    set_obj_hidden(s_ui.settings_calibration_target, false);
    lv_obj_update_layout(s_ui.settings_calibration_target);
    lv_area_t coordinates;
    lv_obj_get_coords(s_ui.settings_calibration_target, &coordinates);
    s_ui.settings_calibration_expected.x = (coordinates.x1 + coordinates.x2) / 2;
    s_ui.settings_calibration_expected.y = (coordinates.y1 + coordinates.y2) / 2;
    if (s_ui.settings_calibration_status) {
        lv_label_set_text_fmt(s_ui.settings_calibration_status,
                              "点击十字中心 %u/4",
                              (unsigned)index + 1U);
    }
}

static void settings_calibration_start_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui.settings_system_view == (uint8_t)SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION) {
        settings_calibration_target_place();
    }
}

static void settings_calibration_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        (void)get_active_pointer(&s_ui.settings_calibration_observed);
        return;
    }
    if (code != LV_EVENT_RELEASED || s_ui.settings_calibration_target_index >= 4U) {
        return;
    }

    queue_touch_calibration_sample(s_ui.settings_calibration_target_index,
                                   &s_ui.settings_calibration_observed,
                                   &s_ui.settings_calibration_expected);
    s_ui.settings_calibration_target_index++;
    if (s_ui.settings_calibration_target_index < 4U) {
        settings_calibration_target_place();
    } else {
        set_obj_hidden(s_ui.settings_calibration_target, true);
        label_set_text_if_changed(s_ui.settings_calibration_status, "正在保存校准...");
    }
}

static void settings_calibration_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    queue_action(ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION);
    settings_show_detail(SETTINGS_DETAIL_SYSTEM);
    lv_indev_wait_release(lv_indev_active());
}

static void settings_show_touch_calibration(void)
{
    s_ui.settings_calibration_target_index = 0;
    queue_action(ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION);

    lv_obj_t *layer = lv_obj_create(s_ui.settings_detail);
    clear_style(layer);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_size(layer, s_ui.width, s_ui.height - SETTINGS_DETAIL_HEADER_H);
    lv_obj_set_style_bg_color(layer, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(layer, settings_calibration_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(layer, settings_calibration_event_cb, LV_EVENT_RELEASED, NULL);

    s_ui.settings_calibration_status = label(layer, 42, 4, s_ui.width - 84, 24, &settings_zh_13);
    lv_label_set_text(s_ui.settings_calibration_status, "准备校准...");
    lv_label_set_long_mode(s_ui.settings_calibration_status, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_ui.settings_calibration_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_calibration_status, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    s_ui.settings_calibration_target = lv_obj_create(layer);
    clear_style(s_ui.settings_calibration_target);
    lv_obj_set_size(s_ui.settings_calibration_target, 36, 36);
    lv_obj_set_style_radius(s_ui.settings_calibration_target, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.settings_calibration_target, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.settings_calibration_target, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_calibration_target, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.settings_calibration_target, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *horizontal = panel(s_ui.settings_calibration_target, 5, 17, 26, 2, COLOR_SWITCH_ACTIVE);
    lv_obj_clear_flag(horizontal, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vertical = panel(s_ui.settings_calibration_target, 17, 5, 2, 26, COLOR_SWITCH_ACTIVE);
    lv_obj_clear_flag(vertical, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    set_obj_hidden(s_ui.settings_calibration_target, true);

    lv_obj_t *cancel = panel(layer, s_ui.width - 58, 4, 50, 28, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(cancel, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, settings_calibration_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = label(cancel, 2, 5, 46, 18, &settings_zh_13);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_align(cancel_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_label, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_timer_t *timer = lv_timer_create(settings_calibration_start_timer_cb, 100, NULL);
    if (timer) {
        lv_timer_set_repeat_count(timer, 1);
    }
}

static void settings_show_system_view(settings_system_view_t view)
{
    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_SYSTEM;
    s_ui.settings_system_view = (uint8_t)view;
    s_ui.settings_system_value = NULL;
    s_ui.settings_system_slider = NULL;
    s_ui.settings_system_slider_fill = NULL;
    s_ui.settings_system_slider_knob = NULL;
    s_ui.settings_calibration_target = NULL;
    s_ui.settings_calibration_status = NULL;
    set_obj_hidden(s_ui.settings_root, true);
    set_obj_hidden(s_ui.settings_detail, false);
    settings_detail_chrome_show(SETTINGS_DETAIL_SYSTEM);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    switch (view) {
    case SETTINGS_SYSTEM_VIEW_BRIGHTNESS:
        label_set_text_if_changed(s_ui.settings_detail_title, "亮度");
        settings_show_system_slider(QUICK_LEVEL_BRIGHTNESS);
        break;
    case SETTINGS_SYSTEM_VIEW_VOLUME:
        label_set_text_if_changed(s_ui.settings_detail_title, "音量");
        settings_show_system_slider(QUICK_LEVEL_VOLUME);
        break;
    case SETTINGS_SYSTEM_VIEW_LEVEL_POSITION:
        label_set_text_if_changed(s_ui.settings_detail_title, "调节条位置");
        settings_show_system_position();
        break;
    case SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION:
        label_set_text_if_changed(s_ui.settings_detail_title, "屏幕校准");
        settings_show_touch_calibration();
        break;
    case SETTINGS_SYSTEM_VIEW_ROOT:
    default:
        settings_show_detail(SETTINGS_DETAIL_SYSTEM);
        break;
    }
}

static void settings_show_detail(settings_detail_id_t detail_id)
{
    if (!s_ui.settings_detail) {
        return;
    }

    settings_bms_popup_close();
    if (detail_id == SETTINGS_DETAIL_HOTSPOT &&
        s_ui.setup_ap_info &&
        lv_obj_get_child_count(s_ui.settings_detail) > 0U) {
        s_ui.settings_detail_id = (uint8_t)detail_id;
        set_obj_hidden(s_ui.settings_root, true);
        set_obj_hidden(s_ui.settings_detail, false);
        settings_detail_chrome_show(detail_id);
        lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
        set_setup_ap(settings_current_snapshot());
        return;
    }

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)detail_id;
    s_ui.settings_system_view = (uint8_t)SETTINGS_SYSTEM_VIEW_ROOT;
    set_obj_hidden(s_ui.settings_root, true);
    set_obj_hidden(s_ui.settings_detail, false);
    settings_detail_chrome_show(detail_id);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
    s_ui.setup_ap_control_row = NULL;
    s_ui.setup_ap_info = NULL;
    s_ui.setup_ap_qr_panel = NULL;
    s_ui.setup_ap_qr = NULL;
    s_ui.setup_ap_qr_ready = false;
    s_ui.setup_ap_qr_encode_attempted = false;
    s_ui.settings_bms_ble_status = NULL;
    s_ui.settings_bms_ble_popup_open = false;
    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_ROOT;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    memset(s_ui.settings_controller_tire_rollers,
           0,
           sizeof(s_ui.settings_controller_tire_rollers));
    s_ui.settings_controller_ratio_roller = NULL;
    s_ui.settings_system_value = NULL;
    s_ui.settings_system_slider = NULL;
    s_ui.settings_system_slider_fill = NULL;
    s_ui.settings_system_slider_knob = NULL;
    s_ui.settings_calibration_target = NULL;
    s_ui.settings_calibration_status = NULL;

    if (detail_id == SETTINGS_DETAIL_HOTSPOT) {
        settings_show_hotspot_detail();
        return;
    }
    if (detail_id == SETTINGS_DETAIL_BLUETOOTH) {
        settings_show_bluetooth_detail();
        return;
    }
    if (detail_id == SETTINGS_DETAIL_BMS) {
        settings_show_bms_detail();
        return;
    }
    if (detail_id == SETTINGS_DETAIL_CONTROLLER) {
        settings_show_controller_detail();
        return;
    }

    const int32_t card_x = 12;
    const int32_t card_w = s_ui.width - 24;
    const int32_t row_h = s_ui.width < s_ui.height ? 56 : 48;
    const int32_t first_y = 12;

    size_t row_count = 0;
    const settings_detail_row_t *rows = settings_detail_rows_for_id(detail_id, &row_count);
    lv_obj_t *list_card = rows ? settings_list_card(s_ui.settings_detail,
                                                    card_x,
                                                    first_y,
                                                    card_w,
                                                    row_h,
                                                    row_count) : NULL;
    for (size_t index = 0; rows && index < row_count; ++index) {
        settings_detail_row(list_card,
                            0,
                            (int32_t)index * row_h,
                            card_w,
                            row_h,
                            &rows[index]);
    }
}

static void settings_option_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
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
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_SETTINGS_LIST);
    lv_obj_set_style_radius(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(box);
    lv_obj_add_event_cb(box, settings_option_event_cb, LV_EVENT_CLICKED,
                        option ? (void *)(uintptr_t)option->detail_id : NULL);

    const int32_t text_x = 12;
    const lv_font_t *title_font = &settings_zh_13;
    const lv_font_t *subtitle_font = &settings_zh_10;
    const int32_t title_h = (int32_t)title_font->line_height + 2;
    const int32_t subtitle_h = (int32_t)subtitle_font->line_height + 6;
    const char *subtitle_text = option ? option->subtitle : "";
    const bool show_subtitle = h >= 42 && subtitle_text[0] != '\0';
    const int32_t text_gap = show_subtitle ? 1 : 0;
    const int32_t total_text_h = title_h + (show_subtitle ? text_gap + subtitle_h : 0);
    const int32_t title_y = total_text_h < h ? (h - total_text_h) / 2 : 0;
    lv_obj_t *title = label(box, text_x, title_y, w - text_x - 30, title_h, title_font);
    lv_label_set_text(title, option ? option->title : "");
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    if (show_subtitle) {
        lv_obj_t *subtitle = label(box,
                                   text_x,
                                   title_y + title_h + text_gap,
                                   w - text_x - 30,
                                   subtitle_h,
                                   subtitle_font);
        lv_label_set_text(subtitle, subtitle_text);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    }

    lv_obj_t *arrow = label(box,
                            w - 22,
                            0,
                            14,
                            16,
                            &settings_zh_16);
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -8, 0);
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

static lv_obj_t *quick_lock_icon(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *icon = lv_obj_create(parent);
    clear_style(icon);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    quick_lock_icon_recenter(icon, w - 8, h - 8);

    lv_obj_t *shackle = panel(icon, 6, 1, 12, 16, COLOR_TEXT);
    lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(shackle, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(shackle, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_side(shackle,
                                 LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT,
                                 LV_PART_MAIN);
    lv_obj_set_style_radius(shackle, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(shackle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(shackle, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *body = panel(icon, 2, 12, 20, 14, COLOR_TEXT);
    lv_obj_set_style_radius(body, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
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
    } else if (item->kind == QUICK_ITEM_LOCK) {
        s_ui.quick_panel_item_icons[index] = quick_lock_icon(box, w, h);
    } else {
        s_ui.quick_panel_item_icons[index] = quick_symbol_icon(box,
                                                               content_w,
                                                               content_h,
                                                               item->icon,
                                                               quick_panel_item_icon_font(item));
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
        return SNAPSHOT_FLAG(snapshot, BLUETOOTH_ADVERTISING);
    case QUICK_ITEM_HOTSPOT:
        return SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) || snapshot->wifi == ESP_BMS_WIFI_SETUP_AP;
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
        const bool active = ui_flag_get(s_ui.quick_panel_item_local_override_flags, index) ?
                                ui_flag_get(s_ui.quick_panel_item_local_active_flags, index) :
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
    return UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE) &&
           s_ui.quick_level_overlay_kind == (uint8_t)kind;
}

static void quick_level_save_timer_cancel(void)
{
    if (s_ui.quick_level_save_timer) {
        lv_timer_delete(s_ui.quick_level_save_timer);
        s_ui.quick_level_save_timer = NULL;
    }
}

static void quick_level_commit_current(void)
{
    if (!UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE)) {
        return;
    }

    const quick_level_kind_t kind = (quick_level_kind_t)s_ui.quick_level_overlay_kind;
    quick_level_queue_value(kind, quick_level_current_value(kind), true);
}

static void quick_level_save_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_ui.quick_level_save_timer = NULL;
    quick_level_commit_current();
    quick_level_overlay_hide();
}

static void quick_level_save_timer_restart(void)
{
    quick_level_save_timer_cancel();
    s_ui.quick_level_save_timer = lv_timer_create(quick_level_save_timer_cb,
                                                  QUICK_LEVEL_SAVE_DELAY_MS,
                                                  NULL);
    if (s_ui.quick_level_save_timer) {
        lv_timer_set_repeat_count(s_ui.quick_level_save_timer, 1);
    }
}

static void quick_level_overlay_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, LV_PART_MAIN);
}

static void quick_level_overlay_hide_completed_cb(lv_anim_t *anim)
{
    (void)anim;
    if (s_ui.quick_level_overlay) {
        set_obj_hidden(s_ui.quick_level_overlay, true);
        lv_obj_set_style_opa(s_ui.quick_level_overlay, LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void quick_level_overlay_fade(lv_opa_t from, lv_opa_t to, lv_anim_completed_cb_t completed_cb)
{
    if (!s_ui.quick_level_overlay) {
        return;
    }

    lv_anim_delete(s_ui.quick_level_overlay, quick_level_overlay_opa_anim_cb);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_ui.quick_level_overlay);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, QUICK_LEVEL_OVERLAY_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, quick_level_overlay_opa_anim_cb);
    lv_anim_set_completed_cb(&anim, completed_cb);
    lv_anim_start(&anim);
}

static bool quick_level_pointer_on_track(void)
{
    lv_point_t point = { 0 };
    lv_obj_t *track = s_ui.quick_level_overlay_track;
    if (!track || !get_active_pointer(&point)) {
        return false;
    }

    lv_area_t area = { 0 };
    lv_obj_get_coords(track, &area);
    const int32_t margin = 28;
    return point.x >= area.x1 - margin &&
           point.x <= area.x2 + margin &&
           point.y >= area.y1 - margin &&
           point.y <= area.y2 + margin;
}

static void quick_level_overlay_layout(void)
{
    if (!s_ui.quick_level_overlay) {
        return;
    }

    lv_obj_set_pos(s_ui.quick_level_overlay, 0, 0);
    lv_obj_set_size(s_ui.quick_level_overlay, s_ui.width, s_ui.height);
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_HORIZONTAL, s_ui.width > s_ui.height);

    const bool horizontal = UI_FLAG(QUICK_LEVEL_OVERLAY_HORIZONTAL);
    const int32_t track_w = horizontal ? clamp_i32((s_ui.width * 2) / 3, 120, s_ui.width - 48) : 22;
    const int32_t track_h = horizontal ? 22 : clamp_i32((s_ui.height * 2) / 3, 112, s_ui.height - 64);
    int32_t track_x = (s_ui.width - track_w) / 2;
    int32_t track_y = (s_ui.height - track_h) / 2;

    if (horizontal) {
        const int32_t top_y = 36;
        const int32_t bottom_y = s_ui.height - track_h - 28;
        if (quick_level_position() == QUICK_LEVEL_POSITION_START) {
            track_y = top_y;
        } else if (quick_level_position() == QUICK_LEVEL_POSITION_END) {
            track_y = bottom_y;
        }
        track_y = clamp_i32(track_y, 4, s_ui.height - track_h - 4);
    } else {
        const int32_t left_x = 28;
        const int32_t right_x = s_ui.width - track_w - 28;
        if (quick_level_position() == QUICK_LEVEL_POSITION_START) {
            track_x = left_x;
        } else if (quick_level_position() == QUICK_LEVEL_POSITION_END) {
            track_x = right_x;
        }
        track_x = clamp_i32(track_x, 4, s_ui.width - track_w - 4);
    }

    if (s_ui.quick_level_overlay_value) {
        const int32_t value_y = horizontal ? (track_y > 34 ? track_y - 34 : track_y + track_h + 8) : 8;
        lv_obj_set_pos(s_ui.quick_level_overlay_value, 0, value_y);
        lv_obj_set_size(s_ui.quick_level_overlay_value, s_ui.width, 26);
    }
    if (s_ui.quick_level_overlay_track) {
        lv_obj_set_pos(s_ui.quick_level_overlay_track, track_x, track_y);
        lv_obj_set_size(s_ui.quick_level_overlay_track, track_w, track_h);
    }
}

static void quick_level_overlay_show(quick_level_kind_t kind)
{
    s_ui.quick_level_overlay_kind = (uint8_t)kind;
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE, true);
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_DRAGGED, false);
    UI_SET_FLAG(QUICK_LEVEL_POINTER_ACTIVE, false);
    UI_SET_FLAG(RETURN_SWIPE_TRACKING, false);
    s_ui.return_swipe_drag_dy = 0;
    UI_SET_FLAG(RETURN_SWIPE_CANCELLED, false);
    UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, false);
    if (s_ui.quick_level_overlay) {
        quick_level_overlay_layout();
        lv_obj_set_style_opa(s_ui.quick_level_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
        set_obj_hidden(s_ui.quick_level_overlay, false);
        lv_obj_move_foreground(s_ui.quick_level_overlay);
        quick_level_overlay_fade(LV_OPA_TRANSP, LV_OPA_COVER, NULL);
    }
    quick_level_overlay_update(kind, quick_level_current_value(kind));
    quick_level_save_timer_restart();
}

static void quick_level_overlay_hide(void)
{
    quick_level_save_timer_cancel();
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE, false);
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_DRAGGED, false);
    UI_SET_FLAG(QUICK_LEVEL_POINTER_ACTIVE, false);
    UI_SET_FLAG(QUICK_PANEL_INTERACTIVE, UI_FLAG(QUICK_PANEL_OPEN) && !UI_FLAG(QUICK_PANEL_SETTLING));
    if (s_ui.quick_level_overlay) {
        quick_level_overlay_fade(lv_obj_get_style_opa(s_ui.quick_level_overlay, LV_PART_MAIN),
                                 LV_OPA_TRANSP,
                                 quick_level_overlay_hide_completed_cb);
    }
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
    if (UI_FLAG(QUICK_LEVEL_OVERLAY_HORIZONTAL)) {
        const int32_t track_w = area.x2 - area.x1 + 1;
        const int32_t offset = point.x - area.x1;
        percent = min_value + ((offset * range) / (track_w > 1 ? track_w - 1 : 1));
    } else {
        const int32_t track_h = area.y2 - area.y1 + 1;
        const int32_t offset = area.y2 - point.y;
        percent = min_value + ((offset * range) / (track_h > 1 ? track_h - 1 : 1));
    }
    const uint8_t snapped = quick_level_snap_drag_value(kind, percent);
    UI_SET_FLAG(QUICK_LEVEL_OVERLAY_DRAGGED, true);

    if (kind == QUICK_LEVEL_VOLUME) {
        set_quick_volume_value(snapped, true, committed);
    } else {
        set_quick_brightness_value(snapped, true, committed);
    }
    if (!committed) {
        quick_level_save_timer_restart();
    }
    return true;
}

static void quick_level_overlay_event_cb(lv_event_t *event)
{
    if (!UI_FLAG(QUICK_LEVEL_OVERLAY_ACTIVE)) {
        return;
    }

    const quick_level_kind_t kind = (quick_level_kind_t)s_ui.quick_level_overlay_kind;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        const bool on_track = quick_level_pointer_on_track();
        UI_SET_FLAG(QUICK_LEVEL_POINTER_ACTIVE, on_track);
        if (on_track) {
            (void)quick_level_set_from_pointer(kind, false);
        } else {
            quick_level_commit_current();
            quick_level_overlay_hide();
        }
    } else if (code == LV_EVENT_PRESSING && UI_FLAG(QUICK_LEVEL_POINTER_ACTIVE)) {
        (void)quick_level_set_from_pointer(kind, false);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (code == LV_EVENT_RELEASED && UI_FLAG(QUICK_LEVEL_POINTER_ACTIVE)) {
            (void)quick_level_set_from_pointer(kind, false);
        }
        UI_SET_FLAG(QUICK_LEVEL_POINTER_ACTIVE, false);
        quick_level_set_pressed(kind, false);
        quick_tile_set_scale(quick_level_tile_for_kind(kind), QUICK_TILE_SCALE_NORMAL);
        UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, false);
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

    const bool overlay_for_this_tile = quick_level_overlay_matches(kind);
    if (overlay_for_this_tile) {
        return;
    }

    if (!UI_FLAG(QUICK_PANEL_INTERACTIVE)) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_level_set_pressed(kind, false);
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
        }
        return;
    }

    if (!overlay_for_this_tile && process_return_swipe_event(code, false)) {
        return;
    }

    if (UI_FLAG(QUICK_EDIT_MODE)) {
        if (code == LV_EVENT_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_PRESSED);
        } else if (code == LV_EVENT_PRESSING) {
            quick_drag_update();
        } else if (code == LV_EVENT_LONG_PRESSED) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_LONG);
            quick_drag_begin(tile,
                             kind == QUICK_LEVEL_VOLUME ? QUICK_DRAG_TARGET_VOLUME :
                                                          QUICK_DRAG_TARGET_BRIGHTNESS,
                             0);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            quick_tile_set_scale(tile, QUICK_TILE_SCALE_NORMAL);
            (void)quick_drag_end();
        }
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, false);
        quick_level_set_pressed(kind, true);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, true);
        quick_toast_show_text(kind == QUICK_LEVEL_VOLUME ? QUICK_VOLUME_TOAST_HINT :
                                                               QUICK_BRIGHTNESS_TOAST_HINT);
        lv_indev_wait_release(lv_indev_active());
    } else if (code == LV_EVENT_PRESS_LOST) {
        quick_level_set_pressed(kind, false);
        UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, false);
    } else if (code == LV_EVENT_RELEASED) {
        quick_level_set_pressed(kind, false);
    } else if (code == LV_EVENT_CLICKED) {
        if (UI_FLAG(QUICK_LEVEL_LONG_TRIGGERED)) {
            UI_SET_FLAG(QUICK_LEVEL_LONG_TRIGGERED, false);
            return;
        }
        quick_toast_cancel();
        set_obj_hidden(s_ui.quick_toast, true);
        show_dashboard_view();
        quick_level_overlay_show(kind);
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
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_ui.quick_level_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay, quick_level_overlay_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_flag(s_ui.quick_level_overlay, LV_OBJ_FLAG_HIDDEN);

    s_ui.quick_level_overlay_value = label(s_ui.quick_level_overlay,
                                           0,
                                           0,
                                           s_ui.width,
                                           26,
                                           &lv_font_montserrat_24);
    lv_obj_set_style_text_align(s_ui.quick_level_overlay_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.quick_level_overlay_value, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay_value, LV_OBJ_FLAG_HIDDEN);

    s_ui.quick_level_overlay_track = lv_obj_create(s_ui.quick_level_overlay);
    clear_style(s_ui.quick_level_overlay_track);
    lv_obj_set_style_radius(s_ui.quick_level_overlay_track, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay_track, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_track, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_ui.quick_level_overlay_track, false, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_track, quick_level_overlay_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_track, quick_level_overlay_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_track, quick_level_overlay_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_track, quick_level_overlay_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_ui.quick_level_overlay_fill = lv_obj_create(s_ui.quick_level_overlay_track);
    clear_style(s_ui.quick_level_overlay_fill);
    lv_obj_set_style_radius(s_ui.quick_level_overlay_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.quick_level_overlay_fill, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay_fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_fill, quick_level_overlay_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_fill, quick_level_overlay_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_fill, quick_level_overlay_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_level_overlay_fill, quick_level_overlay_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_ui.quick_level_overlay_knob = lv_obj_create(s_ui.quick_level_overlay);
    clear_style(s_ui.quick_level_overlay_knob);
    lv_obj_set_size(s_ui.quick_level_overlay_knob, 1, 1);
    lv_obj_set_style_bg_opa(s_ui.quick_level_overlay_knob, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_level_overlay_knob, LV_OBJ_FLAG_HIDDEN);
    quick_level_overlay_layout();
}

static void quick_rotate_toast_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        !UI_FLAG(QUICK_ROTATE_TOAST_ACTIVE)) {
        return;
    }

    lv_indev_wait_release(lv_indev_active());
    perform_ui_action(ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY, false);
}

static void quick_toast_create(lv_obj_t *parent)
{
    s_ui.quick_toast = lv_obj_create(parent);
    clear_style(s_ui.quick_toast);
    lv_obj_set_size(s_ui.quick_toast, 1, 1);
    lv_obj_set_style_bg_color(s_ui.quick_toast, COLOR_PANEL_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.quick_toast, LV_OPA_COVER, LV_PART_MAIN);

    s_ui.quick_toast_text = label(s_ui.quick_toast, 0, 0, 1, 1, &settings_zh_16);
    s_ui.quick_toast_rotate_title = label(s_ui.quick_toast, 0, 0, 1, 1, &settings_zh_13);
    s_ui.quick_toast_rotate_icon = label(s_ui.quick_toast, 0, 0, 1, 1, &lv_font_montserrat_24);
    s_ui.quick_toast_rotate_countdown = label(s_ui.quick_toast, 0, 0, 1, 1, &lv_font_montserrat_14);
    quick_toast_apply_normal_style();
    lv_obj_add_event_cb(s_ui.quick_toast, quick_rotate_toast_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ui.quick_toast, LV_OBJ_FLAG_HIDDEN);
}

static void format_mv(char *out, size_t len, bool valid, uint32_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%lu.%02luV", (unsigned long)(mv / 1000), (unsigned long)((mv % 1000) / 10));
}

static void format_deci_amps(char *out, size_t len, bool valid, int32_t deci_amps)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    const char sign = deci_amps < 0 ? '-' : '+';
    const uint32_t abs_value = deci_amps < 0 ? (uint32_t)(-deci_amps) : (uint32_t)deci_amps;
    snprintf(out,
             len,
             "%c%lu.%luA",
             sign,
             (unsigned long)(abs_value / 10U),
             (unsigned long)(abs_value % 10U));
}

static void format_cell_v(char *out, size_t len, bool valid, uint16_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%u.%03uV", mv / 1000, mv % 1000);
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
    const bool gps_fix_valid = SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID);
    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);

    label_set_text_color_if_changed(s_ui.gps_state, gps_fix_valid ? COLOR_ACCENT : COLOR_WARN);
    label_set_text_if_changed(s_ui.gps_state, gps_fix_valid ? "GPS OK" : "GPS --");

    label_set_text_color_if_changed(s_ui.bms_state, bms_online ? COLOR_ACCENT : COLOR_BAD);
    label_set_text_if_changed(s_ui.bms_state, bms_online ? "BMS OK" : "BMS OFF");

    label_set_text_color_if_changed(s_ui.ap_state,
                                    SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? COLOR_ACCENT : COLOR_MUTED);
    label_set_text_if_changed(s_ui.ap_state,
                              SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "AP" : "AP OFF");
}

static void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (s_ui.setup_ap_control_row) {
        const bool enabled = SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED);
        if (lv_obj_get_child_count(s_ui.setup_ap_control_row) > 1U) {
            label_set_text_if_changed(lv_obj_get_child(s_ui.setup_ap_control_row, 1),
                                      enabled ? "热点已打开" : "未打开");
        }
        if (lv_obj_get_child_count(s_ui.setup_ap_control_row) > 2U) {
            lv_obj_t *track = lv_obj_get_child(s_ui.setup_ap_control_row, 2);
            lv_obj_set_style_bg_color(track,
                                      enabled ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(track,
                                          enabled ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                          LV_PART_MAIN);
            if (lv_obj_get_child_count(track) > 0U) {
                lv_obj_t *thumb = lv_obj_get_child(track, 0);
                const int32_t knob = lv_obj_get_width(thumb);
                lv_obj_set_x(thumb, enabled ? (lv_obj_get_width(track) - knob - 2) : 2);
                lv_obj_set_style_bg_color(thumb,
                                          enabled ? COLOR_WHITE : COLOR_SETTINGS_MUTED,
                                          LV_PART_MAIN);
            }
        }
    }

    if (s_ui.setup_ap_info) {
        const char *ssid = snapshot->setup_ap_ssid[0] != '\0' ? snapshot->setup_ap_ssid : "--";
        const char *password = snapshot->setup_ap_password[0] != '\0' ? snapshot->setup_ap_password : "--";
        if (s_ui.width < s_ui.height) {
            label_set_text_fmt_if_changed(s_ui.setup_ap_info, "SETUP %s\nSSID %.31s\nPW %.8s",
                                          SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "ON" : "OFF",
                                          ssid,
                                          password);
        } else {
            label_set_text_fmt_if_changed(s_ui.setup_ap_info, "SETUP %s\nSSID\n%.31s\nPW %.8s",
                                          SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "ON" : "OFF",
                                          ssid,
                                          password);
        }
    }

#if LV_USE_QRCODE
    if (s_ui.setup_ap_qr && s_ui.setup_ap_qr_panel) {
        if (!s_ui.setup_ap_qr_encode_attempted && snapshot->setup_ap_qr_payload[0] != '\0') {
            s_ui.setup_ap_qr_encode_attempted = true;
            s_ui.setup_ap_qr_ready =
                lv_qrcode_update(s_ui.setup_ap_qr,
                                 snapshot->setup_ap_qr_payload,
                                 strlen(snapshot->setup_ap_qr_payload)) == LV_RESULT_OK;
            if (s_ui.setup_ap_qr_ready) {
                ESP_LOGI(TAG, "[setup-qr] encoded fixed payload once");
            } else {
                ESP_LOGW(TAG, "[setup-qr] encode failed; keep panel hidden");
            }
        }
        set_obj_hidden(s_ui.setup_ap_qr_panel,
                       !SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) || !s_ui.setup_ap_qr_ready);
    }
#endif
}

static void set_cast_page(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (s_ui.cast_qr && snapshot->cast_active) {
        set_obj_hidden(s_ui.cast_qr, true);
        return;
    }
    const char *ssid = snapshot->setup_ap_ssid[0] != '\0' ? snapshot->setup_ap_ssid : "--";
    const char *password = snapshot->setup_ap_password[0] != '\0' ? snapshot->setup_ap_password : "--";
#if LV_USE_QRCODE
    if (s_ui.cast_qr) {
        /* Keep room for the full HTTPS landing URL plus the longest SSID and password. */
        char payload[256] = { 0 };
        const int written = snprintf(payload, sizeof(payload),
                                     "https://esp-bms-setting.vercel.app/cast?ssid=%s&password=%s&host=192.168.4.1&v=1",
                                     ssid, password);
        if (written > 0 && (size_t)written < sizeof(payload)) {
            if (lv_qrcode_update(s_ui.cast_qr, payload, (size_t)written) != LV_RESULT_OK) {
                ESP_LOGW(TAG, "[cast-qr] encode failed");
                set_obj_hidden(s_ui.cast_qr, true);
            } else {
                set_obj_hidden(s_ui.cast_qr, false);
            }
        } else {
            set_obj_hidden(s_ui.cast_qr, true);
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

    if (SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        const uint16_t soc = snapshot->soc_percent > 100 ? 100 : snapshot->soc_percent;
        soc_percent = (uint8_t)soc;
        soc_valid = true;
        if (SNAPSHOT_FLAG(snapshot, CAPACITY_REMAINING_VALID) &&
            SNAPSHOT_FLAG(snapshot, TOTAL_CAPACITY_VALID)) {
            (void)snprintf(ah,
                           sizeof(ah),
                           "%lu.%01lu/%lu.%01luAh",
                           (unsigned long)(snapshot->capacity_remaining_mah / 1000U),
                           (unsigned long)((snapshot->capacity_remaining_mah % 1000U) / 100U),
                           (unsigned long)(snapshot->total_capacity_mah / 1000U),
                           (unsigned long)((snapshot->total_capacity_mah % 1000U) / 100U));
        } else {
            (void)snprintf(ah, sizeof(ah), "--/--Ah");
        }
        label_set_text_fmt_if_changed(s_ui.soc, "%u%%", soc);
    } else {
        (void)snprintf(ah, sizeof(ah), "--/--Ah");
        label_set_text_if_changed(s_ui.soc, "--");
    }
    label_set_text_if_changed(s_ui.capacity, ah);

    const bool current_valid = SNAPSHOT_FLAG(snapshot, CURRENT_VALID);
    format_mv(voltage, sizeof(voltage), SNAPSHOT_FLAG(snapshot, PACK_VOLTAGE_VALID), snapshot->pack_voltage_mv);
    const int32_t display_current_deci_amps = -(int32_t)snapshot->current_deci_amps;
    format_deci_amps(current, sizeof(current), current_valid, display_current_deci_amps);
    const bool charging = current_valid && display_current_deci_amps < 0;
    update_dashboard_soc_fill(soc_percent, soc_valid, charging);
    update_dashboard_battery_icon(soc_percent, soc_valid, charging);
    label_set_text_if_changed(s_ui.pack_voltage, voltage);
    label_set_text_if_changed(s_ui.current, current);

    format_cell_v(min_cell, sizeof(min_cell), SNAPSHOT_FLAG(snapshot, MIN_CELL_VALID), snapshot->min_cell_voltage_mv);
    format_cell_v(avg_cell, sizeof(avg_cell), SNAPSHOT_FLAG(snapshot, AVERAGE_CELL_VALID), snapshot->average_cell_voltage_mv);
    format_cell_v(max_cell, sizeof(max_cell), SNAPSHOT_FLAG(snapshot, MAX_CELL_VALID), snapshot->max_cell_voltage_mv);
    if (SNAPSHOT_FLAG(snapshot, DELTA_CELL_VALID)) {
        (void)snprintf(delta_cell,
                       sizeof(delta_cell),
                       "%u.%03uV",
                       snapshot->delta_cell_voltage_mv / 1000U,
                       snapshot->delta_cell_voltage_mv % 1000U);
    } else {
        (void)snprintf(delta_cell, sizeof(delta_cell), "--");
    }
    label_set_text_if_changed(s_ui.cell_stat_values[0], max_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[1], min_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[2], delta_cell);
    label_set_text_if_changed(s_ui.cell_stat_values[3], avg_cell);

    if (snapshot->bms_protection_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_BAD);
        label_set_text_if_changed(s_ui.bms_error, "BMS WARN\nPROTECTION");
    } else if (snapshot->bms_warning_count > 0U) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_WARN);
        label_set_text_if_changed(s_ui.bms_error, "BMS WARN\nWARNING");
    } else if (SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_ACCENT);
        label_set_text_if_changed(s_ui.bms_error, "BMS INFO\nOK");
    } else if (strstr(snapshot->bms_info_text, "FAIL") != NULL ||
               strstr(snapshot->bms_info_text, "ERR") != NULL ||
               strstr(snapshot->bms_info_text, "NO ") != NULL) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_BAD);
        label_set_text_if_changed(s_ui.bms_error, "BLE STATUS\nFAILED");
    } else if (snapshot->bms_info_text[0] != '\0' &&
               strcmp(snapshot->bms_info_text, "BMS OFF") != 0) {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_WARN);
        label_set_text_if_changed(s_ui.bms_error, "BLE STATUS\nCONNECTING");
    } else {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_MUTED);
        label_set_text_if_changed(s_ui.bms_error, "BLE STATUS\nDISCONNECTED");
    }

    format_temp_c(t1, sizeof(t1), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 0U), snapshot->bms_temperature_celsius[0]);
    format_temp_c(t2, sizeof(t2), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 1U), snapshot->bms_temperature_celsius[1]);
    format_temp_c(t3, sizeof(t3), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 2U), snapshot->bms_temperature_celsius[2]);
    format_temp_c(t4, sizeof(t4), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 3U), snapshot->bms_temperature_celsius[3]);
    format_temp_c(mos, sizeof(mos), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 4U), snapshot->bms_temperature_celsius[4]);
    format_temp_c(bal, sizeof(bal), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 5U), snapshot->bms_temperature_celsius[5]);
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

static void controller_label_set(lv_obj_t *label_obj,
                                 char *buffer,
                                 size_t buffer_len,
                                 const char *text)
{
    if (!label_obj || strcmp(buffer, text) == 0) {
        return;
    }
    snprintf(buffer, buffer_len, "%s", text);
    lv_label_set_text_static(label_obj, buffer);
}

static void set_controller_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!s_ui.controller_page) {
        return;
    }
    char text[20];
    if (SNAPSHOT_FLAG(snapshot, CONTROLLER_SPEED_VALID)) {
        snprintf(text, sizeof(text), "%u",
                 (snapshot->controller_speed_deci_units + 5U) / 10U);
    } else {
        snprintf(text, sizeof(text), "-");
    }
    controller_label_set(s_ui.controller_speed, s_ui.controller_speed_buf,
                         sizeof(s_ui.controller_speed_buf), text);
    controller_label_set(s_ui.controller_speed_unit, s_ui.controller_speed_unit_buf,
                         sizeof(s_ui.controller_speed_unit_buf),
                         snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");
    snprintf(text, sizeof(text), SNAPSHOT_FLAG(snapshot, CONTROLLER_GEAR_VALID) ? "%u" : "-",
             snapshot->controller_gear);
    controller_label_set(s_ui.controller_gear, s_ui.controller_gear_buf,
                         sizeof(s_ui.controller_gear_buf), text);
    if (SNAPSHOT_FLAG(snapshot, CONTROLLER_POWER_VALID)) {
        snprintf(text, sizeof(text), "%ld.%01ld",
                 (long)(snapshot->controller_power_w / 1000),
                 (long)(labs(snapshot->controller_power_w) % 1000 / 100));
    } else {
        snprintf(text, sizeof(text), "-");
    }
    controller_label_set(s_ui.controller_power, s_ui.controller_power_buf,
                         sizeof(s_ui.controller_power_buf), text);
    snprintf(text, sizeof(text), SNAPSHOT_FLAG(snapshot, CONTROLLER_RPM_VALID) ? "%u" : "-",
             snapshot->controller_rpm);
    controller_label_set(s_ui.controller_rpm, s_ui.controller_rpm_buf,
                         sizeof(s_ui.controller_rpm_buf), text);
    snprintf(text, sizeof(text), SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID) ? "%d" : "-",
             snapshot->controller_temp_c);
    controller_label_set(s_ui.controller_temp, s_ui.controller_temp_buf,
                         sizeof(s_ui.controller_temp_buf), text);
    snprintf(text, sizeof(text), SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID) ? "%d" : "-",
             snapshot->motor_temp_c);
    controller_label_set(s_ui.controller_motor_temp, s_ui.controller_motor_temp_buf,
                         sizeof(s_ui.controller_motor_temp_buf), text);
}

static lv_obj_t *controller_dashboard_panel(lv_obj_t *parent,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h,
                                            lv_color_t color,
                                            lv_color_t border_color)
{
    lv_obj_t *obj = dashboard_panel(parent, x, y, w, h, color, border_color);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *controller_dashboard_label(lv_obj_t *parent,
                                            const char *text,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h,
                                            const lv_font_t *font,
                                            lv_color_t color)
{
    lv_obj_t *obj = label(parent, x, y, w, h, font);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_label_set_text_static(obj, text);
    return obj;
}

static void controller_dashboard_vertical_separator(lv_obj_t *parent,
                                                    int32_t x,
                                                    int32_t y,
                                                    int32_t h)
{
    lv_obj_t *line = lv_obj_create(parent);
    clear_style(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, 1, h);
    lv_obj_set_style_bg_color(line, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
}

static void __attribute__((unused)) create_controller_dashboard(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    lv_obj_t *frame = controller_dashboard_panel(s_ui.controller_page,
                                                 4,
                                                 4,
                                                 s_ui.width - 8,
                                                 s_ui.height - 8,
                                                 COLOR_DASHBOARD_BG,
                                                 COLOR_DASHBOARD_BORDER);
    const int32_t speed_w = portrait ? s_ui.width - 16 : 210;
    const int32_t speed_h = portrait ? 120 : 154;
    const int32_t gear_x = portrait ? 4 : 216;
    const int32_t gear_y = portrait ? 126 : 4;
    const int32_t gear_w = portrait ? s_ui.width - 16 : 92;
    const int32_t gear_h = portrait ? 108 : 154;
    const int32_t stats_y = portrait ? 236 : 160;
    const int32_t stats_w = s_ui.width - 16;
    const int32_t stats_h = portrait ? 72 : 68;

    lv_obj_t *speed_panel = controller_dashboard_panel(frame,
                                                       4,
                                                       4,
                                                       speed_w,
                                                       speed_h,
                                                       COLOR_DASHBOARD_BG,
                                                       COLOR_DASHBOARD_BORDER);
    lv_obj_t *gear_panel = controller_dashboard_panel(frame,
                                                      gear_x,
                                                      gear_y,
                                                      gear_w,
                                                      gear_h,
                                                      COLOR_SOC,
                                                      COLOR_DASHBOARD_SOC_BORDER);
    lv_obj_set_style_bg_grad_color(gear_panel, COLOR_DASHBOARD_SOC_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(gear_panel, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_t *stats_panel = controller_dashboard_panel(frame,
                                                       4,
                                                       stats_y,
                                                       stats_w,
                                                       stats_h,
                                                       COLOR_DASHBOARD_BG,
                                                       COLOR_DASHBOARD_BORDER);

    lv_obj_t *speed_title = controller_dashboard_label(speed_panel,
                                                       "SPEED",
                                                       8,
                                                       8,
                                                       speed_w - 16,
                                                       lv_font_montserrat_14.line_height,
                                                       &lv_font_montserrat_14,
                                                       COLOR_TEXT);
    lv_obj_set_style_text_align(speed_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_t *gear_title = controller_dashboard_label(gear_panel,
                                                      "GEAR",
                                                      8,
                                                      8,
                                                      gear_w - 16,
                                                      lv_font_montserrat_14.line_height,
                                                      &lv_font_montserrat_14,
                                                      COLOR_TEXT);
    lv_obj_set_style_text_align(gear_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    s_ui.controller_speed = controller_dashboard_label(speed_panel,
                                                       s_ui.controller_speed_buf,
                                                       4,
                                                       portrait ? 44 : 58,
                                                       116,
                                                       controller_digits_72.line_height,
                                                       &controller_digits_72,
                                                       COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.controller_speed, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_ui.controller_speed_unit = controller_dashboard_label(speed_panel,
                                                            s_ui.controller_speed_unit_buf,
                                                            120,
                                                            portrait ? 62 : 76,
                                                            speed_w - 124,
                                                            lv_font_montserrat_24.line_height,
                                                            &lv_font_montserrat_24,
                                                            COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.controller_speed_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.controller_gear = controller_dashboard_label(gear_panel,
                                                      s_ui.controller_gear_buf,
                                                      2,
                                                      portrait ? 24 : 58,
                                                      gear_w - 4,
                                                      controller_digits_72.line_height,
                                                      &controller_digits_72,
                                                      COLOR_TEXT);

    const lv_font_t *value_font = &lv_font_montserrat_14;
    const lv_font_t *unit_font = &lv_font_montserrat_14;
    if (portrait) {
        const int32_t cell_w = stats_w / 2;
        const int32_t row_h = stats_h / 2;
        controller_dashboard_vertical_separator(stats_panel, cell_w, 4, stats_h - 8);
        dashboard_separator(stats_panel, 6, row_h, stats_w - 12);

        s_ui.controller_power = controller_dashboard_label(stats_panel,
                                                           s_ui.controller_power_buf,
                                                           31,
                                                           10,
                                                           24,
                                                           value_font->line_height,
                                                           value_font,
                                                           COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "kW", 58, 10, 28,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        s_ui.controller_rpm = controller_dashboard_label(stats_panel,
                                                         s_ui.controller_rpm_buf,
                                                         cell_w + 20,
                                                         10,
                                                         38,
                                                         value_font->line_height,
                                                         value_font,
                                                         COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "RPM", cell_w + 62, 10, 38,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);

        lv_obj_t *controller_title = controller_dashboard_label(stats_panel,
                                                                "CTRL",
                                                                6,
                                                                row_h + 2,
                                                                cell_w - 12,
                                                                unit_font->line_height,
                                                                unit_font,
                                                                COLOR_TEXT);
        lv_obj_set_style_text_align(controller_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_temp = controller_dashboard_label(stats_panel,
                                                          s_ui.controller_temp_buf,
                                                          40,
                                                          row_h + 18,
                                                          22,
                                                          value_font->line_height,
                                                          value_font,
                                                          COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", 66, row_h + 18, 14,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *motor_title = controller_dashboard_label(stats_panel,
                                                           "MOTOR",
                                                           cell_w + 6,
                                                           row_h + 2,
                                                           cell_w - 12,
                                                           unit_font->line_height,
                                                           unit_font,
                                                           COLOR_TEXT);
        lv_obj_set_style_text_align(motor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_motor_temp = controller_dashboard_label(stats_panel,
                                                                s_ui.controller_motor_temp_buf,
                                                                cell_w + 40,
                                                                row_h + 18,
                                                                22,
                                                                value_font->line_height,
                                                                value_font,
                                                                COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", cell_w + 66, row_h + 18, 14,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
    } else {
        const int32_t col_w = stats_w / 4;
        for (int32_t index = 1; index < 4; ++index) {
            controller_dashboard_vertical_separator(stats_panel,
                                                    col_w * index,
                                                    6,
                                                    stats_h - 12);
        }
        s_ui.controller_power = controller_dashboard_label(stats_panel,
                                                           s_ui.controller_power_buf,
                                                           11,
                                                           26,
                                                           24,
                                                           value_font->line_height,
                                                           value_font,
                                                           COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "kW", 39, 26, 28,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        s_ui.controller_rpm = controller_dashboard_label(stats_panel,
                                                         s_ui.controller_rpm_buf,
                                                         col_w + 2,
                                                         26,
                                                         36,
                                                         value_font->line_height,
                                                         value_font,
                                                         COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "RPM", col_w + 40, 26, 34,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *controller_title = controller_dashboard_label(stats_panel,
                                                                "CTRL",
                                                                col_w * 2 + 6,
                                                                6,
                                                                col_w - 12,
                                                                unit_font->line_height,
                                                                unit_font,
                                                                COLOR_TEXT);
        lv_obj_set_style_text_align(controller_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_temp = controller_dashboard_label(stats_panel,
                                                          s_ui.controller_temp_buf,
                                                          col_w * 2 + 21,
                                                          34,
                                                          22,
                                                          value_font->line_height,
                                                          value_font,
                                                          COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", col_w * 2 + 47, 34, 14,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *motor_title = controller_dashboard_label(stats_panel,
                                                           "MOTOR",
                                                           col_w * 3 + 6,
                                                           6,
                                                           col_w - 12,
                                                           unit_font->line_height,
                                                           unit_font,
                                                           COLOR_TEXT);
        lv_obj_set_style_text_align(motor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_motor_temp = controller_dashboard_label(stats_panel,
                                                                s_ui.controller_motor_temp_buf,
                                                                col_w * 3 + 21,
                                                                34,
                                                                22,
                                                                value_font->line_height,
                                                                value_font,
                                                                COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", col_w * 3 + 47, 34, 14,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
    }
}

static bool settings_ble_candidate_rows_changed(
    const esp_bms_bms_scan_candidate_t *previous,
    uint8_t previous_count,
    const esp_bms_bms_scan_candidate_t *current,
    uint8_t current_count)
{
    if (previous_count != current_count) {
        return true;
    }
    for (uint8_t index = 0; index < current_count; ++index) {
        const esp_bms_bms_scan_candidate_t *old_candidate = &previous[index];
        const esp_bms_bms_scan_candidate_t *new_candidate = &current[index];
        if (old_candidate->has_name != new_candidate->has_name ||
            strcmp(old_candidate->mac, new_candidate->mac) != 0 ||
            strcmp(old_candidate->name, new_candidate->name) != 0) {
            return true;
        }
    }
    return false;
}

static bool settings_controller_candidate_rows_changed(
    const esp_bms_dashboard_snapshot_t *previous,
    const esp_bms_dashboard_snapshot_t *current)
{
    return settings_ble_candidate_rows_changed(previous->controller_scan_candidates,
                                               previous->controller_scan_candidate_count,
                                               current->controller_scan_candidates,
                                               current->controller_scan_candidate_count);
}

static bool settings_controller_view_changed(const esp_bms_dashboard_snapshot_t *previous,
                                             const esp_bms_dashboard_snapshot_t *current,
                                             bool had_previous)
{
    return !had_previous ||
           previous->speed_unit != current->speed_unit ||
           previous->speed_source != current->speed_source ||
           previous->active_speed_source != current->active_speed_source ||
           SNAPSHOT_FLAG(previous, CONTROLLER_PAGE_ENABLED) !=
               SNAPSHOT_FLAG(current, CONTROLLER_PAGE_ENABLED) ||
           SNAPSHOT_FLAG(previous, CONTROLLER_CONNECTION_ENABLED) !=
               SNAPSHOT_FLAG(current, CONTROLLER_CONNECTION_ENABLED) ||
           SNAPSHOT_FLAG(previous, CONTROLLER_ONLINE) !=
               SNAPSHOT_FLAG(current, CONTROLLER_ONLINE) ||
           previous->controller_scan_active != current->controller_scan_active ||
           previous->controller_scan_revision != current->controller_scan_revision ||
           previous->controller_param_source != current->controller_param_source ||
           previous->controller_tire_rim_inch != current->controller_tire_rim_inch ||
           previous->controller_tire_aspect_percent != current->controller_tire_aspect_percent ||
           previous->controller_tire_width_mm != current->controller_tire_width_mm ||
           previous->controller_wheel_circumference_mm !=
               current->controller_wheel_circumference_mm ||
           previous->controller_gear_ratio_centi != current->controller_gear_ratio_centi ||
           previous->controller_fallback_tire_rim_inch !=
               current->controller_fallback_tire_rim_inch ||
           previous->controller_fallback_tire_aspect_percent !=
               current->controller_fallback_tire_aspect_percent ||
           previous->controller_fallback_tire_width_mm !=
               current->controller_fallback_tire_width_mm ||
           previous->controller_fallback_gear_ratio_centi !=
               current->controller_fallback_gear_ratio_centi ||
           strcmp(previous->controller_bound_name, current->controller_bound_name) != 0 ||
           settings_controller_candidate_rows_changed(previous, current);
}

static void gps_label_set(lv_obj_t *label_obj,
                          char *buffer,
                          size_t buffer_len,
                          const char *text)
{
    if (!label_obj || strcmp(buffer, text) == 0) {
        return;
    }
    snprintf(buffer, buffer_len, "%s", text);
    lv_label_set_text_static(label_obj, buffer);
}

static lv_point_t speed_dashboard_point(int32_t x, int32_t y)
{
    const lv_point_t point = { .x = x, .y = y };
    return point;
}

static void speed_dashboard_draw_line(lv_layer_t *layer,
                                      lv_point_t start,
                                      lv_point_t end,
                                      lv_color_t color,
                                      int32_t width,
                                      bool rounded)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1.x = start.x;
    line.p1.y = start.y;
    line.p2.x = end.x;
    line.p2.y = end.y;
    line.color = color;
    line.width = width;
    line.opa = LV_OPA_COVER;
    line.round_start = rounded;
    line.round_end = rounded;
    lv_draw_line(layer, &line);
}

static void speed_dashboard_draw_triangle(lv_layer_t *layer,
                                          lv_point_t p0,
                                          lv_point_t p1,
                                          lv_point_t p2,
                                          lv_color_t color)
{
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.p[0].x = p0.x;
    triangle.p[0].y = p0.y;
    triangle.p[1].x = p1.x;
    triangle.p[1].y = p1.y;
    triangle.p[2].x = p2.x;
    triangle.p[2].y = p2.y;
    triangle.color = color;
    triangle.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &triangle);
}

static void speed_dashboard_draw_rect(lv_layer_t *layer,
                                      lv_area_t area,
                                      lv_color_t color,
                                      bool filled,
                                      int32_t radius)
{
    lv_draw_rect_dsc_t rectangle;
    lv_draw_rect_dsc_init(&rectangle);
    rectangle.radius = radius;
    rectangle.bg_color = color;
    rectangle.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    rectangle.border_color = color;
    rectangle.border_opa = LV_OPA_COVER;
    rectangle.border_width = filled ? 0 : 1;
    lv_draw_rect(layer, &rectangle, &area);
}

static uint32_t speed_dashboard_smooth_step(uint32_t index)
{
    const uint32_t position = index * 1024U / SPEED_DASHBOARD_SEGMENT_COUNT;
    return (uint32_t)(((uint64_t)position * position * (3072U - (2U * position))) /
                      UINT64_C(1048576));
}

static void speed_dashboard_geometry(bool portrait,
                                     const lv_area_t *coords,
                                     lv_point_t *outer,
                                     lv_point_t *inner)
{
    for (uint32_t index = 0U; index <= SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        const uint32_t smooth = speed_dashboard_smooth_step(index);
        if (portrait) {
            outer[index] = speed_dashboard_point(
                coords->x1 + 28 + (int32_t)(180U * smooth / 1024U),
                coords->y1 + 292 - (int32_t)(228U * index / SPEED_DASHBOARD_SEGMENT_COUNT));
            inner[index] = speed_dashboard_point(
                coords->x1 + 86 + (int32_t)(126U * smooth / 1024U),
                coords->y1 + 292 - (int32_t)(175U * index / SPEED_DASHBOARD_SEGMENT_COUNT));
        } else {
            outer[index] = speed_dashboard_point(
                coords->x1 + 14 + (int32_t)(292U * index / SPEED_DASHBOARD_SEGMENT_COUNT),
                coords->y1 + 185 - (int32_t)(88U * smooth / 1024U));
            inner[index] = speed_dashboard_point(
                coords->x1 + 14 + (int32_t)(286U * index / SPEED_DASHBOARD_SEGMENT_COUNT),
                coords->y1 + 222 - (int32_t)(78U * smooth / 1024U));
        }
    }
}

static lv_color_t speed_dashboard_segment_color(uint32_t index,
                                                uint32_t active_segments,
                                                bool speed_valid)
{
    if (index >= 28U) {
        return COLOR_SPEED_BAND_DANGER;
    }
    if (!speed_valid || index >= active_segments) {
        return COLOR_SPEED_BAND_IDLE;
    }
    const uint32_t denominator = active_segments > 1U ? active_segments - 1U : 1U;
    const uint8_t progress = (uint8_t)(index * 255U / denominator);
    if (progress < 176U) {
        return lv_color_mix(COLOR_SPEED_BAND_BLUE,
                            COLOR_SPEED_BAND_DARK,
                            (uint8_t)(progress * 255U / 176U));
    }
    return lv_color_mix(COLOR_WHITE,
                        COLOR_SPEED_BAND_BLUE,
                        (uint8_t)((progress - 176U) * 255U / 79U));
}

static uint32_t speed_dashboard_active_segments(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, SPEED_VALID)) {
        return 0U;
    }
    const uint32_t maximum = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? 1200U : 1800U;
    const uint32_t clamped_speed = snapshot->speed_deci_units > maximum
                                       ? maximum
                                       : snapshot->speed_deci_units;
    return (clamped_speed * SPEED_DASHBOARD_SEGMENT_COUNT + maximum - 1U) / maximum;
}

static uint32_t speed_dashboard_battery_active_segments(
    const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, BMS_ONLINE) || !SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        return 0U;
    }
    const uint32_t soc = snapshot->soc_percent > 100U ? 100U : snapshot->soc_percent;
    return (soc * 8U + 99U) / 100U;
}

static uint32_t speed_dashboard_render_signature(
    const esp_bms_dashboard_snapshot_t *snapshot)
{
    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
    uint32_t signature = speed_dashboard_active_segments(snapshot);
    signature |= SNAPSHOT_FLAG(snapshot, SPEED_VALID) ? UINT32_C(1) << 6 : 0U;
    signature |= SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID) ? UINT32_C(1) << 7 : 0U;
    signature |= bms_online ? UINT32_C(1) << 8 : 0U;
    signature |= bms_online && SNAPSHOT_FLAG(snapshot, SOC_VALID)
                     ? UINT32_C(1) << 9
                     : 0U;
    signature |= speed_dashboard_battery_active_segments(snapshot) << 10;
    return signature;
}

static int32_t speed_dashboard_band_width(bool portrait,
                                          lv_point_t outer_start,
                                          lv_point_t inner_start,
                                          lv_point_t outer_end,
                                          lv_point_t inner_end)
{
    const int32_t start_width = portrait ? abs(inner_start.x - outer_start.x)
                                         : abs(inner_start.y - outer_start.y);
    const int32_t end_width = portrait ? abs(inner_end.x - outer_end.x)
                                       : abs(inner_end.y - outer_end.y);
    const int32_t width = (start_width + end_width + 1) / 2;
    return width > 2 ? width : 2;
}

static void speed_dashboard_overlap_band_endpoints(uint32_t index,
                                                   lv_point_t *start,
                                                   lv_point_t *end)
{
    const int32_t dx = end->x - start->x;
    const int32_t dy = end->y - start->y;
    const int32_t span = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (span == 0) {
        return;
    }
    if (index > 0U) {
        start->x -= dx / span;
        start->y -= dy / span;
    }
    if (index + 1U < SPEED_DASHBOARD_SEGMENT_COUNT) {
        end->x += dx / span;
        end->y += dy / span;
    }
}

static void speed_dashboard_draw_battery(lv_layer_t *layer,
                                         const lv_area_t *coords,
                                         bool portrait,
                                         const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        return;
    }
    const int32_t x = coords->x1 + 8;
    const int32_t y = coords->y1 + (portrait ? 6 : 7);
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x, y + 2, x + 7, y + 21 },
                              COLOR_WHITE,
                              false,
                              1);
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x + 2, y, x + 5, y + 2 },
                              COLOR_WHITE,
                              true,
                              0);

    if (!SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        return;
    }
    const uint32_t active = speed_dashboard_battery_active_segments(snapshot);
    const int32_t start_x = x + 12;
    const int32_t segment_y = y + 5;
    for (uint32_t index = 0U; index < active; ++index) {
        const int32_t left = start_x + (int32_t)(index * 6U);
        const lv_point_t p0 = speed_dashboard_point(left + 2, segment_y);
        const lv_point_t p1 = speed_dashboard_point(left + 6, segment_y);
        const lv_point_t p2 = speed_dashboard_point(left + 4, segment_y + 6);
        const lv_point_t p3 = speed_dashboard_point(left, segment_y + 6);
        speed_dashboard_draw_triangle(layer, p0, p1, p2, COLOR_WHITE);
        speed_dashboard_draw_triangle(layer, p0, p2, p3, COLOR_WHITE);
    }
}

static void speed_dashboard_draw_satellite(lv_layer_t *layer,
                                           const lv_area_t *coords,
                                           bool portrait,
                                           bool gps_fix_valid)
{
    const int32_t x = coords->x1 + (portrait ? 26 : 136);
    const int32_t y = coords->y1 + (portrait ? 29 : 8);
    const lv_point_t top = speed_dashboard_point(x + 7, y + 2);
    const lv_point_t right = speed_dashboard_point(x + 12, y + 7);
    const lv_point_t bottom = speed_dashboard_point(x + 7, y + 12);
    const lv_point_t left = speed_dashboard_point(x + 2, y + 7);
    speed_dashboard_draw_triangle(layer, top, right, bottom, COLOR_WHITE);
    speed_dashboard_draw_triangle(layer, top, bottom, left, COLOR_WHITE);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + 1, y + 1),
                              speed_dashboard_point(x + 5, y + 5),
                              COLOR_WHITE,
                              3,
                              false);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + 9, y + 9),
                              speed_dashboard_point(x + 14, y + 14),
                              COLOR_WHITE,
                              3,
                              false);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + 7, y + 12),
                              speed_dashboard_point(x + 3, y + 16),
                              COLOR_WHITE,
                              1,
                              false);
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x + 15, y + 1, x + 20, y + 6 },
                              gps_fix_valid ? COLOR_SPEED_GPS_OK : COLOR_WARN,
                              true,
                              LV_RADIUS_CIRCLE);
}

static void speed_dashboard_draw_event_cb(lv_event_t *event)
{
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
    const int64_t draw_started_us = esp_timer_get_time();
#endif
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const bool portrait = lv_area_get_width(&coords) < lv_area_get_height(&coords);
    const esp_bms_dashboard_snapshot_t *snapshot = &s_ui.last_snapshot;
    const bool speed_valid = SNAPSHOT_FLAG(snapshot, SPEED_VALID);
    const uint32_t active_segments = speed_dashboard_active_segments(snapshot);

    lv_point_t outer[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    lv_point_t inner[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    speed_dashboard_geometry(portrait, &coords, outer, inner);
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        const lv_color_t color = speed_dashboard_segment_color(index,
                                                                active_segments,
                                                                speed_valid);
        lv_point_t start = speed_dashboard_point(
            (outer[index].x + inner[index].x) / 2,
            (outer[index].y + inner[index].y) / 2);
        lv_point_t end = speed_dashboard_point(
            (outer[index + 1U].x + inner[index + 1U].x) / 2,
            (outer[index + 1U].y + inner[index + 1U].y) / 2);
        speed_dashboard_overlap_band_endpoints(index, &start, &end);
        speed_dashboard_draw_line(layer,
                                  start,
                                  end,
                                  color,
                                  speed_dashboard_band_width(portrait,
                                                             outer[index],
                                                             inner[index],
                                                             outer[index + 1U],
                                                             inner[index + 1U]),
                                  false);
    }
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        speed_dashboard_draw_line(layer,
                                  outer[index],
                                  outer[index + 1U],
                                  index >= 28U ? COLOR_SPEED_BAND_DANGER : COLOR_WHITE,
                                  2,
                                  false);
    }

    for (uint32_t index = 0U; index <= SPEED_DASHBOARD_SEGMENT_COUNT; index += 2U) {
        const bool major = index % 8U == 0U || index == SPEED_DASHBOARD_SEGMENT_COUNT;
        const int32_t numerator = major ? 38 : 22;
        const lv_point_t tick_end = speed_dashboard_point(
            outer[index].x + ((inner[index].x - outer[index].x) * numerator / 100),
            outer[index].y + ((inner[index].y - outer[index].y) * numerator / 100));
        speed_dashboard_draw_line(layer,
                                  outer[index],
                                  tick_end,
                                  index >= 28U ? COLOR_SPEED_BAND_DANGER : COLOR_WHITE,
                                  major ? 2 : 1,
                                  false);
    }

    const int32_t divider_y = coords.y1 + (portrait ? 50 : 34);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(coords.x1 + 8, divider_y),
                              speed_dashboard_point(coords.x2 - 8, divider_y),
                              COLOR_SPEED_DIVIDER,
                              1,
                              false);
    speed_dashboard_draw_battery(layer, &coords, portrait, snapshot);
    speed_dashboard_draw_satellite(layer,
                                   &coords,
                                   portrait,
                                   SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID));
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
    const int64_t elapsed_us = esp_timer_get_time() - draw_started_us;
    const uint32_t bounded_elapsed_us = elapsed_us > UINT32_MAX
                                            ? UINT32_MAX
                                            : (uint32_t)elapsed_us;
    s_ui.speed_art_draw_count++;
    s_ui.speed_art_draw_elapsed_us += bounded_elapsed_us;
    if (bounded_elapsed_us > s_ui.speed_art_draw_max_us) {
        s_ui.speed_art_draw_max_us = bounded_elapsed_us;
    }
#endif
}

static void speed_dashboard_apply_layout(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    lv_obj_set_pos(s_ui.speed_art, 0, 0);
    lv_obj_set_size(s_ui.speed_art, s_ui.width, s_ui.height);
    if (portrait) {
        lv_obj_set_pos(s_ui.speed, 14, 58);
        lv_obj_set_size(s_ui.speed, 104, 52);
        lv_obj_set_pos(s_ui.gps_speed_unit, 20, 105);
        lv_obj_set_size(s_ui.gps_speed_unit, 76, 26);
        lv_obj_set_pos(s_ui.speed_soc, 90, 7);
        lv_obj_set_size(s_ui.speed_soc, 30, 18);
        lv_obj_set_pos(s_ui.speed_consumption, 74, 7);
        lv_obj_set_size(s_ui.speed_consumption, 159, 18);
        lv_obj_set_pos(s_ui.speed_controller_temp, 66, 29);
        lv_obj_set_size(s_ui.speed_controller_temp, 55, 18);
        lv_obj_set_pos(s_ui.speed_motor_temp, 124, 29);
        lv_obj_set_size(s_ui.speed_motor_temp, 70, 18);
        lv_obj_set_pos(s_ui.speed_gear, 167, 230);
        lv_obj_set_size(s_ui.speed_gear, 40, 44);
        lv_obj_set_pos(s_ui.gps_detail, 112, 278);
        lv_obj_set_size(s_ui.gps_detail, 120, 34);
        static const int16_t positions[SPEED_DASHBOARD_SCALE_LABEL_COUNT][2] = {
            { 16, 264 }, { 56, 218 }, { 101, 160 }, { 139, 111 }, { 178, 67 }, { 207, 51 },
        };
        for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
            lv_obj_set_pos(s_ui.speed_scale_labels[index], positions[index][0], positions[index][1]);
            lv_obj_set_size(s_ui.speed_scale_labels[index], 34, 18);
        }
    } else {
        lv_obj_set_pos(s_ui.speed, 0, 56);
        lv_obj_set_size(s_ui.speed, 94, 52);
        lv_obj_set_pos(s_ui.gps_speed_unit, 98, 78);
        lv_obj_set_size(s_ui.gps_speed_unit, 68, 26);
        lv_obj_set_pos(s_ui.speed_soc, 90, 9);
        lv_obj_set_size(s_ui.speed_soc, 30, 18);
        lv_obj_set_pos(s_ui.speed_consumption, 74, 9);
        lv_obj_set_size(s_ui.speed_consumption, 58, 18);
        lv_obj_set_pos(s_ui.speed_controller_temp, 164, 9);
        lv_obj_set_size(s_ui.speed_controller_temp, 36, 18);
        lv_obj_set_pos(s_ui.speed_motor_temp, 206, 9);
        lv_obj_set_size(s_ui.speed_motor_temp, 48, 18);
        lv_obj_set_pos(s_ui.speed_gear, 269, 153);
        lv_obj_set_size(s_ui.speed_gear, 38, 40);
        lv_obj_set_pos(s_ui.gps_detail, 196, 195);
        lv_obj_set_size(s_ui.gps_detail, 120, 34);
        static const int16_t positions[SPEED_DASHBOARD_SCALE_LABEL_COUNT][2] = {
            { 8, 168 }, { 53, 148 }, { 111, 124 }, { 174, 102 }, { 244, 84 }, { 286, 80 },
        };
        for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
            lv_obj_set_pos(s_ui.speed_scale_labels[index], positions[index][0], positions[index][1]);
            lv_obj_set_size(s_ui.speed_scale_labels[index], 34, 18);
        }
    }
}

static void set_gps_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    char text[32];
    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
    if (SNAPSHOT_FLAG(snapshot, SPEED_VALID)) {
        snprintf(text,
                 sizeof(text),
                 "%u",
                 (snapshot->speed_deci_units + 5U) / 10U);
    } else {
        snprintf(text, sizeof(text), "-");
    }
    gps_label_set(s_ui.speed, s_ui.gps_speed_buf, sizeof(s_ui.gps_speed_buf), text);
    gps_label_set(s_ui.gps_speed_unit,
                  s_ui.gps_speed_unit_buf,
                  sizeof(s_ui.gps_speed_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");

    set_obj_hidden(s_ui.speed_soc, true);

    const char *consumption_unit = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                       ? "Wh/mi"
                                       : "Wh/km";
    if (snapshot->average_consumption_valid) {
        const int32_t deci = snapshot->average_consumption_deci_wh_per_distance;
        const int32_t rounded = deci >= 0 ? (deci + 5) / 10 : (deci - 5) / 10;
        snprintf(text, sizeof(text), "%ld %s", (long)rounded, consumption_unit);
    } else {
        snprintf(text, sizeof(text), "-- %s", consumption_unit);
    }
    gps_label_set(s_ui.speed_consumption,
                  s_ui.speed_consumption_buf,
                  sizeof(s_ui.speed_consumption_buf),
                  text);
    set_obj_hidden(s_ui.speed_consumption, !bms_online);

    const bool controller_online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    const bool controller_temp_visible = controller_online &&
                                         SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID);
    const bool motor_temp_visible = controller_online &&
                                    SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID);
    const bool gear_valid = controller_online && SNAPSHOT_FLAG(snapshot, CONTROLLER_GEAR_VALID);
    if (controller_temp_visible) {
        snprintf(text, sizeof(text), "控 %dC", snapshot->controller_temp_c);
        gps_label_set(s_ui.speed_controller_temp,
                      s_ui.speed_controller_temp_buf,
                      sizeof(s_ui.speed_controller_temp_buf),
                      text);
    }
    if (motor_temp_visible) {
        snprintf(text, sizeof(text), "电机 %dC", snapshot->motor_temp_c);
        gps_label_set(s_ui.speed_motor_temp,
                      s_ui.speed_motor_temp_buf,
                      sizeof(s_ui.speed_motor_temp_buf),
                      text);
    }
    snprintf(text, sizeof(text), "%u", gear_valid ? snapshot->controller_gear : 1U);
    gps_label_set(s_ui.speed_gear,
                  s_ui.speed_gear_buf,
                  sizeof(s_ui.speed_gear_buf),
                  text);
    set_obj_hidden(s_ui.speed_controller_temp, !controller_temp_visible);
    set_obj_hidden(s_ui.speed_motor_temp, !motor_temp_visible);
    set_obj_hidden(s_ui.speed_gear, false);

    if (snapshot->gps_local_time_valid) {
        snprintf(text, sizeof(text), "%02u:%02u",
                 snapshot->gps_local_hour,
                 snapshot->gps_local_minute);
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    gps_label_set(s_ui.gps_detail, s_ui.gps_uptime_buf, sizeof(s_ui.gps_uptime_buf), text);

    static const uint16_t metric_scale[SPEED_DASHBOARD_SCALE_LABEL_COUNT] = {
        0U, 40U, 80U, 120U, 160U, 180U,
    };
    static const uint16_t imperial_scale[SPEED_DASHBOARD_SCALE_LABEL_COUNT] = {
        0U, 30U, 60U, 90U, 110U, 120U,
    };
    const uint16_t *scale = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                ? imperial_scale
                                : metric_scale;
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
        snprintf(text, sizeof(text), "%u", scale[index]);
        gps_label_set(s_ui.speed_scale_labels[index],
                      s_ui.speed_scale_buf[index],
                      sizeof(s_ui.speed_scale_buf[index]),
                      text);
    }
    const uint32_t render_signature = speed_dashboard_render_signature(snapshot);
    if (s_ui.speed_art &&
        (!s_ui.speed_art_signature_valid || s_ui.speed_art_signature != render_signature)) {
        s_ui.speed_art_signature = render_signature;
        s_ui.speed_art_signature_valid = true;
        lv_obj_invalidate(s_ui.speed_art);
    }
}

static void create_gps_dashboard(void)
{
    s_ui.speed_art = lv_obj_create(s_ui.gps_page);
    clear_style(s_ui.speed_art);
    lv_obj_clear_flag(s_ui.speed_art, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.speed_art,
                        speed_dashboard_draw_event_cb,
                        LV_EVENT_DRAW_MAIN,
                        NULL);

    s_ui.speed = controller_dashboard_label(s_ui.gps_page,
                                            s_ui.gps_speed_buf,
                                            0,
                                            0,
                                            1,
                                            lv_font_montserrat_48.line_height,
                                            &lv_font_montserrat_48,
                                            COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.speed, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_ui.gps_speed_unit = controller_dashboard_label(s_ui.gps_page,
                                                     s_ui.gps_speed_unit_buf,
                                                     0,
                                                     0,
                                                     1,
                                                     lv_font_montserrat_24.line_height,
                                                     &lv_font_montserrat_24,
                                                     COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.gps_speed_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    s_ui.speed_soc = controller_dashboard_label(s_ui.gps_page,
                                                s_ui.speed_soc_buf,
                                                0, 0, 1,
                                                settings_zh_10.line_height,
                                                &settings_zh_10,
                                                COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.speed_soc, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_obj_hidden(s_ui.speed_soc, true);
    s_ui.speed_consumption = controller_dashboard_label(s_ui.gps_page,
                                                        s_ui.speed_consumption_buf,
                                                        0, 0, 1,
                                                        settings_zh_10.line_height,
                                                        &settings_zh_10,
                                                        COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_consumption, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_consumption, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    set_obj_hidden(s_ui.speed_consumption, true);
    s_ui.speed_controller_temp = controller_dashboard_label(s_ui.gps_page,
                                                            s_ui.speed_controller_temp_buf,
                                                            0, 0, 1,
                                                            settings_zh_10.line_height,
                                                            &settings_zh_10,
                                                            COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_controller_temp, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_controller_temp, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.speed_motor_temp = controller_dashboard_label(s_ui.gps_page,
                                                       s_ui.speed_motor_temp_buf,
                                                       0, 0, 1,
                                                       settings_zh_10.line_height,
                                                       &settings_zh_10,
                                                       COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_motor_temp, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_motor_temp, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.speed_gear = controller_dashboard_label(s_ui.gps_page,
                                                 s_ui.speed_gear_buf,
                                                 0, 0, 1,
                                                 lv_font_montserrat_28.line_height,
                                                 &lv_font_montserrat_28,
                                                 COLOR_SPEED_BAND_BLUE);
    lv_obj_set_style_border_width(s_ui.speed_gear, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.speed_gear, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.speed_gear, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.speed_gear, COLOR_DASHBOARD_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.speed_gear, LV_OPA_70, LV_PART_MAIN);

    s_ui.gps_detail = controller_dashboard_label(s_ui.gps_page,
                                                 s_ui.gps_uptime_buf,
                                                 0, 0, 1,
                                                 lv_font_montserrat_24.line_height,
                                                 &lv_font_montserrat_24,
                                                 COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.gps_detail, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
        s_ui.speed_scale_labels[index] = controller_dashboard_label(
            s_ui.gps_page,
            s_ui.speed_scale_buf[index],
            0, 0, 1,
            lv_font_montserrat_14.line_height,
            &lv_font_montserrat_14,
            index + 1U == SPEED_DASHBOARD_SCALE_LABEL_COUNT
                ? COLOR_SPEED_BAND_DANGER
                : COLOR_TEXT);
    }
    speed_dashboard_apply_layout();
}

static void apply_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    const bool had_last_snapshot = UI_FLAG(LAST_SNAPSHOT_VALID);
    const bool previous_bms_online = SNAPSHOT_FLAG(&s_ui.last_snapshot, BMS_ONLINE);
    const bool previous_controller_online =
        SNAPSHOT_FLAG(&s_ui.last_snapshot, CONTROLLER_ONLINE);
    const uint8_t previous_bms_type = s_ui.last_snapshot.bms_type;
    const bool previous_bluetooth_enabled = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_ENABLED);
    const bool previous_bluetooth_advertising = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_ADVERTISING);
    const bool previous_bluetooth_connected = SNAPSHOT_FLAG(&s_ui.last_snapshot, BLUETOOTH_CONNECTED);
    const uint8_t previous_bms_scan_candidate_count = s_ui.last_snapshot.bms_scan_candidate_count;
    char previous_bms_info_text[sizeof(s_ui.last_snapshot.bms_info_text)] = { 0 };
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
    }
    const bool bms_scan_candidates_changed =
        !had_last_snapshot ||
        strcmp(previous_bms_info_text, snapshot->bms_info_text) != 0 ||
        settings_ble_candidate_rows_changed(s_ui.last_snapshot.bms_scan_candidates,
                                            previous_bms_scan_candidate_count,
                                            snapshot->bms_scan_candidates,
                                            snapshot->bms_scan_candidate_count);
    const bool controller_view_changed =
        settings_controller_view_changed(&s_ui.last_snapshot, snapshot, had_last_snapshot);
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
    set_dashboard(snapshot);
    set_controller_dashboard(snapshot);
    set_gps_dashboard(snapshot);
    set_cast_page(snapshot);
    if (had_last_snapshot &&
        ((!previous_bms_online && SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) ||
         (!previous_controller_online && SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE)))) {
        quick_toast_show_text("绑定成功");
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
         strcmp(previous_bluetooth_name, snapshot->bluetooth_name) != 0)) {
        settings_show_detail(SETTINGS_DETAIL_BLUETOOTH);
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BMS) {
        const bool bms_online_changed =
            !had_last_snapshot || previous_bms_online != SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
        const bool bms_type_changed = !had_last_snapshot || previous_bms_type != snapshot->bms_type;
        if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST &&
            (bms_scan_candidates_changed || bms_online_changed)) {
            settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_BMS, false);
        } else if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_TYPE_LIST &&
                   bms_type_changed) {
            settings_show_bms_type_picker();
        } else if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_ROOT &&
                   (bms_scan_candidates_changed || bms_online_changed || bms_type_changed)) {
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
            settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_CONTROLLER, false);
        }
    }
}

static void defer_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    memcpy(&s_ui.deferred_snapshot, snapshot, sizeof(s_ui.deferred_snapshot));
    UI_SET_FLAG(DEFERRED_SNAPSHOT_VALID, true);
}

static void flush_deferred_dashboard_snapshot(void)
{
    if (!UI_FLAG(DEFERRED_SNAPSHOT_VALID)) {
        return;
    }

    esp_bms_dashboard_snapshot_t snapshot;
    memcpy(&snapshot, &s_ui.deferred_snapshot, sizeof(snapshot));
    UI_SET_FLAG(DEFERRED_SNAPSHOT_VALID, false);
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
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER || page == ESP_BMS_LVGL_PAGE_GPS) {
        return s_ui.width;
    }
    if (page == ESP_BMS_LVGL_PAGE_CAST) {
        return s_ui.width * 2;
    }
    return 0;
}

static esp_bms_lvgl_page_t page_from_scroll_x(int32_t scroll_x)
{
    const int32_t index = (scroll_x + (s_ui.width / 2)) / s_ui.width;
    if (index <= 0) {
        return ESP_BMS_LVGL_PAGE_BATTERY;
    }
    if (index == 1) {
        return ESP_BMS_LVGL_PAGE_GPS;
    }
    return ESP_BMS_LVGL_PAGE_CAST;
}

static void finish_page_scroll_state(bool flush_snapshot)
{
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
        s_ui.page = page_from_scroll_x(lv_obj_get_scroll_x(s_ui.pages));
        lv_obj_scroll_to_x(s_ui.pages, page_target_scroll_x(s_ui.page), LV_ANIM_OFF);
        invalidate_dashboard_viewport();
    }

    UI_SET_FLAG(DRAGGING, false);
    UI_SET_FLAG(SETTLING, false);
    s_ui.page_scroll_gesture_active = false;
    s_ui.page_scroll_throw_frozen = false;
    s_ui.drag_pages_dx = 0;
    s_ui.drag_release_pages_dx = 0;
    s_ui.drag_start_page = s_ui.page;
    if (flush_snapshot) {
        flush_deferred_dashboard_snapshot();
    }
}

static void move_to_page(esp_bms_lvgl_page_t page, bool animated)
{
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER) {
        page = ESP_BMS_LVGL_PAGE_GPS;
    }
    lv_obj_stop_scroll_anim(s_ui.pages);
    const int32_t target_x = page_target_scroll_x(page);
    const int32_t current_x = lv_obj_get_scroll_x(s_ui.pages);
    if (current_x == target_x) {
        s_ui.page = page;
        UI_SET_FLAG(DRAGGING, false);
        UI_SET_FLAG(SETTLING, false);
        flush_deferred_dashboard_snapshot();
        return;
    }

    s_ui.page = page;
    UI_SET_FLAG(DRAGGING, false);
    UI_SET_FLAG(SETTLING, animated);
    lv_obj_scroll_to_x(s_ui.pages, target_x, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animated) {
        flush_deferred_dashboard_snapshot();
    }
}

static void screen_unlock_timer_cancel(void)
{
    if (s_ui.screen_unlock_timer) {
        lv_timer_delete(s_ui.screen_unlock_timer);
        s_ui.screen_unlock_timer = NULL;
    }
}

static void screen_unlock_reset(void)
{
    UI_SET_FLAG(SCREEN_UNLOCK_DRAGGING, false);
    s_ui.screen_unlock_knob_x = 4;
    if (s_ui.screen_unlock_knob) {
        lv_obj_set_x(s_ui.screen_unlock_knob, s_ui.screen_unlock_knob_x);
    }
    if (s_ui.screen_unlock_fill) {
        lv_obj_set_width(s_ui.screen_unlock_fill, SCREEN_UNLOCK_KNOB_SIZE);
    }
}

static void screen_unlock_prompt_hide(void)
{
    UI_SET_FLAG(SCREEN_UNLOCK_PROMPT_VISIBLE, false);
    screen_unlock_reset();
    set_obj_hidden(s_ui.screen_unlock_card, true);
}

static void screen_unlock_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    s_ui.screen_unlock_timer = NULL;
    screen_unlock_prompt_hide();
}

static void screen_unlock_prompt_show(void)
{
    if (!UI_FLAG(SCREEN_LOCKED) || !s_ui.screen_unlock_card) {
        return;
    }

    screen_unlock_timer_cancel();
    screen_unlock_reset();
    UI_SET_FLAG(SCREEN_UNLOCK_PROMPT_VISIBLE, true);
    set_obj_hidden(s_ui.screen_unlock_card, false);
    lv_obj_move_foreground(s_ui.screen_unlock_card);
    s_ui.screen_unlock_timer = lv_timer_create(screen_unlock_timeout_cb,
                                               SCREEN_LOCK_PROMPT_TIMEOUT_MS,
                                               NULL);
    if (s_ui.screen_unlock_timer) {
        lv_timer_set_repeat_count(s_ui.screen_unlock_timer, 1);
    }
}

static void screen_lock_reapply(void)
{
    if (!UI_FLAG(SCREEN_LOCKED)) {
        set_obj_hidden(s_ui.screen_lock_guard, true);
        return;
    }

    set_obj_hidden(s_ui.screen_lock_guard, false);
    set_obj_hidden(s_ui.screen_unlock_card, !UI_FLAG(SCREEN_UNLOCK_PROMPT_VISIBLE));
    set_obj_hidden(s_ui.quick_pull_zone, true);
    if (s_ui.screen_lock_guard) {
        lv_obj_move_foreground(s_ui.screen_lock_guard);
    }
}

static void screen_lock_exit(void)
{
    screen_unlock_timer_cancel();
    screen_unlock_prompt_hide();
    UI_SET_FLAG(SCREEN_LOCKED, false);
    set_obj_hidden(s_ui.screen_lock_guard, true);
    set_quick_panel_open(false);
}

static void screen_lock_enter(void)
{
    screen_unlock_timer_cancel();
    set_quick_panel_open(false);
    UI_SET_FLAG(SCREEN_LOCKED, true);
    screen_unlock_prompt_hide();
    screen_lock_reapply();
}

static void screen_lock_guard_event_cb(lv_event_t *event)
{
    if (!UI_FLAG(SCREEN_LOCKED) || UI_FLAG(SCREEN_UNLOCK_PROMPT_VISIBLE)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.screen_lock_drag_dx = 0;
        s_ui.screen_lock_drag_dy = 0;
        (void)get_active_pointer(&s_ui.screen_lock_press_start);
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        lv_point_t point = { 0 };
        if (get_active_pointer(&point)) {
            s_ui.screen_lock_drag_dx = point.x - s_ui.screen_lock_press_start.x;
            s_ui.screen_lock_drag_dy = point.y - s_ui.screen_lock_press_start.y;
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        lv_point_t point = { 0 };
        if (get_active_pointer(&point)) {
            s_ui.screen_lock_drag_dx = point.x - s_ui.screen_lock_press_start.x;
            s_ui.screen_lock_drag_dy = point.y - s_ui.screen_lock_press_start.y;
        }
        const int32_t abs_dx = abs_i32(s_ui.screen_lock_drag_dx);
        const int32_t abs_dy = abs_i32(s_ui.screen_lock_drag_dy);
        if (abs_dx <= SCREEN_LOCK_TAP_MAX_MOVE && abs_dy <= SCREEN_LOCK_TAP_MAX_MOVE) {
            screen_unlock_prompt_show();
        }
        s_ui.screen_lock_drag_dx = 0;
        s_ui.screen_lock_drag_dy = 0;
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        s_ui.screen_lock_drag_dx = 0;
        s_ui.screen_lock_drag_dy = 0;
    }
}

static void screen_unlock_update_from_pointer(void)
{
    if (!UI_FLAG(SCREEN_UNLOCK_DRAGGING) || !s_ui.screen_unlock_track) {
        return;
    }

    lv_point_t point = { 0 };
    if (!get_active_pointer(&point)) {
        return;
    }

    lv_area_t area = { 0 };
    lv_obj_get_coords(s_ui.screen_unlock_track, &area);
    const int32_t track_w = area.x2 - area.x1 + 1;
    const int32_t min_x = 4;
    const int32_t max_x = track_w - SCREEN_UNLOCK_KNOB_SIZE - 4;
    s_ui.screen_unlock_knob_x = clamp_i32(point.x - area.x1 - (SCREEN_UNLOCK_KNOB_SIZE / 2),
                                          min_x,
                                          max_x);
    lv_obj_set_x(s_ui.screen_unlock_knob, s_ui.screen_unlock_knob_x);
    lv_obj_set_width(s_ui.screen_unlock_fill,
                     s_ui.screen_unlock_knob_x + SCREEN_UNLOCK_KNOB_SIZE - min_x);
}

static uint8_t screen_unlock_progress_percent(void)
{
    if (!s_ui.screen_unlock_track) {
        return 0;
    }
    const int32_t min_x = 4;
    const int32_t max_x = lv_obj_get_width(s_ui.screen_unlock_track) -
                          SCREEN_UNLOCK_KNOB_SIZE - 4;
    const int32_t range = max_x - min_x;
    if (range <= 0) {
        return 0;
    }
    return (uint8_t)(((s_ui.screen_unlock_knob_x - min_x) * 100) / range);
}

static void screen_unlock_track_event_cb(lv_event_t *event)
{
    if (!UI_FLAG(SCREEN_LOCKED) || !UI_FLAG(SCREEN_UNLOCK_PROMPT_VISIBLE)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t point = { 0 };
        lv_area_t area = { 0 };
        if (!get_active_pointer(&point)) {
            return;
        }
        lv_obj_get_coords(s_ui.screen_unlock_track, &area);
        const bool starts_on_knob =
            point.x <= area.x1 + SCREEN_UNLOCK_KNOB_SIZE + SCREEN_UNLOCK_TOUCH_MARGIN;
        UI_SET_FLAG(SCREEN_UNLOCK_DRAGGING, starts_on_knob);
        if (starts_on_knob) {
            screen_unlock_update_from_pointer();
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        screen_unlock_update_from_pointer();
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        if (!UI_FLAG(SCREEN_UNLOCK_DRAGGING)) {
            return;
        }
        screen_unlock_update_from_pointer();
        const bool unlocked = screen_unlock_progress_percent() >=
                              SCREEN_UNLOCK_THRESHOLD_PERCENT;
        UI_SET_FLAG(SCREEN_UNLOCK_DRAGGING, false);
        if (unlocked) {
            screen_lock_exit();
        } else {
            screen_unlock_reset();
        }
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        screen_unlock_reset();
    }
}

static void screen_lock_create(lv_obj_t *screen)
{
    s_ui.screen_lock_guard = lv_obj_create(screen);
    clear_style(s_ui.screen_lock_guard);
    lv_obj_set_pos(s_ui.screen_lock_guard, 0, 0);
    lv_obj_set_size(s_ui.screen_lock_guard, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_opa(s_ui.screen_lock_guard, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.screen_lock_guard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.screen_lock_guard, screen_lock_guard_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.screen_lock_guard, screen_lock_guard_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.screen_lock_guard, screen_lock_guard_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.screen_lock_guard, screen_lock_guard_event_cb, LV_EVENT_PRESS_LOST, NULL);

    const int32_t card_w = clamp_i32(s_ui.width - 24, 200, 300);
    const int32_t card_h = SCREEN_UNLOCK_TRACK_H + 20;
    const int32_t card_x = (s_ui.width - card_w) / 2;
    const int32_t card_y = s_ui.height - card_h - (s_ui.width < s_ui.height ? 24 : 16);
    s_ui.screen_unlock_card = panel(s_ui.screen_lock_guard,
                                    card_x,
                                    card_y,
                                    card_w,
                                    card_h,
                                    COLOR_PANEL_ALT);
    lv_obj_set_style_radius(s_ui.screen_unlock_card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.screen_unlock_card, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.screen_unlock_card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.screen_unlock_card, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen_unlock_card, LV_OBJ_FLAG_CLICKABLE);

    const int32_t track_w = card_w - 20;
    s_ui.screen_unlock_track = panel(s_ui.screen_unlock_card,
                                     10,
                                     10,
                                     track_w,
                                     SCREEN_UNLOCK_TRACK_H,
                                     COLOR_BG);
    lv_obj_set_style_radius(s_ui.screen_unlock_track, SCREEN_UNLOCK_TRACK_H / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.screen_unlock_track, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.screen_unlock_track, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.screen_unlock_track, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.screen_unlock_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_ui.screen_unlock_track, SCREEN_UNLOCK_TOUCH_MARGIN);
    lv_obj_add_event_cb(s_ui.screen_unlock_track, screen_unlock_track_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.screen_unlock_track, screen_unlock_track_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.screen_unlock_track, screen_unlock_track_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.screen_unlock_track, screen_unlock_track_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_ui.screen_unlock_fill = panel(s_ui.screen_unlock_track,
                                    4,
                                    4,
                                    SCREEN_UNLOCK_KNOB_SIZE,
                                    SCREEN_UNLOCK_KNOB_SIZE,
                                    COLOR_ACCENT);
    lv_obj_set_style_radius(s_ui.screen_unlock_fill, SCREEN_UNLOCK_KNOB_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.screen_unlock_fill, LV_OPA_50, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen_unlock_fill, LV_OBJ_FLAG_CLICKABLE);

    s_ui.screen_unlock_hint = label(s_ui.screen_unlock_track,
                                    SCREEN_UNLOCK_KNOB_SIZE + 12,
                                    (SCREEN_UNLOCK_TRACK_H - 18) / 2,
                                    track_w - SCREEN_UNLOCK_KNOB_SIZE - 24,
                                    18,
                                    &lv_font_montserrat_14);
    lv_label_set_text(s_ui.screen_unlock_hint, "SLIDE >");
    lv_obj_set_style_text_align(s_ui.screen_unlock_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.screen_unlock_hint, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen_unlock_hint, LV_OBJ_FLAG_CLICKABLE);

    s_ui.screen_unlock_knob = panel(s_ui.screen_unlock_track,
                                    4,
                                    4,
                                    SCREEN_UNLOCK_KNOB_SIZE,
                                    SCREEN_UNLOCK_KNOB_SIZE,
                                    COLOR_ACCENT);
    lv_obj_set_style_radius(s_ui.screen_unlock_knob, SCREEN_UNLOCK_KNOB_SIZE / 2, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.screen_unlock_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *arrow = label(s_ui.screen_unlock_knob,
                            0,
                            (SCREEN_UNLOCK_KNOB_SIZE - 24) / 2,
                            SCREEN_UNLOCK_KNOB_SIZE,
                            24,
                            &lv_font_montserrat_24);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_BG, LV_PART_MAIN);
    lv_obj_clear_flag(arrow, LV_OBJ_FLAG_CLICKABLE);

    screen_unlock_reset();
    set_obj_hidden(s_ui.screen_unlock_card, true);
    set_obj_hidden(s_ui.screen_lock_guard, true);
    screen_lock_reapply();
}

static void page_scroll_event_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_ui.pages) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        UI_SET_FLAG(DRAGGING, true);
        if (!s_ui.page_scroll_gesture_active) {
            s_ui.page_scroll_gesture_active = true;
            s_ui.page_scroll_throw_frozen = false;
            s_ui.drag_start_page = s_ui.page;
            s_ui.drag_start_pages_x = lv_obj_get_scroll_x(s_ui.pages);
            s_ui.drag_pages_dx = 0;
            s_ui.drag_release_pages_dx = 0;
        }
        s_ui.drag_last_sample_log_ms = lv_tick_get();
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        s_ui.speed_art_draw_count = 0U;
        s_ui.speed_art_draw_max_us = 0U;
        s_ui.speed_art_draw_elapsed_us = 0U;
        s_ui.drag_diagnostic_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "[drag] scroll_begin scroll_x=%ld page=%d",
                 (long)s_ui.drag_start_pages_x,
                 (int)s_ui.drag_start_page);
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL) {
        invalidate_dashboard_viewport();
        if (s_ui.page_scroll_gesture_active && !s_ui.page_scroll_throw_frozen) {
            s_ui.drag_pages_dx = lv_obj_get_scroll_x(s_ui.pages) - s_ui.drag_start_pages_x;
        }
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

    if (code == LV_EVENT_SCROLL_THROW_BEGIN) {
        if (s_ui.page_scroll_gesture_active && !s_ui.page_scroll_throw_frozen) {
            s_ui.drag_pages_dx = lv_obj_get_scroll_x(s_ui.pages) - s_ui.drag_start_pages_x;
            s_ui.drag_release_pages_dx = s_ui.drag_pages_dx;
            s_ui.page_scroll_throw_frozen = true;
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
            ESP_LOGI(TAG, "[drag] throw_begin release_dx=%ld page=%d",
                     (long)s_ui.drag_release_pages_dx,
                     (int)s_ui.drag_start_page);
#endif
        }
        return;
    }

    if (code == LV_EVENT_SCROLL_END) {
        const int32_t scroll_x = lv_obj_get_scroll_x(s_ui.pages);
        const bool pointer_gesture = s_ui.page_scroll_gesture_active &&
                                     s_ui.page_scroll_throw_frozen;
        const esp_bms_lvgl_page_t stable_page = pointer_gesture
                                                    ? s_ui.drag_start_page
                                                    : s_ui.page;
        const int32_t stable_x = page_target_scroll_x(stable_page);
        const int32_t last_x = page_target_scroll_x(ESP_BMS_LVGL_PAGE_CAST);
        const esp_bms_lvgl_page_t raw_target = page_from_scroll_x(scroll_x);
        const int32_t raw_target_x = page_target_scroll_x(raw_target);
        int32_t target_x = raw_target_x;
        if (pointer_gesture) {
            const int32_t trigger_px = s_ui.width / 5;
            if (s_ui.drag_release_pages_dx >= trigger_px) {
                target_x = stable_x + s_ui.width < last_x ? stable_x + s_ui.width : last_x;
            } else if (s_ui.drag_release_pages_dx <= -trigger_px) {
                target_x = stable_x > s_ui.width ? stable_x - s_ui.width : 0;
            } else {
                target_x = stable_x;
            }
        }
        const esp_bms_lvgl_page_t target = page_from_scroll_x(target_x);
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        const int full_invalidate_enabled =
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE
            1;
#else
            0;
#endif
        ESP_LOGI(TAG,
                 "[drag] scroll_end scroll_x=%ld release_dx=%ld gesture=%d raw_target=%d target=%d",
                 (long)scroll_x,
                 (long)s_ui.drag_release_pages_dx,
                 pointer_gesture ? 1 : 0,
                 (int)raw_target,
                 (int)target);
        const int64_t diagnostic_elapsed_us = esp_timer_get_time() -
                                              s_ui.drag_diagnostic_start_us;
        ESP_LOGI(TAG,
                 "[drag] perf elapsed_ms=%lld speed_art_draws=%lu draw_us=%llu "
                 "draw_max_us=%lu heap_free=%u heap_min=%u heap_largest=%u full_invalidate=%d",
                 (long long)(diagnostic_elapsed_us / 1000),
                 (unsigned long)s_ui.speed_art_draw_count,
                 (unsigned long long)s_ui.speed_art_draw_elapsed_us,
                 (unsigned long)s_ui.speed_art_draw_max_us,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
                 full_invalidate_enabled);
#endif
        s_ui.page_scroll_gesture_active = false;
        s_ui.page_scroll_throw_frozen = false;
        s_ui.drag_pages_dx = 0;
        s_ui.drag_release_pages_dx = 0;
        s_ui.drag_start_page = target;
        UI_SET_FLAG(DRAGGING, false);
        s_ui.page = target;
        if (scroll_x != target_x) {
            UI_SET_FLAG(SETTLING, true);
            lv_obj_scroll_to_x(s_ui.pages, target_x, LV_ANIM_ON);
            return;
        }
        UI_SET_FLAG(SETTLING, false);
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
        s_ui.ap_state = label(s_ui.header, 152, 2, 80, 16, &lv_font_montserrat_14);
    } else {
        s_ui.gps_state = label(s_ui.header, 48, 2, 50, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 104, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.ap_state = label(s_ui.header, 166, 2, 148, 16, &lv_font_montserrat_14);
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
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_THROW_BEGIN, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.pages, quick_pull_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_ui.battery_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.battery_page);
    lv_obj_set_pos(s_ui.battery_page, 0, 0);
    lv_obj_set_size(s_ui.battery_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.battery_page, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.battery_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.battery_page, LV_OBJ_FLAG_SNAPPABLE);

    s_ui.controller_page_enabled = false;
    s_ui.controller_page = NULL;

    s_ui.gps_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.gps_page);
    lv_obj_set_pos(s_ui.gps_page, s_ui.width, 0);
    lv_obj_set_size(s_ui.gps_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.gps_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.gps_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.gps_page, LV_OBJ_FLAG_SNAPPABLE);
    snprintf(s_ui.gps_speed_buf, sizeof(s_ui.gps_speed_buf), "-");
    snprintf(s_ui.gps_speed_unit_buf, sizeof(s_ui.gps_speed_unit_buf), "km/h");
    snprintf(s_ui.gps_uptime_buf, sizeof(s_ui.gps_uptime_buf), "--:--");
    s_ui.speed_soc_buf[0] = '\0';
    snprintf(s_ui.speed_consumption_buf, sizeof(s_ui.speed_consumption_buf), "-- Wh/km");
    s_ui.speed_controller_temp_buf[0] = '\0';
    s_ui.speed_motor_temp_buf[0] = '\0';
    snprintf(s_ui.speed_gear_buf, sizeof(s_ui.speed_gear_buf), "1");
    memset(s_ui.speed_scale_buf, 0, sizeof(s_ui.speed_scale_buf));
    create_gps_dashboard();

    s_ui.cast_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.cast_page);
    lv_obj_set_pos(s_ui.cast_page, s_ui.width * 2, 0);
    lv_obj_set_size(s_ui.cast_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.cast_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.cast_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.cast_page, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_t *cast_title = label(s_ui.cast_page, 0, portrait ? 28 : 16, s_ui.width,
                                 settings_zh_16.line_height, &settings_zh_16);
    lv_label_set_text(cast_title, "扫码投屏");
    lv_obj_set_style_text_align(cast_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(cast_title, COLOR_ACCENT, LV_PART_MAIN);
#if LV_USE_QRCODE
    s_ui.cast_qr = lv_qrcode_create(s_ui.cast_page);
    if (s_ui.cast_qr) {
        const int32_t cast_qr_size = portrait ? 132 : 112;
        lv_qrcode_set_size(s_ui.cast_qr, cast_qr_size);
        lv_qrcode_set_dark_color(s_ui.cast_qr, COLOR_BG);
        lv_qrcode_set_light_color(s_ui.cast_qr, COLOR_WHITE);
        lv_qrcode_set_quiet_zone(s_ui.cast_qr, true);
        lv_obj_align(s_ui.cast_qr, LV_ALIGN_CENTER, 0, portrait ? 55 : 44);
    }
#endif

    if (portrait) {
        lv_obj_t *soc_panel = dashboard_panel(s_ui.battery_page,
                                              8,
                                              8,
                                              108,
                                              112,
                                              COLOR_DASHBOARD_SOC_PANEL,
                                              COLOR_DASHBOARD_SOC_BORDER);
        s_ui.soc = label(soc_panel, 4, 8, 100, 30, &lv_font_montserrat_24);
        dashboard_battery_icon(soc_panel, 19, 43, 66, 22);
        s_ui.capacity = label(soc_panel, 4, 76, 100, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = dashboard_panel(s_ui.battery_page,
                                               124,
                                               8,
                                               108,
                                               112,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        s_ui.pack_voltage = label(pack_panel, 4, 12, 100, 34, &lv_font_montserrat_28);
        dashboard_separator(pack_panel, 8, 52, 92);
        s_ui.current = label(pack_panel, 4, 58, 100, 34, &lv_font_montserrat_28);

        lv_obj_t *bms_panel = dashboard_panel(s_ui.battery_page,
                                              8,
                                              128,
                                              108,
                                              120,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);
        s_ui.bms_error = label(bms_panel, 4, 4, 100, 112, &lv_font_montserrat_14);

        lv_obj_t *cell_panel = dashboard_panel(s_ui.battery_page,
                                               124,
                                               128,
                                               108,
                                               120,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        for (uint8_t index = 0; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
            const int32_t row_y = 6 + ((int32_t)index * 26);
            lv_obj_t *key = dashboard_cell_key(cell_panel, 11, row_y + 2, index);
            if (index == 0U) {
                s_ui.cell_stats = key;
            }
            s_ui.cell_stat_values[index] = label(cell_panel, 49, row_y, 53, 20, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.cell_stat_values[index], COLOR_DASHBOARD_VALUE, LV_PART_MAIN);
            if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
                dashboard_separator(cell_panel, 8, row_y + 23, 92);
            }
        }

        lv_obj_t *temp_panel = dashboard_panel(s_ui.battery_page,
                                               8,
                                               256,
                                               content_w,
                                               56,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        const int32_t temp_col_w = content_w / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
        for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            const int32_t col_x = (int32_t)index * temp_col_w;
            lv_obj_t *key = label(temp_panel, col_x, 2, temp_col_w, 18, &lv_font_montserrat_14);
            lv_label_set_text(key, DASHBOARD_TEMP_KEYS[index]);
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (index == 0U) {
                s_ui.temperature = key;
            }
            dashboard_thermometer_icon(temp_panel, col_x + (temp_col_w / 2), 18);
            s_ui.temperature_values[index] = label(temp_panel, col_x, 34, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.temperature_values[index], COLOR_DASHBOARD_VALUE, LV_PART_MAIN);
        }
    } else {
        lv_obj_t *soc_panel = dashboard_panel(s_ui.battery_page,
                                              8,
                                              8,
                                              148,
                                              84,
                                              COLOR_DASHBOARD_SOC_PANEL,
                                              COLOR_DASHBOARD_SOC_BORDER);
        s_ui.soc = label(soc_panel, 4, 3, 140, 30, &lv_font_montserrat_24);
        dashboard_battery_icon(soc_panel, 34, 35, 76, 19);
        s_ui.capacity = label(soc_panel, 4, 58, 140, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = dashboard_panel(s_ui.battery_page,
                                               164,
                                               8,
                                               148,
                                               84,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        s_ui.pack_voltage = label(pack_panel, 4, 3, 140, 34, &lv_font_montserrat_28);
        dashboard_separator(pack_panel, 10, 40, 128);
        s_ui.current = label(pack_panel, 4, 44, 140, 34, &lv_font_montserrat_28);

        lv_obj_t *bms_panel = dashboard_panel(s_ui.battery_page,
                                              8,
                                              100,
                                              148,
                                              70,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);
        s_ui.bms_error = label(bms_panel, 4, 4, 140, 62, &lv_font_montserrat_14);

        lv_obj_t *cell_panel = dashboard_panel(s_ui.battery_page,
                                               164,
                                               100,
                                               148,
                                               70,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        for (uint8_t index = 0; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
            const int32_t row_y = 2 + ((int32_t)index * 16);
            lv_obj_t *key = dashboard_cell_key(cell_panel, 20, row_y, index);
            if (index == 0U) {
                s_ui.cell_stats = key;
            }
            s_ui.cell_stat_values[index] = label(cell_panel, 74, row_y, 62, 16, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.cell_stat_values[index], COLOR_DASHBOARD_VALUE, LV_PART_MAIN);
            if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
                dashboard_separator(cell_panel, 12, row_y + 15, 124);
            }
        }

        lv_obj_t *temp_panel = dashboard_panel(s_ui.battery_page,
                                               8,
                                               178,
                                               304,
                                               54,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        const int32_t temp_col_w = 304 / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
        const int32_t temp_left = (304 - (temp_col_w * (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT)) / 2;
        for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            const int32_t col_x = temp_left + ((int32_t)index * temp_col_w);
            lv_obj_t *key = label(temp_panel, col_x, 1, temp_col_w, 18, &lv_font_montserrat_14);
            lv_label_set_text(key, DASHBOARD_TEMP_KEYS[index]);
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (index == 0U) {
                s_ui.temperature = key;
            }
            dashboard_thermometer_icon(temp_panel, col_x + (temp_col_w / 2), 17);
            s_ui.temperature_values[index] = label(temp_panel, col_x, 32, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.temperature_values[index], COLOR_DASHBOARD_VALUE, LV_PART_MAIN);
        }
    }
    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.current, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.soc, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.capacity, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.pack_voltage, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.current, COLOR_WHITE, LV_PART_MAIN);

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
    lv_obj_add_flag(s_ui.settings_root, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(s_ui.settings_root);

    s_ui.settings_carousel = lv_obj_create(s_ui.settings_root);
    clear_style(s_ui.settings_carousel);
    lv_obj_set_pos(s_ui.settings_carousel, 0, 0);
    lv_obj_set_size(s_ui.settings_carousel, s_ui.width, settings_h);
    lv_obj_set_style_pad_top(s_ui.settings_carousel,
                             SETTINGS_DETAIL_HEADER_H,
                             LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.settings_carousel, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_carousel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_carousel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ui.settings_carousel, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                              LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(s_ui.settings_carousel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.settings_carousel, LV_SCROLLBAR_MODE_OFF);
    settings_add_swipe_handlers(s_ui.settings_carousel);
    lv_obj_add_event_cb(s_ui.settings_carousel,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL_BEGIN,
                        NULL);
    lv_obj_add_event_cb(s_ui.settings_carousel,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL,
                        NULL);
    lv_obj_add_event_cb(s_ui.settings_carousel,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL_END,
                        NULL);

    const int32_t row_h = portrait ? SETTINGS_LIST_ROW_H_PORTRAIT : SETTINGS_LIST_ROW_H_LANDSCAPE;
    const int32_t list_x = 12;
    const int32_t list_w = s_ui.width - (list_x * 2);
    lv_obj_t *list_card = settings_list_card(s_ui.settings_carousel,
                                             list_x,
                                             SETTINGS_LIST_PAD_Y,
                                             list_w,
                                             row_h,
                                             SETTINGS_OPTION_COUNT);
    for (uint32_t index = 0; index < SETTINGS_OPTION_COUNT; ++index) {
        settings_option_card(list_card,
                             0,
                             (int32_t)index * row_h,
                             list_w,
                             row_h,
                             &SETTINGS_OPTIONS[index]);
    }
    s_ui.settings_detail = lv_obj_create(s_ui.settings_page);
    clear_style(s_ui.settings_detail);
    lv_obj_set_pos(s_ui.settings_detail, 0, 0);
    lv_obj_set_size(s_ui.settings_detail, s_ui.width, settings_h);
    lv_obj_set_style_pad_top(s_ui.settings_detail,
                             SETTINGS_DETAIL_HEADER_H,
                             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ui.settings_detail, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.settings_detail, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_detail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_detail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ui.settings_detail, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                            LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(s_ui.settings_detail, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.settings_detail, LV_SCROLLBAR_MODE_AUTO);
    settings_add_swipe_handlers(s_ui.settings_detail);
    lv_obj_add_event_cb(s_ui.settings_detail,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL_BEGIN,
                        NULL);
    lv_obj_add_event_cb(s_ui.settings_detail,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL,
                        NULL);
    lv_obj_add_event_cb(s_ui.settings_detail,
                        settings_navigation_scroll_event_cb,
                        LV_EVENT_SCROLL_END,
                        NULL);
    lv_obj_add_flag(s_ui.settings_detail, LV_OBJ_FLAG_HIDDEN);

    s_ui.settings_detail_header = panel(s_ui.settings_page,
                                        0,
                                        0,
                                        s_ui.width,
                                        SETTINGS_DETAIL_HEADER_H,
                                        COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(s_ui.settings_detail_header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.settings_detail_header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.settings_detail_header,
                                  COLOR_SETTINGS_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_side(s_ui.settings_detail_header,
                                 LV_BORDER_SIDE_BOTTOM,
                                 LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.settings_detail_header, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_detail_header, LV_OBJ_FLAG_FLOATING);

    lv_obj_t *detail_back = panel(s_ui.settings_detail_header,
                                  4,
                                  3,
                                  48,
                                  SETTINGS_DETAIL_HEADER_H - 6,
                                  COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(detail_back, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(detail_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(detail_back, 4);
    lv_obj_add_flag(detail_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(detail_back,
                        settings_detail_back_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *detail_back_icon = label(detail_back,
                                       0,
                                       4,
                                       48,
                                       SETTINGS_DETAIL_HEADER_H - 10,
                                       &lv_font_montserrat_24);
    lv_label_set_text(detail_back_icon, "<");
    lv_obj_set_style_text_align(detail_back_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(detail_back_icon, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);

    s_ui.settings_detail_title = label(s_ui.settings_detail_header,
                                       56,
                                       7,
                                       s_ui.width - 112,
                                       SETTINGS_DETAIL_HEADER_H - 12,
                                       &settings_zh_16);
    lv_label_set_text(s_ui.settings_detail_title, "设置");
    lv_obj_set_style_text_align(s_ui.settings_detail_title,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_detail_title,
                                COLOR_SETTINGS_TEXT,
                                LV_PART_MAIN);
    set_obj_hidden(s_ui.settings_detail_header, true);

    s_ui.settings_detail_edge_zone = lv_obj_create(s_ui.settings_page);
    clear_style(s_ui.settings_detail_edge_zone);
    lv_obj_set_pos(s_ui.settings_detail_edge_zone,
                   0,
                   SETTINGS_DETAIL_HEADER_H);
    lv_obj_set_size(s_ui.settings_detail_edge_zone,
                    SETTINGS_SWIPE_EDGE_WIDTH,
                    settings_h - SETTINGS_DETAIL_HEADER_H);
    lv_obj_set_style_bg_opa(s_ui.settings_detail_edge_zone,
                            LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_detail_edge_zone,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_ui.settings_detail_edge_zone,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    settings_add_swipe_handlers(s_ui.settings_detail_edge_zone);
    set_obj_hidden(s_ui.settings_detail_edge_zone, true);

    s_ui.settings_swipe_indicator = panel(lv_layer_top(),
                                          -SETTINGS_SWIPE_INDICATOR_SIZE,
                                          (s_ui.height - SETTINGS_SWIPE_INDICATOR_SIZE) / 2,
                                          SETTINGS_SWIPE_INDICATOR_SIZE,
                                          SETTINGS_SWIPE_INDICATOR_SIZE,
                                          COLOR_SETTINGS_ACCENT);
    lv_obj_set_style_radius(s_ui.settings_swipe_indicator,
                            SETTINGS_SWIPE_INDICATOR_SIZE / 2,
                            LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.settings_swipe_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_swipe_indicator, LV_OPA_80, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.settings_swipe_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back_icon = label(s_ui.settings_swipe_indicator,
                                0,
                                (SETTINGS_SWIPE_INDICATOR_SIZE - 24) / 2,
                                SETTINGS_SWIPE_INDICATOR_SIZE,
                                24,
                                &lv_font_montserrat_24);
    lv_label_set_text(back_icon, "<");
    lv_obj_set_style_text_align(back_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_icon, COLOR_WHITE, LV_PART_MAIN);
    quick_symbol_icon_recenter(back_icon,
                               SETTINGS_SWIPE_INDICATOR_SIZE,
                               SETTINGS_SWIPE_INDICATOR_SIZE,
                               "<",
                               &lv_font_montserrat_24);
    set_obj_hidden(s_ui.settings_swipe_indicator, true);
    s_ui.setup_ap_control_row = NULL;
    s_ui.setup_ap_info = NULL;
    s_ui.setup_ap_qr_panel = NULL;
    s_ui.setup_ap_qr = NULL;
    s_ui.setup_ap_qr_ready = false;
    s_ui.setup_ap_qr_encode_attempted = false;
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
    screen_lock_create(screen);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
}

static esp_err_t rebuild_screen_if_needed(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(s_ui.display, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    const int32_t width = lv_display_get_horizontal_resolution(s_ui.display);
    const int32_t height = lv_display_get_vertical_resolution(s_ui.display);
    if (width == s_ui.width && height == s_ui.height) {
        return ESP_OK;
    }

    lv_obj_t *old_root = s_ui.root;
    esp_bms_lvgl_page_t page = s_ui.page;
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER) {
        page = ESP_BMS_LVGL_PAGE_GPS;
    }
    const esp_bms_lvgl_action_event_t pending_event = s_ui.pending_event;
    const bool settings_visible = s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    const bool screen_locked = UI_FLAG(SCREEN_LOCKED);
    const bool rotate_toast_active = UI_FLAG(QUICK_ROTATE_TOAST_ACTIVE);
    const uint8_t quick_level_position = s_ui.quick_level_position;
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
    screen_unlock_timer_cancel();
    settings_bms_popup_close();
    settings_restore_popup_close();
    if (s_ui.settings_swipe_indicator) {
        lv_obj_delete(s_ui.settings_swipe_indicator);
        s_ui.settings_swipe_indicator = NULL;
    }
    if (old_root) {
        lv_obj_delete(old_root);
    }

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
    if (settings_visible) {
        show_settings_view();
    } else {
        show_dashboard_view();
    }
    if (rotate_toast_active) {
        quick_rotate_toast_show();
    }
    screen_lock_reapply();
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is required");
    ESP_RETURN_ON_FALSE(!UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI already initialized");
    create_screen(display);
    UI_SET_FLAG(INITIALIZED, true);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_ERROR(rebuild_screen_if_needed(snapshot), TAG, "rebuild UI failed");
    if (UI_FLAG(DRAGGING) || UI_FLAG(SETTLING)) {
        defer_dashboard_snapshot(snapshot);
        return ESP_OK;
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
    ESP_RETURN_ON_FALSE(s_ui.settings_system_view ==
                            (uint8_t)SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION,
                        ESP_ERR_INVALID_STATE, TAG, "touch calibration view is not active");
    set_obj_hidden(s_ui.settings_calibration_target, true);
    label_set_text_if_changed(s_ui.settings_calibration_status,
                              success ? "校准成功，返回系统设置" : "校准失败，返回后重试");
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_FALSE(page == ESP_BMS_LVGL_PAGE_BATTERY ||
                            page == ESP_BMS_LVGL_PAGE_CONTROLLER ||
                            page == ESP_BMS_LVGL_PAGE_GPS ||
                            page == ESP_BMS_LVGL_PAGE_CAST,
                        ESP_ERR_INVALID_ARG, TAG, "invalid page");

    move_to_page(page, animated);
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
        return ESP_BMS_LVGL_DATA_SOURCE_NONE;
    case ESP_BMS_LVGL_PAGE_BATTERY:
    default:
        return ESP_BMS_LVGL_DATA_SOURCE_BMS;
    }
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
