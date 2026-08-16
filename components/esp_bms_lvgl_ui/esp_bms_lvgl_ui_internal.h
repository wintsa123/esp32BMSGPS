/*
 * esp_bms_lvgl_ui 组件内部共享头（拆分自 esp_bms_lvgl_ui.c）。
 * 仅供组件内 .c 文件包含，不属于公共 API。
 */
#pragma once

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
#include "misc/cache/instance/lv_image_cache.h"
#include "widgets/canvas/lv_canvas.h"
#include "widgets/line/lv_line.h"

static const char *TAG = "bms_lvgl_ui";

LV_FONT_DECLARE(bluetoothon);
LV_FONT_DECLARE(wlanJZ);

#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
LV_FONT_DECLARE(controller_digits_72);
#endif
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
LV_FONT_DECLARE(fireblade_digits_64);
LV_FONT_DECLARE(fireblade_info_digits_12);
LV_FONT_DECLARE(fireblade_info_units_8);
LV_FONT_DECLARE(fireblade_scale_digits_14);
#endif
LV_FONT_DECLARE(settings_zh_10);
LV_FONT_DECLARE(settings_zh_13);
LV_FONT_DECLARE(settings_zh_16);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID || \
    ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
LV_FONT_DECLARE(media_zh_13);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) || ESP_BMS_LVGL_UI_SIMULATOR
#define SETTINGS_S3_FONT_ENABLED 1
LV_FONT_DECLARE(settings_zh_18);
#else
#define SETTINGS_S3_FONT_ENABLED 0
#endif

#ifndef CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
#if ESP_BMS_LVGL_UI_SIMULATOR
#define CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING 1
#else
#define CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING 0
#endif
#endif

#define QUICK_PANEL_BUTTON_COUNT \
    (3 + ((ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER) ? 1 : 0) + \
     (ESP_BMS_FEATURE_NETWORK ? 1 : 0) + \
     ((ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER) ? 1 : 0))
#define QUICK_PANEL_GRID_COLS 4
#define QUICK_PANEL_GRID_ROWS 2
#define QUICK_PANEL_GRID_SLOT_COUNT (QUICK_PANEL_GRID_COLS * QUICK_PANEL_GRID_ROWS)
#define QUICK_PANEL_LEVEL_COUNT \
    (CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING + ESP_BMS_FEATURE_AUDIO)
#define QUICK_PANEL_CONTROL_COUNT (QUICK_PANEL_BUTTON_COUNT + QUICK_PANEL_LEVEL_COUNT)
#define QUICK_EDIT_BUTTON_SIZE 28
#define QUICK_EDIT_BUTTON_SIZE_S3 36
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define MEDIA_HID_PAGE_ENABLED \
    (ESP_BMS_FEATURE_BLE_MEDIA_HID || ESP_BMS_FEATURE_CLASSIC_MEDIA_HID)
#define MUSIC_PAGE_ENABLED MEDIA_HID_PAGE_ENABLED
#if MEDIA_HID_PAGE_ENABLED
#define MUSIC_CONTROL_COUNT 5U
#else
#define MUSIC_CONTROL_COUNT 0U
#endif
#define SETTINGS_BLE_STATUS_TEXT_LEN 24U
#define SETTINGS_BLE_ROW_TEXT_LEN (ESP_BMS_BMS_SCAN_NAME_LEN + 16U)
#define SETTINGS_BLE_DIRECT_CANDIDATE_COUNT 6U
#define SETTINGS_BLE_PRIMARY_CANDIDATE_COUNT 5U
#define SETTINGS_BLE_MAX_VISIBLE_ROWS 7U
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
#define SETTINGS_DETAIL_HEADER_H_BASE 38
#define SETTINGS_NAV_SCROLL_THRESHOLD 12
#define SETTINGS_NAV_ANIM_MS 160
#define SETTINGS_LIST_ROW_H_PORTRAIT_BASE 52
#define SETTINGS_LIST_ROW_H_LANDSCAPE_BASE 52
#define SETTINGS_DETAIL_ROW_H_PORTRAIT_BASE 64
#define SETTINGS_DETAIL_ROW_H_LANDSCAPE_BASE 56
#define SETTINGS_CHOICE_ROW_H_PORTRAIT_BASE 56
#define SETTINGS_CHOICE_ROW_H_LANDSCAPE_BASE 48
#define SETTINGS_LIST_MARGIN_X_BASE 8
#define SETTINGS_LIST_PAD_Y_BASE 4
#define SETTINGS_DETAIL_HEADER_H settings_scaled_px(SETTINGS_DETAIL_HEADER_H_BASE)
#define SETTINGS_LIST_ROW_H_PORTRAIT settings_scaled_px(SETTINGS_LIST_ROW_H_PORTRAIT_BASE)
#define SETTINGS_LIST_ROW_H_LANDSCAPE settings_scaled_px(SETTINGS_LIST_ROW_H_LANDSCAPE_BASE)
#define SETTINGS_DETAIL_ROW_H_PORTRAIT settings_scaled_px(SETTINGS_DETAIL_ROW_H_PORTRAIT_BASE)
#define SETTINGS_DETAIL_ROW_H_LANDSCAPE settings_scaled_px(SETTINGS_DETAIL_ROW_H_LANDSCAPE_BASE)
#define SETTINGS_CHOICE_ROW_H_PORTRAIT settings_scaled_px(SETTINGS_CHOICE_ROW_H_PORTRAIT_BASE)
#define SETTINGS_CHOICE_ROW_H_LANDSCAPE settings_scaled_px(SETTINGS_CHOICE_ROW_H_LANDSCAPE_BASE)
#define SETTINGS_LIST_MARGIN_X settings_scaled_px(SETTINGS_LIST_MARGIN_X_BASE)
#define SETTINGS_LIST_PAD_Y settings_scaled_px(SETTINGS_LIST_PAD_Y_BASE)
#define SETTINGS_BOOT_PREVIEW_TIMER_MS 20U
#define SETTINGS_BOOT_PREVIEW_DURATION_MS 3000U
#define SETTINGS_BOOT_PREVIEW_READY_HOLD_MS 300U
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
#if ESP_BMS_LVGL_UI_SIMULATOR
#define SETTINGS_ABOUT_DEVICE_MODEL "ESP32-S3 BMS GPS"
#define SETTINGS_ABOUT_DISPLAY_MODEL "ST7796U"
#else
#define SETTINGS_ABOUT_DEVICE_MODEL "ESP32 BMS GPS"
#define SETTINGS_ABOUT_DISPLAY_MODEL "ST7789"
#endif
#define SCREEN_LOCK_PROMPT_TIMEOUT_MS 3000U
#define SCREEN_LOCK_TAP_MAX_MOVE 14
#define SCREEN_UNLOCK_THRESHOLD_PERCENT 85
#define SCREEN_UNLOCK_TRACK_H 56
#define SCREEN_UNLOCK_KNOB_SIZE 48
#define SCREEN_UNLOCK_TOUCH_MARGIN 20
#define QUICK_LOCK_ICON_W 24
#define QUICK_LOCK_ICON_H 28
#define DASHBOARD_CELL_STAT_COUNT 4U
#define SPEED_DASHBOARD_SEGMENT_COUNT 48U
#define SPEED_DASHBOARD_DANGER_START \
    ((SPEED_DASHBOARD_SEGMENT_COUNT * 7U) / 8U)
#define SPEED_DASHBOARD_MINOR_TICK_STEP (SPEED_DASHBOARD_SEGMENT_COUNT / 16U)
#define SPEED_DASHBOARD_MAJOR_TICK_STEP (SPEED_DASHBOARD_SEGMENT_COUNT / 4U)
#define SPEED_DASHBOARD_BAND_OVERLAP 4
#define SPEED_DASHBOARD_SCALE_LABEL_COUNT 6U
#define FIREBLADE_ARC_START_ANGLE 120
#define FIREBLADE_ARC_SWEEP_ANGLE 234
#define FIREBLADE_SPEED_TICK_COUNT 18U
#define FIREBLADE_SCALE_LABEL_COUNT ((FIREBLADE_SPEED_TICK_COUNT / 2U) + 1U)
#define FIREBLADE_GEAR_RADIUS 29
#define BOOT_CHARGE_SEGMENT_COUNT 10U
#define BOOT_BRAND_PART_COUNT 12U
#define OTA_BAR_SEGMENT_COUNT 20U
#define BOOT_GAUGE_BMW_RR_PERCENT 32U
#define BOOT_GAUGE_BRAND_INTRO_PERCENT 40U
#define DASHBOARD_CELL_KEY_BITMAP_W 28
#define DASHBOARD_CELL_KEY_BITMAP_H 16
#define DASHBOARD_CELL_KEY_BITMAP_BYTES \
    (((DASHBOARD_CELL_KEY_BITMAP_W * DASHBOARD_CELL_KEY_BITMAP_H) + 7U) / 8U)
#define DASHBOARD_STATIC_CACHE_WIDTH 480U
#define DASHBOARD_STATIC_CACHE_HEIGHT 320U
#define DASHBOARD_STATIC_CACHE_BYTES \
    (DASHBOARD_STATIC_CACHE_WIDTH * DASHBOARD_STATIC_CACHE_HEIGHT * sizeof(uint16_t))
#define SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH 320U
#define SPEED_DASHBOARD_STATIC_LANDSCAPE_HEIGHT 240U
#define SPEED_DASHBOARD_STATIC_LANDSCAPE_BYTES \
    (SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH * SPEED_DASHBOARD_STATIC_LANDSCAPE_HEIGHT * sizeof(uint16_t))

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
_Static_assert(DASHBOARD_STATIC_CACHE_BYTES == 307200U,
               "480x320 RGB565 dashboard cache size changed");
_Static_assert(SPEED_DASHBOARD_STATIC_LANDSCAPE_BYTES == 153600U,
               "320x240 RGB565 speed dashboard image size changed");
_Static_assert(SPEED_DASHBOARD_SEGMENT_COUNT < 64U,
               "speed dashboard segments must fit the render signature");
_Static_assert((SPEED_DASHBOARD_SEGMENT_COUNT % 16U) == 0U,
               "speed dashboard tick spacing requires 16 subdivisions");
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
    UI_STATE_FLAG_OTA_ACTIVE = UINT32_C(1) << 29,
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

typedef enum {
    SETTINGS_DASHBOARD_VIEW_ROOT = 0,
    SETTINGS_DASHBOARD_VIEW_STYLE_LIST,
    SETTINGS_DASHBOARD_VIEW_SPEED_UNIT_LIST,
    SETTINGS_DASHBOARD_VIEW_SPEED_SOURCE_LIST,
} settings_dashboard_view_t;

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

typedef struct {
    void *pixels;
    lv_draw_buf_t draw_buf;
    lv_obj_t *image;
    bool active;
} dashboard_static_cache_t;

typedef struct {
    lv_obj_t *obj;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} boot_brand_part_t;

typedef struct {
    int32_t margin;
    int32_t gap;
    int32_t content_w;
    int32_t top_y;
    int32_t left_w;
    int32_t right_x;
    int32_t right_w;
    int32_t electrical_h;
    int32_t temp_y;
    int32_t temp_h;
    int32_t cell_y;
    int32_t cell_h;
    int32_t safety_y;
    int32_t safety_h;
} bms_native_layout_t;




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
_Static_assert(sizeof(esp_bms_speed_dashboard_style_t) == 4 &&
                   ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR == 0 &&
                   ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER == 1 &&
                   ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE == 2,
               "esp_bms_speed_dashboard_style_t ABI changed; update runtime consumers too");
_Static_assert(sizeof(esp_bms_gps_module_state_t) == 4 &&
                   ESP_BMS_GPS_MODULE_PROBING == 0 &&
                   ESP_BMS_GPS_MODULE_AVAILABLE == 1 &&
                   ESP_BMS_GPS_MODULE_UNAVAILABLE == 2,
               "esp_bms_gps_module_state_t ABI changed; update runtime consumers too");
_Static_assert(sizeof(esp_bms_boot_animation_style_t) == 4 &&
                   ESP_BMS_BOOT_ANIMATION_CHARGE == 0 &&
                   ESP_BMS_BOOT_ANIMATION_GAUGE_SWEEP == 1 &&
                   ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR == 1 &&
                   ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE == 2,
               "esp_bms_boot_animation_style_t ABI changed; update runtime consumers too");
_Static_assert(sizeof(esp_bms_dashboard_snapshot_t) == 1652,
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
_Static_assert(ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE == 30,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE == 31,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE == 32,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG == 35,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS == 36 &&
                   ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP == 39 &&
                   ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE == 40,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");

typedef struct {
    lv_display_t *display;
    lv_obj_t *root;
    lv_obj_t *staging_screen;
    lv_obj_t *header;
    lv_obj_t *pages;
    lv_obj_t *battery_page;
    lv_obj_t *gps_page;
    lv_obj_t *cast_page;
    lv_obj_t *music_page;
    lv_obj_t *page_transition_battery;
    lv_obj_t *page_transition_gps;
    lv_obj_t *page_transition_cast;
    lv_obj_t *page_transition_music;
    lv_obj_t *page_transition_battery_card;
    lv_obj_t *page_transition_gps_card;
    lv_obj_t *page_transition_cast_card;
    lv_obj_t *page_transition_music_card;
    lv_obj_t *page_transition_anim_card;
    lv_obj_t *cast_qr;
    lv_obj_t *music_status;
    lv_obj_t *music_title;
    lv_obj_t *music_controls[5];
    lv_obj_t *music_control_icons[5];
    lv_obj_t *music_control_captions[5];
    bool music_play_paused;
    lv_obj_t *controller_page;
    dashboard_static_cache_t battery_static_cache;
    dashboard_static_cache_t fireblade_static_cache;
    dashboard_static_cache_t speed_static_cache;
    lv_obj_t *boot_overlay;
    lv_obj_t *boot_status;
    lv_obj_t *boot_progress;
    lv_obj_t *boot_scan_line;
    lv_obj_t *boot_brand_mark;
    lv_obj_t *boot_rr_mark;
    boot_brand_part_t boot_brand_parts[BOOT_BRAND_PART_COUNT];
    lv_obj_t *boot_charge_segments[BOOT_CHARGE_SEGMENT_COUNT];
    lv_obj_t *ota_overlay;
    lv_obj_t *ota_status;
    lv_obj_t *ota_percent;
    lv_obj_t *ota_warning;
    lv_obj_t *ota_bar_segments[OTA_BAR_SEGMENT_COUNT];
    lv_obj_t *settings_page;
    lv_obj_t *settings_root;
    lv_obj_t *settings_carousel;
    lv_obj_t *settings_detail;
    lv_obj_t *settings_detail_header;
    lv_obj_t *settings_detail_title;
    lv_obj_t *settings_detail_edge_zone;
    lv_obj_t *settings_boot_preview_button;
    lv_timer_t *settings_boot_preview_timer;
    lv_timer_t *settings_calibration_start_timer;
    uint32_t settings_boot_preview_started_ms;
    lv_obj_t *settings_bms_popup;
    lv_obj_t *settings_bms_ble_status;
    lv_obj_t *settings_bms_ble_empty;
    lv_obj_t *settings_bms_ble_list;
    lv_obj_t *settings_preset_range_rollers[4];
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
    char settings_bms_ble_status_text[SETTINGS_BLE_STATUS_TEXT_LEN];
    char settings_bms_ble_empty_text[SETTINGS_BLE_STATUS_TEXT_LEN];
    char settings_bms_ble_list_text[SETTINGS_BLE_MAX_VISIBLE_ROWS * SETTINGS_BLE_ROW_TEXT_LEN];
    lv_obj_t *settings_swipe_indicator;
    bool settings_bms_ble_popup_open;
    bool settings_ble_more_page;
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
    lv_obj_t *soc_arc;
    lv_obj_t *soc_battery_level;
    lv_obj_t *pack_voltage;
    lv_obj_t *pack_voltage_unit;
    lv_obj_t *current;
    lv_obj_t *current_unit;
    lv_obj_t *capacity;
    lv_obj_t *bms_running_time;
    lv_obj_t *bms_cycle_capacity;
    lv_obj_t *cell_stats;
    lv_obj_t *cell_stat_values[DASHBOARD_CELL_STAT_COUNT];
    lv_obj_t *bms_safety_values[ESP_BMS_BMS_SAFETY_COUNT];
    lv_obj_t *bms_safety_checks[ESP_BMS_BMS_SAFETY_COUNT];
    lv_obj_t *bms_error;
    lv_obj_t *bms_status_ok;
    lv_obj_t *remaining_range_separator;
    lv_obj_t *remaining_range_title;
    lv_obj_t *remaining_range_value;
    lv_obj_t *remaining_range_unit;
    lv_obj_t *temperature;
    lv_obj_t *temperature_values[ESP_BMS_BMS_TEMP_MAX_COUNT];
    lv_obj_t *local_battery;
    lv_obj_t *gps_detail;
    lv_obj_t *gps_speed_unit;
    lv_obj_t *speed_static_background;
    lv_obj_t *speed_art;
    lv_obj_t *fireblade_page;
    lv_obj_t *fireblade_time;
    lv_obj_t *fireblade_controller_temp;
    lv_obj_t *fireblade_motor_temp;
    lv_obj_t *fireblade_soc;
    lv_obj_t *fireblade_consumption;
    lv_obj_t *fireblade_consumption_unit;
    lv_obj_t *fireblade_range;
    lv_obj_t *fireblade_average_speed;
    lv_obj_t *fireblade_average_speed_unit;
    lv_obj_t *fireblade_date;
    lv_obj_t *fireblade_gear;
    lv_obj_t *fireblade_gear_unit;
    lv_obj_t *fireblade_speed;
    lv_obj_t *fireblade_speed_unit;
    lv_obj_t *fireblade_needle_black;
    lv_obj_t *fireblade_needle_red;
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
    char boot_status_buf[24];
    char boot_progress_buf[8];
    char ota_status_buf[24];
    char ota_percent_buf[8];
    char ota_warning_buf[32];
    char fireblade_time_buf[8];
    char fireblade_controller_temp_buf[24];
    char fireblade_motor_temp_buf[8];
    char fireblade_soc_buf[8];
    char fireblade_consumption_buf[12];
    char fireblade_consumption_unit_buf[8];
    char fireblade_range_buf[8];
    char fireblade_average_speed_buf[8];
    char fireblade_average_speed_unit_buf[8];
    char fireblade_date_buf[24];
    char fireblade_gear_buf[4];
    char fireblade_gear_unit_buf[8];
    char fireblade_speed_buf[8];
    char fireblade_speed_unit_buf[8];
    char bms_soc_buf[8];
    char bms_capacity_buf[40];
    char bms_running_time_buf[32];
    char bms_cycle_capacity_buf[24];
    char bms_pack_voltage_buf[24];
    char bms_current_buf[24];
    char bms_cell_stat_buf[DASHBOARD_CELL_STAT_COUNT][16];
    char bms_safety_buf[ESP_BMS_BMS_SAFETY_COUNT][16];
    char bms_error_buf[16];
    char bms_status_buf[16];
    char bms_range_buf[8];
    char bms_temperature_buf[ESP_BMS_BMS_TEMP_MAX_COUNT][8];
    char speed_soc_buf[8];
    char speed_consumption_buf[20];
    char speed_controller_temp_buf[16];
    char speed_motor_temp_buf[16];
    char speed_gear_buf[8];
    char speed_scale_buf[SPEED_DASHBOARD_SCALE_LABEL_COUNT][8];
    lv_point_precise_t fireblade_tick_points[FIREBLADE_SCALE_LABEL_COUNT][2];
    lv_point_precise_t fireblade_needle_black_points[3];
    lv_point_precise_t fireblade_needle_red_points[3];
    lv_point_t fireblade_needle_center;
    int32_t fireblade_needle_radius;
    uint32_t fireblade_needle_signature;
    bool fireblade_needle_signature_valid;
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
    bool speed_page_renderable;
    bool native_bms_dashboard;
    bool native_fireblade_dashboard;
    bool boot_active;
    bool ota_active;
    bool page_transition_expanding;
    bool page_scroll_programmatic;
    bool page_scroll_gesture_active;
    bool page_scroll_throw_frozen;
    int32_t page_transition_anim_from_x;
    int32_t page_transition_anim_from_y;
    int32_t page_transition_anim_from_w;
    int32_t page_transition_anim_from_h;
    int32_t page_transition_anim_to_x;
    int32_t page_transition_anim_to_y;
    int32_t page_transition_anim_to_w;
    int32_t page_transition_anim_to_h;
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
    uint8_t settings_dashboard_view;
    uint8_t settings_system_view;
    uint8_t settings_system_slider_kind;
    uint8_t settings_calibration_target_index;
    uint8_t quick_level_overlay_kind;
    uint8_t quick_level_position;
    uint8_t quick_brightness_percent;
    uint8_t quick_volume_percent;
    uint8_t quick_rotate_toast_remaining_s;
    uint8_t boot_animation_style;
    esp_bms_speed_unit_t boot_speed_unit;
    esp_bms_speed_dashboard_style_t boot_dashboard_style;
    uint32_t flags;
    esp_bms_dashboard_snapshot_t last_snapshot;
    esp_bms_dashboard_snapshot_t deferred_snapshot;
} esp_bms_lvgl_ui_t;
#define PAGE_TRANSITION_CARD_MARGIN 16
#define PAGE_TRANSITION_CARD_RADIUS 8
#define PAGE_TRANSITION_CARD_ANIM_MS 160U
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
    SETTINGS_DETAIL_GPS,
    SETTINGS_DETAIL_DASHBOARD,
    SETTINGS_DETAIL_CONTROLLER,
    SETTINGS_DETAIL_SYSTEM,
    SETTINGS_DETAIL_ABOUT,
} settings_detail_id_t;
typedef enum {
    SETTINGS_BMS_VIEW_ROOT = 0,
    SETTINGS_BMS_VIEW_BLE_LIST,
    SETTINGS_BMS_VIEW_TYPE_LIST,
    SETTINGS_BMS_VIEW_PRESET_RANGE_EDIT,
} settings_bms_view_t;
typedef enum {
    SETTINGS_SYSTEM_VIEW_ROOT = 0,
    SETTINGS_SYSTEM_VIEW_BRIGHTNESS,
    SETTINGS_SYSTEM_VIEW_VOLUME,
    SETTINGS_SYSTEM_VIEW_LEVEL_POSITION,
    SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION,
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
typedef struct {
    esp_bms_speed_dashboard_style_t style;
    const char *label;
} settings_dashboard_style_option_t;
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
typedef struct {
    esp_bms_speed_source_t source;
    const char *label;
} settings_speed_source_option_t;
#endif
typedef struct {
    esp_bms_boot_animation_style_t style;
    const char *label;
} settings_boot_animation_option_t;
#define NATIVE_FOCUS_CAPACITY 32U
typedef struct {
    lv_obj_t *items[NATIVE_FOCUS_CAPACITY];
    size_t count;
} native_focus_list_t;

/* ---- 跨模块共享状态（定义见 esp_bms_lvgl_ui.c / ui_bms_dashboard.c） ---- */
extern esp_bms_lvgl_ui_t s_ui;
extern bool s_touch_calibration_supported;
extern bool s_native_gestures_supported;

extern const lv_color_t COLOR_BG;
extern const lv_color_t COLOR_PANEL_ALT;
extern const lv_color_t COLOR_SOC;
extern const lv_color_t COLOR_WHITE;
extern const lv_color_t COLOR_TEXT;
extern const lv_color_t COLOR_MUTED;
extern const lv_color_t COLOR_ACCENT;
extern const lv_color_t COLOR_WARN;
extern const lv_color_t COLOR_BAD;
extern const lv_color_t COLOR_BOOT_CYAN;
extern const lv_color_t COLOR_BOOT_BLUE;
extern const lv_color_t COLOR_BOOT_DIM;
extern const lv_color_t COLOR_BOOT_GRID;
extern const lv_color_t COLOR_DASHBOARD_BG;
extern const lv_color_t COLOR_DASHBOARD_PANEL;
extern const lv_color_t COLOR_DASHBOARD_SOC_PANEL;
extern const lv_color_t COLOR_DASHBOARD_BORDER;
extern const lv_color_t COLOR_DASHBOARD_SOC_BORDER;
extern const lv_color_t COLOR_DASHBOARD_TITLE;
extern const lv_color_t COLOR_DASHBOARD_VALUE;
extern const lv_color_t COLOR_STATUS_OK;
extern const lv_color_t COLOR_CONTROLLER_VALUE;
extern const lv_color_t COLOR_SPEED_BAND_DARK;
extern const lv_color_t COLOR_SPEED_BAND_BLUE;
extern const lv_color_t COLOR_SPEED_BAND_IDLE;
extern const lv_color_t COLOR_SPEED_BAND_DANGER;
extern const lv_color_t COLOR_SPEED_DIVIDER;
extern const lv_image_dsc_t SPEED_DASHBOARD_STATIC_LANDSCAPE;
extern const lv_color_t COLOR_SPEED_GPS_OK;
extern const lv_color_t COLOR_FIREBLADE_RED;
extern const lv_color_t COLOR_FIREBLADE_BLACK;
extern const lv_color_t COLOR_FIREBLADE_BRIDGE;
extern const lv_color_t COLOR_FIREBLADE_MODE;
extern const lv_color_t COLOR_FIREBLADE_GRAY;
extern const lv_color_t COLOR_FIREBLADE_DANGER_BG;
extern const lv_color_t COLOR_FIREBLADE_GREEN;
extern const lv_color_t COLOR_FIREBLADE_GEAR_BORDER;
extern const lv_color_t COLOR_SETTINGS_BG;
extern const lv_color_t COLOR_SETTINGS_CARD;
extern const lv_color_t COLOR_SETTINGS_LIST;
extern const lv_color_t COLOR_SETTINGS_BORDER;
extern const lv_color_t COLOR_SETTINGS_TEXT;
extern const lv_color_t COLOR_SETTINGS_MUTED;
extern const lv_color_t COLOR_SETTINGS_ACCENT;
extern const lv_color_t COLOR_SWITCH_ACTIVE;
extern const char *const DASHBOARD_TEMP_KEYS[ESP_BMS_BMS_TEMP_MAX_COUNT];
extern const uint16_t BMS_SAFETY_BITS[ESP_BMS_BMS_SAFETY_COUNT];
extern const char *const BMS_SAFETY_KEYS[ESP_BMS_BMS_SAFETY_COUNT];
extern const char *const FIREBLADE_SCALE_LABELS[FIREBLADE_SCALE_LABEL_COUNT];
extern const quick_panel_item_t QUICK_PANEL_ITEMS[QUICK_PANEL_BUTTON_COUNT];
/* 数组大小必须与 esp_bms_lvgl_ui.c 中的定义完全一致，否则 ARRAY_SIZE()
 * 会按声明大小遍历并越界（曾导致开机崩溃重启循环）。 */
#define SETTINGS_OPTIONS_COUNT \
    ((ESP_BMS_FEATURE_NETWORK ? 1 : 0) + \
     ((ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER) ? 1 : 0) + \
     (ESP_BMS_FEATURE_BMS ? 1 : 0) + \
     (ESP_BMS_FEATURE_GPS ? 1 : 0) + \
     ((ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER) ? 1 : 0) + \
     (ESP_BMS_FEATURE_CONTROLLER ? 1 : 0) + \
     2)
#define SETTINGS_HOTSPOT_ROWS_COUNT 6
#define SETTINGS_BLUETOOTH_ROWS_COUNT 3
#define SETTINGS_BMS_ROWS_COUNT 1
#define SETTINGS_BMS_TYPE_COUNT 5
#define SETTINGS_SYSTEM_ROWS_COUNT \
    (6 + CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING + (ESP_BMS_FEATURE_AUDIO ? 1 : 0))
#define SETTINGS_ABOUT_ROWS_COUNT 3
extern const settings_option_t SETTINGS_OPTIONS[SETTINGS_OPTIONS_COUNT];
extern const settings_detail_row_t SETTINGS_HOTSPOT_ROWS[SETTINGS_HOTSPOT_ROWS_COUNT];
extern const settings_detail_row_t SETTINGS_BLUETOOTH_ROWS[SETTINGS_BLUETOOTH_ROWS_COUNT];
extern const settings_detail_row_t SETTINGS_BMS_ROWS[SETTINGS_BMS_ROWS_COUNT];
extern const char *const SETTINGS_BMS_TYPE_LABELS[SETTINGS_BMS_TYPE_COUNT];
extern const esp_bms_lvgl_action_t SETTINGS_BMS_TYPE_ACTIONS[SETTINGS_BMS_TYPE_COUNT];
extern const settings_detail_row_t SETTINGS_SYSTEM_ROWS[SETTINGS_SYSTEM_ROWS_COUNT];
extern const settings_detail_row_t SETTINGS_ABOUT_ROWS[SETTINGS_ABOUT_ROWS_COUNT];

#define UI_FLAG(name) ui_state_flag_get(UI_STATE_FLAG_##name)
#define UI_SET_FLAG(name, enabled) ui_state_flag_set(UI_STATE_FLAG_##name, (enabled))

/* ---- core ---- */
const char *controller_gear_text(uint8_t gear, bool controller_online, bool gear_valid);
bool ui_flag_get(uint8_t flags, uint32_t index);
void ui_flag_set(uint8_t *flags, uint32_t index, bool enabled);
bool ui_state_flag_get(ui_state_flag_t flag);
void ui_state_flag_set(ui_state_flag_t flag, bool enabled);
void clear_style(lv_obj_t *obj);
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
bool dashboard_native_landscape_enabled(void);
#endif
bool bms_native_landscape_enabled(void);
bool bms_native_portrait_enabled(void);
void dashboard_static_cache_release_one(dashboard_static_cache_t *cache);
bool dashboard_static_cache_finalize(dashboard_static_cache_t *cache, lv_obj_t *page, lv_obj_t *static_layer, const char *name);
void label_set_text_if_changed(lv_obj_t *obj, const char *text);
void bms_label_set(lv_obj_t *obj, char *buffer, size_t buffer_len, const char *text);
void bms_native_set_safety_status(const esp_bms_dashboard_snapshot_t *snapshot);
void label_set_text_fmt_if_changed(lv_obj_t *obj, const char *fmt, ...);
void label_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color);
void set_obj_hidden(lv_obj_t *obj, bool hidden);
bool get_active_pointer(lv_point_t *point);
bool settings_view_is_visible(void);
void show_dashboard_view(void);
void show_settings_view(void);
void queue_action_with_commit(esp_bms_lvgl_action_t action, bool committed);
void queue_action(esp_bms_lvgl_action_t action);
void queue_bms_bind_action(const char *mac);
void queue_controller_bind_action(const char *mac);
void queue_touch_calibration_sample(uint8_t target_index, const lv_point_t *observed, const lv_point_t *expected);
uint8_t clamp_brightness_percent(int32_t value);
uint8_t clamp_volume_percent(int32_t value);
void perform_ui_action(esp_bms_lvgl_action_t action, bool close_quick_panel);
void action_event_cb(lv_event_t *event);
bool process_return_swipe_event(lv_event_code_t code, bool allow_start);
void return_swipe_event_cb(lv_event_t *event);
void quick_pull_event_cb(lv_event_t *event);
void apply_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot);
void flush_deferred_dashboard_snapshot(void);
esp_err_t rebuild_screen_if_needed(const esp_bms_dashboard_snapshot_t *snapshot);
native_focus_list_t native_focus_list(void);
void native_gesture_back(void);
esp_err_t esp_bms_lvgl_ui_handle_native_gesture(esp_bms_lvgl_native_gesture_t gesture);

/* ---- bms_dash ---- */
lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color);
lv_obj_t *label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font);
lv_obj_t *dashboard_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color, lv_color_t border_color);
lv_obj_t *dashboard_viewport(lv_obj_t *parent, bool portrait);
lv_obj_t *dashboard_separator(lv_obj_t *parent, int32_t x, int32_t y, int32_t w);
void dashboard_battery_icon(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
void update_dashboard_battery_icon(uint8_t soc_percent, bool valid, bool charging);
void dashboard_thermometer_icon(lv_obj_t *parent, int32_t center_x, int32_t y);
lv_obj_t *dashboard_native_layer(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height);
void create_native_bms_dashboard(void);
void create_native_bms_portrait_dashboard(void);
lv_obj_t *dashboard_cell_key(lv_obj_t *parent, int32_t x, int32_t y, uint8_t index);
lv_color_t dashboard_soc_fill_color(uint8_t soc_percent, bool valid, bool charging);

/* ---- quick ---- */
int32_t abs_i32(int32_t value);
int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value);
quick_panel_layout_t *quick_layout_ensure_current(void);
uint8_t quick_level_current_value(quick_level_kind_t kind);
quick_level_position_t quick_level_position(void);
const char *quick_level_position_text(void);
void quick_level_queue_value(quick_level_kind_t kind, uint8_t value, bool committed);
uint8_t quick_level_snap_drag_value(quick_level_kind_t kind, int32_t value);
void set_quick_brightness_value(uint8_t brightness_percent, bool queue, bool committed);
void set_quick_volume_value(uint8_t volume_percent, bool queue, bool committed);
void refresh_quick_level_layouts(void);
void quick_panel_stop_settle_anim(void);
int32_t quick_pull_open_threshold(void);
void quick_panel_animate_to_open_state(bool open);
void quick_toast_cancel(void);
void quick_toast_show_text(const char *text);
void quick_toast_show_connecting(void);
void quick_rotate_toast_show(void);
void set_quick_panel_open(bool open);
void set_quick_edit_mode(bool edit_mode);
void quick_edit_event_cb(lv_event_t *event);
int32_t quick_edit_button_size(void);
const lv_font_t *quick_edit_icon_font(void);
void quick_panel_item_event_cb(lv_event_t *event);
void quick_symbol_icon_recenter(lv_obj_t *icon, int32_t content_w, int32_t content_h, const char *symbol, const lv_font_t *font);
lv_obj_t *quick_symbol_icon(lv_obj_t *parent, int32_t content_w, int32_t content_h, const char *symbol, const lv_font_t *font);
lv_obj_t *quick_panel_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t index, const quick_panel_item_t *item);
void update_quick_item_colors(const esp_bms_dashboard_snapshot_t *snapshot);
void quick_level_overlay_hide(void);
void quick_level_event_cb(lv_event_t *event);
lv_obj_t *quick_level_tile(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, quick_level_kind_t kind, uint8_t value);
void quick_level_overlay_create(lv_obj_t *parent);
void quick_toast_create(lv_obj_t *parent);

/* ---- settings ---- */
bool settings_uses_s3_layout(void);
int32_t settings_scaled_px(int32_t value);
const lv_font_t *settings_title_font(void);
const lv_font_t *settings_disclosure_font(void);
void settings_navigation_set_hidden(bool hidden, bool animated);
void settings_navigation_scroll_event_cb(lv_event_t *event);
void settings_show_root(void);
bool settings_detail_is_enabled(settings_detail_id_t detail_id);
void settings_detail_chrome_show(settings_detail_id_t detail_id);
void settings_navigate_back(void);
void settings_detail_back_event_cb(lv_event_t *event);
void settings_swipe_indicator_hide(void);
void settings_add_swipe_handlers(lv_obj_t *obj);
const esp_bms_dashboard_snapshot_t *settings_current_snapshot(void);
lv_obj_t *settings_list_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t row_h, size_t row_count);
void settings_bms_popup_close(void);
bool settings_bms_popup_click_ready(lv_event_t *event);
void settings_show_bms_type_picker(void);
void settings_bms_ble_format_status(char *out, size_t out_len, const esp_bms_dashboard_snapshot_t *snapshot, settings_ble_source_t source, bool scan_requested);
void settings_bms_ble_start_scan(void);
bool settings_bms_ble_connection_in_progress(const esp_bms_dashboard_snapshot_t *snapshot, settings_ble_source_t source);
void settings_bms_ble_log_memory(const char *phase, settings_ble_source_t source, uint8_t candidate_count);
void settings_show_bms_ble_popup(settings_ble_source_t source, bool start_scan);
void settings_show_hotspot_detail(void);
void settings_show_bluetooth_detail(void);
void settings_preset_range_button_event_cb(lv_event_t *event);
void settings_show_bms_detail(void);
void settings_preset_range_confirm_event_cb(lv_event_t *event);
void settings_controller_confirm_event_cb(lv_event_t *event);
void settings_controller_value_event_cb(lv_event_t *event);
void settings_show_gps_detail(void);
void settings_show_dashboard_detail(void);
void settings_show_controller_detail(void);
void settings_show_bms_bind_confirm(const esp_bms_bms_scan_candidate_t *candidate);
const settings_detail_row_t *settings_detail_rows_for_id(settings_detail_id_t detail_id, size_t *count);
lv_obj_t *settings_detail_row(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const settings_detail_row_t *row);
void settings_show_system_slider(quick_level_kind_t kind);
void settings_show_system_position(void);
lv_obj_t *settings_option_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const settings_option_t *option);
bool settings_ble_candidate_rows_changed( const esp_bms_bms_scan_candidate_t *previous, uint8_t previous_count, const esp_bms_bms_scan_candidate_t *current, uint8_t current_count);
bool settings_controller_candidate_rows_changed( const esp_bms_dashboard_snapshot_t *previous, const esp_bms_dashboard_snapshot_t *current);
bool settings_controller_view_changed(const esp_bms_dashboard_snapshot_t *previous, const esp_bms_dashboard_snapshot_t *current, bool had_previous);

/* ---- settings_pickers ---- */
esp_bms_speed_dashboard_style_t speed_dashboard_style_from_snapshot( const esp_bms_dashboard_snapshot_t *snapshot);
const char *settings_dashboard_style_label(esp_bms_speed_dashboard_style_t style);
void settings_controller_style_option_event_cb(lv_event_t *event);
void settings_show_controller_style_picker(void);
void settings_speed_unit_button_event_cb(lv_event_t *event);
void settings_speed_unit_option_event_cb(lv_event_t *event);
void settings_show_speed_unit_picker(void);
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
void settings_speed_source_button_event_cb(lv_event_t *event);
#endif
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
void settings_speed_source_option_event_cb(lv_event_t *event);
#endif
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
void settings_show_speed_source_picker(void);
#endif
lv_obj_t *settings_speed_unit_row(lv_obj_t *parent, int32_t y, int32_t w, int32_t h, const char *value);
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
lv_obj_t *settings_speed_source_row(lv_obj_t *parent, int32_t y, int32_t w, int32_t h, const char *value);
#endif
void settings_controller_style_row(lv_obj_t *parent, int32_t y, int32_t w, int32_t h, const char *value);

/* ---- settings_system ---- */
const char *settings_bms_type_label(uint8_t type);
void settings_bms_ble_refresh_rows(const esp_bms_dashboard_snapshot_t *snapshot, settings_ble_source_t source, bool scan_requested, const char *phase);
uint8_t settings_bms_ble_candidate_index(uint8_t count, bool more_page, uint8_t row, bool *more_action);
bool settings_detail_action_uses_switch(esp_bms_lvgl_action_t action);
bool settings_detail_action_switch_on(esp_bms_lvgl_action_t action);
void settings_detail_switch(lv_obj_t *parent, int32_t x, int32_t y, bool enabled);
void settings_restore_popup_close(void);
void settings_restore_cancel_event_cb(lv_event_t *event);
void settings_restore_accept_event_cb(lv_event_t *event);
void settings_detail_action_event_cb(lv_event_t *event);
void settings_bms_type_button_event_cb(lv_event_t *event);
void settings_bms_type_option_event_cb(lv_event_t *event);
void settings_bms_bind_confirm_cancel(void);
void settings_bms_bind_confirm_cancel_event_cb(lv_event_t *event);
void settings_bms_bind_confirm_accept_event_cb(lv_event_t *event);
void settings_bms_ble_candidate_event_cb(lv_event_t *event);
void settings_bms_ble_refresh_event_cb(lv_event_t *event);
void settings_system_slider_event_cb(lv_event_t *event);
void settings_system_position_option_event_cb(lv_event_t *event);
void settings_boot_preview_timer_cancel(void);
void settings_calibration_start_timer_cancel(void);
void settings_boot_preview_button_event_cb(lv_event_t *event);
void settings_boot_animation_option_event_cb(lv_event_t *event);
void settings_show_system_view(settings_system_view_t view);
void settings_show_detail(settings_detail_id_t detail_id);
void settings_option_event_cb(lv_event_t *event);

/* ---- pages_common ---- */
void set_header(const esp_bms_dashboard_snapshot_t *snapshot);
void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot);
void set_setup_ap_control(bool enabled);
void set_cast_page(const esp_bms_dashboard_snapshot_t *snapshot);
void set_music_page(const esp_bms_dashboard_snapshot_t *snapshot);
void set_dashboard(const esp_bms_dashboard_snapshot_t *snapshot);

/* ---- controller_dash ---- */
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
void set_controller_dashboard(const esp_bms_dashboard_snapshot_t *snapshot);
#endif
lv_obj_t *controller_dashboard_label(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font, lv_color_t color);
void speed_dashboard_style_apply(const esp_bms_dashboard_snapshot_t *snapshot);

/* ---- fireblade ---- */
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
void set_fireblade_dashboard(const esp_bms_dashboard_snapshot_t *snapshot);
#endif
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
void create_fireblade_dashboard(void);
#endif

/* ---- speed ---- */
void gps_label_set(lv_obj_t *label_obj, char *buffer, size_t buffer_len, const char *text);
lv_point_t speed_dashboard_point(int32_t x, int32_t y);
void speed_dashboard_draw_triangle(lv_layer_t *layer, lv_point_t p0, lv_point_t p1, lv_point_t p2, lv_color_t color);
void set_gps_dashboard(const esp_bms_dashboard_snapshot_t *snapshot);
void create_gps_dashboard(void);
void speed_page_sync(const esp_bms_dashboard_snapshot_t *snapshot);

/* ---- screen ---- */
int32_t page_target_scroll_x(esp_bms_lvgl_page_t page);
int32_t page_last_scroll_x(void);
esp_bms_lvgl_page_t page_from_scroll_x(int32_t scroll_x);
bool page_transition_active(void);
void page_transition_show(void);
void page_transition_expand(esp_bms_lvgl_page_t page);
void finish_page_scroll_state(bool flush_snapshot);
void move_to_page(esp_bms_lvgl_page_t page, bool animated);
void screen_unlock_timer_cancel(void);
void screen_lock_reapply(void);
void screen_lock_enter(void);
bool dashboard_page_content_ready(esp_bms_lvgl_page_t page);
void dashboard_page_content_release(esp_bms_lvgl_page_t page);
void dashboard_pages_release_except(esp_bms_lvgl_page_t page);
void dashboard_page_content_ensure(esp_bms_lvgl_page_t page);
void create_screen(lv_display_t *display);

/* ---- boot_ota ---- */
bool boot_animation_style_is_available(uint8_t style);
bool boot_animation_style_is_gauge(uint8_t style);
esp_bms_speed_dashboard_style_t boot_gauge_dashboard_style(void);
uint16_t boot_gauge_demo_speed(uint8_t progress_percent);
esp_err_t esp_bms_lvgl_ui_boot_start(const esp_bms_dashboard_snapshot_t *snapshot);
esp_err_t esp_bms_lvgl_ui_boot_update(uint8_t progress_percent, const char *status_text);
esp_err_t esp_bms_lvgl_ui_boot_finish(const esp_bms_dashboard_snapshot_t *snapshot);
