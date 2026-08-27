/*
 * UI 模块: settings
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

/* 文件内前向声明 */
static void settings_show_preset_range_edit(void);

bool settings_uses_s3_layout(void)
{
#if SETTINGS_S3_FONT_ENABLED
    return (s_ui.width == 480 && s_ui.height == 320) ||
           (s_ui.width == 320 && s_ui.height == 480);
#else
    return false;
#endif
}
int32_t settings_scaled_px(int32_t value)
{
    return settings_uses_s3_layout() ? (value * 5 + 2) / 4 : value;
}
const lv_font_t *settings_title_font(void)
{
#if SETTINGS_S3_FONT_ENABLED
    return settings_uses_s3_layout() ? &settings_zh_18 : &settings_zh_16;
#else
    return &settings_zh_16;
#endif
}
static const lv_font_t *settings_subtitle_font(void)
{
    return settings_uses_s3_layout() ? &settings_zh_16 : &settings_zh_13;
}
const lv_font_t *settings_disclosure_font(void)
{
    return settings_uses_s3_layout() ? &settings_zh_13 : &settings_zh_16;
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
    lv_obj_set_style_pad_bottom(s_ui.settings_detail,
                                settings_scaled_px(16) + offset,
                                LV_PART_MAIN);
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

void settings_navigation_set_hidden(bool hidden, bool animated)
{
    if (!s_ui.settings_detail_header) {
        return;
    }

    const bool tertiary_view =
        (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BMS &&
         s_ui.settings_bms_view != (uint8_t)SETTINGS_BMS_VIEW_ROOT) ||
        (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_DASHBOARD &&
         s_ui.settings_dashboard_view != (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT) ||
        (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_CONTROLLER &&
         s_ui.settings_controller_view != (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT) ||
        (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_SYSTEM &&
         s_ui.settings_system_view != (uint8_t)SETTINGS_SYSTEM_VIEW_ROOT);
    if (hidden && tertiary_view) {
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
        if (!settings_view_is_visible()) {
            return;
        }
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

void settings_navigation_scroll_event_cb(lv_event_t *event)
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

void settings_show_root(void)
{
    settings_root_build();
    settings_bms_popup_close();
    if (s_ui.settings_swipe_drag_dx == 0) {
        settings_swipe_indicator_hide();
    }
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_NONE;
    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_ROOT;
    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    s_ui.settings_bms_ble_status = NULL;
    UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
    set_obj_hidden(s_ui.settings_detail, true);
    set_obj_hidden(s_ui.settings_root, false);
    if (s_ui.settings_carousel && lv_obj_get_scroll_y(s_ui.settings_carousel) != 0) {
        lv_obj_scroll_to_y(s_ui.settings_carousel, 0, LV_ANIM_OFF);
    }
    settings_detail_chrome_show(SETTINGS_DETAIL_NONE);
}

bool settings_detail_is_enabled(settings_detail_id_t detail_id)
{
    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_OPTIONS); ++index) {
        if (SETTINGS_OPTIONS[index].detail_id == detail_id) {
            return true;
        }
    }
    return false;
}

static const char *settings_detail_title_text(settings_detail_id_t detail_id)
{
    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_OPTIONS); ++index) {
        if (SETTINGS_OPTIONS[index].detail_id == detail_id) {
            return ui_t(SETTINGS_OPTIONS[index].title, SETTINGS_OPTIONS[index].title_en);
        }
    }
    return ui_t("设置", "Settings");
}

void settings_detail_chrome_show(settings_detail_id_t detail_id)
{
    label_set_text_if_changed(s_ui.settings_detail_title,
                              settings_detail_title_text(detail_id));
    const bool show_boot_preview =
        detail_id == SETTINGS_DETAIL_SYSTEM &&
        s_ui.settings_system_view == (uint8_t)SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION;
    set_obj_hidden(s_ui.settings_boot_preview_button, !show_boot_preview);
    set_obj_hidden(s_ui.settings_detail_header, false);
    set_obj_hidden(s_ui.settings_detail_edge_zone, false);
    settings_navigation_set_hidden(false, false);
    s_ui.settings_nav_scroll_anchor_y = 0;
    lv_obj_move_foreground(s_ui.settings_detail_edge_zone);
    lv_obj_move_foreground(s_ui.settings_detail_header);
}

void settings_navigate_back(void)
{
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_NONE) {
        show_dashboard_view();
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BMS &&
        s_ui.settings_bms_view != (uint8_t)SETTINGS_BMS_VIEW_ROOT) {
        if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST &&
            s_ui.settings_ble_more_page) {
            s_ui.settings_ble_more_page = false;
            settings_bms_ble_refresh_rows(settings_current_snapshot(),
                                          SETTINGS_BLE_SOURCE_BMS,
                                          false,
                                          "back-to-first-page");
            lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
            settings_navigation_set_hidden(false, false);
            return;
        }
        if (s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST) {
            queue_action(ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION);
        }
        settings_show_bms_detail();
        settings_navigation_set_hidden(false, false);
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_DASHBOARD &&
        s_ui.settings_dashboard_view != (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT) {
        settings_show_dashboard_detail();
        settings_navigation_set_hidden(false, false);
        return;
    }
    if (s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_CONTROLLER &&
        s_ui.settings_controller_view != (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT) {
        if (s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST &&
            s_ui.settings_ble_more_page) {
            s_ui.settings_ble_more_page = false;
            settings_bms_ble_refresh_rows(settings_current_snapshot(),
                                          SETTINGS_BLE_SOURCE_CONTROLLER,
                                          false,
                                          "back-to-first-page");
            lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
            settings_navigation_set_hidden(false, false);
            return;
        }
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

void settings_detail_back_event_cb(lv_event_t *event)
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

void settings_swipe_indicator_hide(void)
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
        UI_SET_FLAG(SETTINGS_SWIPE_CONSUMED, false);
        s_ui.settings_swipe_drag_dx = 0;
        s_ui.settings_nav_drag_anchor_y = 0;
    }
}

void settings_add_swipe_handlers(lv_obj_t *obj)
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

const esp_bms_dashboard_snapshot_t *settings_current_snapshot(void)
{
    static const esp_bms_dashboard_snapshot_t empty_snapshot = { 0 };
    return UI_FLAG(LAST_SNAPSHOT_VALID) ? &s_ui.last_snapshot : &empty_snapshot;
}

lv_obj_t *settings_list_card(lv_obj_t *parent,
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

void settings_bms_popup_close(void)
{
    if (s_ui.settings_bms_popup) {
        lv_obj_delete(s_ui.settings_bms_popup);
        s_ui.settings_bms_popup = NULL;
        s_ui.settings_bms_ble_status = NULL;
        s_ui.settings_bms_ble_popup_open = false;
    }
}

bool settings_bms_popup_click_ready(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return false;
    }
    UI_SET_FLAG(SETTINGS_SWIPE_TRACKING, false);
    return true;
}

void settings_show_bms_type_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = settings_scaled_px(portrait ? 7 : 5);
    const int32_t first_y = settings_scaled_px(12);
    const uint8_t current = settings_current_snapshot()->bms_type;

    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_TYPE_LIST;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("保护板类型", "BMS type"));
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

        const lv_font_t *text_font = settings_title_font();
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

void settings_bms_ble_format_status(char *out,
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
        (void)snprintf(out, out_len, "%s", ui_t("扫描...", "Scanning..."));
    } else if (source == SETTINGS_BLE_SOURCE_CONTROLLER &&
               SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE)) {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       snapshot->controller_bound_name[0] != '\0'
                           ? snapshot->controller_bound_name
                           : ui_t("已连接", "Connected"));
    } else if (source == SETTINGS_BLE_SOURCE_BMS && SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       snapshot->bms_bound_name[0] != '\0' ? snapshot->bms_bound_name : ui_t("已连接", "Connected"));
    } else if ((source == SETTINGS_BLE_SOURCE_BMS ? snapshot->bms_scan_candidate_count
                                                  : snapshot->controller_scan_candidate_count) > 0U) {
        const uint8_t count = source == SETTINGS_BLE_SOURCE_BMS
                                  ? snapshot->bms_scan_candidate_count
                                  : snapshot->controller_scan_candidate_count;
        (void)snprintf(out, out_len, ui_t("发现 %u", "Found %u"), (unsigned)count);
    } else if (source == SETTINGS_BLE_SOURCE_CONTROLLER &&
               snapshot->controller_bound_name[0] != '\0') {
        (void)snprintf(out, out_len, "%s", snapshot->controller_bound_name);
    } else if (source == SETTINGS_BLE_SOURCE_BMS && snapshot->bms_info_text[0] != '\0') {
        (void)snprintf(out, out_len, "%.15s", snapshot->bms_info_text);
    } else {
        (void)snprintf(out,
                       out_len,
                       "%s",
                       source == SETTINGS_BLE_SOURCE_BMS ? ui_t("未发现保护板", "No BMS found")
                                                         : ui_t("未发现控制器", "No controller found"));
    }
}

void settings_bms_ble_start_scan(void)
{
    if (s_ui.settings_bms_ble_status) {
        label_set_text_if_changed(s_ui.settings_bms_ble_status, ui_t("扫描...", "Scanning..."));
    }
    const settings_ble_source_t source = (settings_ble_source_t)s_ui.settings_ble_source;
    ESP_LOGI(TAG, "[ble-ui] queue %s scan from list page",
             source == SETTINGS_BLE_SOURCE_BMS ? "BMS" : "controller");
    queue_action(source == SETTINGS_BLE_SOURCE_BMS ? ESP_BMS_LVGL_ACTION_START_BMS_BIND
                                                    : ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND);
}

bool settings_bms_ble_connection_in_progress(const esp_bms_dashboard_snapshot_t *snapshot,
                                                    settings_ble_source_t source)
{
    if (!snapshot || source != SETTINGS_BLE_SOURCE_BMS ||
        SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        return false;
    }
    return strcmp(snapshot->bms_info_text, "BMS CONN") == 0 ||
           strcmp(snapshot->bms_info_text, "BMS DISC") == 0 ||
           strcmp(snapshot->bms_info_text, "BMS SVC") == 0 ||
           strcmp(snapshot->bms_info_text, "BMS CHR") == 0 ||
           strcmp(snapshot->bms_info_text, "BMS CCCD") == 0 ||
           strcmp(snapshot->bms_info_text, "BMS SUB") == 0;
}

void settings_bms_ble_log_memory(const char *phase,
                                        settings_ble_source_t source,
                                        uint8_t candidate_count)
{
    lv_mem_monitor_t lvgl_memory = { 0 };
    lv_mem_monitor(&lvgl_memory);
    ESP_LOGI(TAG,
             "[ble-ui] %s source=%s candidates=%u lvgl_free=%u lvgl_largest=%u "
             "lvgl_used=%u%% lvgl_frag=%u%% heap_free=%u heap_largest=%u",
             phase,
             source == SETTINGS_BLE_SOURCE_BMS ? "BMS" : "controller",
             (unsigned)candidate_count,
             (unsigned)lvgl_memory.free_size,
             (unsigned)lvgl_memory.free_biggest_size,
             (unsigned)lvgl_memory.used_pct,
             (unsigned)lvgl_memory.frag_pct,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

void settings_show_bms_ble_popup(settings_ble_source_t source, bool start_scan)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t status_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                        SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t refresh_w = status_h;
    const int32_t gap = portrait ? 7 : 5;
    const int32_t status_w = card_w - refresh_w - gap;
    const int32_t first_y = 12;
    const int32_t list_y = first_y + status_h + gap;
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();

    const bool same_list = s_ui.settings_ble_source == (uint8_t)source &&
                           (source == SETTINGS_BLE_SOURCE_BMS
                                ? s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST
                                : s_ui.settings_controller_view ==
                                      (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST);
    if (!same_list) {
        s_ui.settings_ble_more_page = false;
    }
    s_ui.settings_ble_source = (uint8_t)source;
    if (source == SETTINGS_BLE_SOURCE_BMS) {
        s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST;
    } else {
        s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST;
    }
    s_ui.settings_bms_ble_status = NULL;
    s_ui.settings_bms_ble_empty = NULL;
    s_ui.settings_bms_ble_list = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("蓝牙连接", "Bluetooth"));
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
    s_ui.settings_bms_ble_status = label(status,
                                         10,
                                         (status_h - ((int32_t)settings_zh_16.line_height + 4)) / 2,
                                         status_w - 20,
                                         (int32_t)settings_zh_16.line_height + 4,
                                         &settings_zh_16);
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

    const int32_t empty_h = (int32_t)settings_zh_16.line_height + 8;
    s_ui.settings_bms_ble_empty = label(s_ui.settings_detail,
                                        card_x,
                                        list_y + 18,
                                        card_w,
                                        empty_h,
                                        &settings_zh_16);
    lv_obj_set_style_text_align(s_ui.settings_bms_ble_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_bms_ble_empty, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    s_ui.settings_bms_ble_list = label(
        s_ui.settings_detail,
        card_x,
        list_y,
        card_w,
        ((row_h + gap) * SETTINGS_BLE_MAX_VISIBLE_ROWS) - gap,
        &settings_zh_16);
    lv_obj_set_style_radius(s_ui.settings_bms_ble_list, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.settings_bms_ble_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.settings_bms_ble_list,
                                  COLOR_SETTINGS_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_ui.settings_bms_ble_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.settings_bms_ble_list, COLOR_SETTINGS_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_bms_ble_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_ui.settings_bms_ble_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_ui.settings_bms_ble_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_ui.settings_bms_ble_list,
                             (row_h - (int32_t)settings_zh_16.line_height) / 2,
                             LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_ui.settings_bms_ble_list,
                                     row_h + gap - (int32_t)settings_zh_16.line_height,
                                     LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_bms_ble_list,
                                COLOR_SETTINGS_TEXT,
                                LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_bms_ble_list, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(s_ui.settings_bms_ble_list);
    lv_obj_add_event_cb(s_ui.settings_bms_ble_list,
                        settings_bms_ble_candidate_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    settings_bms_ble_refresh_rows(snapshot, source, start_scan, "list-built");

    if (start_scan) {
        settings_bms_ble_start_scan();
    }
}

void settings_show_hotspot_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();

    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = portrait ? s_ui.width - (SETTINGS_LIST_MARGIN_X * 2) :
                                      (s_ui.width / 2) - 12;
    const int32_t row_h = portrait ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const int32_t gap = 8;
    const int32_t info_y = 12 + row_h + gap;
    const int32_t info_h = portrait ? 70 : 96;
    const settings_detail_row_t control_row = {
        "热点共享",
        "Hotspot share",
        SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? ui_t("热点已打开", "Hotspot on")
                                                 : ui_t("未打开", "Off"),
        SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "Hotspot on" : "Off",
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
    /* 顶部两行提示文字的高度，二维码面板需要加高容纳 */
    const int32_t qr_hint_h = 40;
    /* 面板可用宽度：竖屏=整卡宽，横屏=右半卡宽 */
    const int32_t avail_w = portrait ? (s_ui.width - SETTINGS_LIST_MARGIN_X * 2)
                                     : ((s_ui.width / 2) - 12);
    /* 13px 两行提示约 162px 宽；窄屏退回 10px 提示字体 */
    const bool qr_hint_small = avail_w < 184;
    const int32_t qr_hint_min_w = qr_hint_small ? 148 : 184;
    const int32_t qr_panel_w = qr_size + 18 > qr_hint_min_w ? (qr_size + 18) : qr_hint_min_w;
    const int32_t qr_panel_w_clamped = qr_panel_w < avail_w ? qr_panel_w : avail_w;
    const int32_t qr_panel_h = qr_size + 18 + qr_hint_h;
    const int32_t qr_x = portrait ? (s_ui.width - qr_panel_w_clamped) / 2
                                  : (s_ui.width - qr_panel_w_clamped - 12);
    /* 横屏：二维码面板贴近顶部状态栏（内容区 y=8，含 header 上移后实际约 46px）；
       竖屏：保持在 info 卡片下方。 */
    const int32_t qr_y = portrait ? (info_y + info_h + 10) : 8;
    s_ui.setup_ap_qr_panel = panel(s_ui.settings_detail,
                                  qr_x,
                                  qr_y,
                                  qr_panel_w_clamped,
                                  qr_panel_h,
                                  COLOR_WHITE);
    lv_obj_set_style_radius(s_ui.setup_ap_qr_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.setup_ap_qr_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.setup_ap_qr_panel, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_ui.setup_ap_qr_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *qr_hint = label(s_ui.setup_ap_qr_panel,
                              10,
                              8,
                              qr_panel_w_clamped - 20,
                              qr_hint_h - 10,
                              qr_hint_small ? &settings_zh_10 : &settings_zh_13);
    lv_label_set_long_mode(qr_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(qr_hint, ui_t("扫描二维码自动连接wifi；\n连接后访问192.168.4.1",
                                    "Scan the QR code to join the WiFi;\nthen open 192.168.4.1"));
    lv_obj_set_style_text_align(qr_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(qr_hint, COLOR_SETTINGS_BG, LV_PART_MAIN);
    s_ui.setup_ap_qr = lv_qrcode_create(s_ui.setup_ap_qr_panel);
    if (s_ui.setup_ap_qr) {
        lv_qrcode_set_size(s_ui.setup_ap_qr, qr_size);
        lv_qrcode_set_dark_color(s_ui.setup_ap_qr, COLOR_SETTINGS_BG);
        lv_qrcode_set_light_color(s_ui.setup_ap_qr, COLOR_WHITE);
        lv_qrcode_set_quiet_zone(s_ui.setup_ap_qr, true);
        lv_obj_align(s_ui.setup_ap_qr, LV_ALIGN_BOTTOM_MID, 0, -9);
    }
    set_obj_hidden(s_ui.setup_ap_qr_panel, true);
#endif

    set_setup_ap(snapshot);
}

static const char *bluetooth_status_text(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot) {
        return ui_t("附近不可见", "Hidden");
    }
    if (SNAPSHOT_FLAG(snapshot, BLUETOOTH_CONNECTED)) {
        return ui_t("已连接", "Connected");
    }
    if (SNAPSHOT_FLAG(snapshot, BLUETOOTH_ADVERTISING)) {
        return ui_t("可被发现", "Discoverable");
    }
    return ui_t("附近不可见", "Hidden");
}

void settings_show_bluetooth_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();

    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const int32_t first_y = 12;
    const bool pin_visible = snapshot && strcmp(snapshot->bms_error_text, "PIN 123456") == 0;

    const settings_detail_row_t rows[] = {
        { "状态", "Status", bluetooth_status_text(snapshot), bluetooth_status_text(snapshot),
          ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "名称", "Name",
          snapshot->bluetooth_name[0] != '\0' ? snapshot->bluetooth_name : "ESP32 BMS GPS",
          snapshot->bluetooth_name[0] != '\0' ? snapshot->bluetooth_name : "ESP32 BMS GPS",
          ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "可被发现", "Discoverable", pin_visible ? "PIN 123456" : ui_t("附近可见", "Visible nearby"),
          pin_visible ? "PIN 123456" : "Visible nearby", ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING,
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

void settings_preset_range_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    settings_show_preset_range_edit();
}

void settings_show_bms_detail(void)
{
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    char ble_status[ESP_BMS_BMS_SCAN_NAME_LEN + 1U] = { 0 };
    char preset_range[16] = { 0 };
    char capacity_estimate[16] = { 0 };

    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_ROOT;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT;
    memset(s_ui.settings_controller_tire_rollers,
           0,
           sizeof(s_ui.settings_controller_tire_rollers));
    s_ui.settings_controller_ratio_roller = NULL;
    memset(s_ui.settings_preset_range_rollers,
           0,
           sizeof(s_ui.settings_preset_range_rollers));
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("BMS设置", "BMS settings"));
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    settings_bms_ble_format_status(ble_status,
                                   sizeof(ble_status),
                                   snapshot,
                                   SETTINGS_BLE_SOURCE_BMS,
                                   false);
    const settings_detail_row_t ble_row = {
        "蓝牙连接",
        "Bluetooth",
        ble_status,
        ble_status,
        ESP_BMS_LVGL_ACTION_START_BMS_BIND,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    const settings_detail_row_t type_row = {
        "保护板类型",
        "BMS type",
        settings_bms_type_label(snapshot->bms_type),
        settings_bms_type_label(snapshot->bms_type),
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    (void)snprintf(preset_range,
                   sizeof(preset_range),
                   "%04u km",
                   snapshot->preset_range_km);
    const settings_detail_row_t preset_range_row = {
        "预设里程",
        "Preset range",
        preset_range,
        preset_range,
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    const bool capacity_supported = snapshot->bms_type == 0U || snapshot->bms_type == 1U ||
                                    snapshot->bms_type == 3U || snapshot->bms_type == 4U;
    const char *capacity_subtitle = capacity_supported ? ui_t("估算中", "Estimating")
                                                       : "ANT / JK / DALY / YY";
    if (capacity_supported && snapshot->bms_capacity_estimate_mah > 0U) {
        (void)snprintf(capacity_estimate,
                       sizeof(capacity_estimate),
                       "%lu.%01lu Ah",
                       (unsigned long)(snapshot->bms_capacity_estimate_mah / 1000U),
                       (unsigned long)((snapshot->bms_capacity_estimate_mah % 1000U) / 100U));
        capacity_subtitle = capacity_estimate;
    }
    const settings_detail_row_t capacity_estimate_row = {
        "容量估算",
        "Capacity estimate",
        capacity_subtitle,
        capacity_subtitle,
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *list_card = settings_list_card(s_ui.settings_detail,
                                             card_x,
                                             12,
                                             card_w,
                                             row_h,
                                             4);
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

    lv_obj_t *preset_box = settings_detail_row(list_card,
                                                0,
                                                row_h * 2,
                                                card_w,
                                                row_h,
                                                &preset_range_row);
    lv_obj_add_event_cb(preset_box,
                        settings_preset_range_button_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    arrow = label(preset_box, card_w - 24, 0, 14, 15, &settings_zh_13);
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);

    lv_obj_t *capacity_box = settings_detail_row(list_card,
                                                  0,
                                                  row_h * 3,
                                                  card_w,
                                                  row_h,
                                                  &capacity_estimate_row);
    lv_obj_remove_flag(capacity_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_state(capacity_box, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(capacity_box, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
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
    const lv_font_t *font =
        settings_uses_s3_layout() ? settings_title_font() : &lv_font_montserrat_14;
    lv_obj_set_style_text_font(roller, font, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, font, LV_PART_SELECTED);
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

static const char PRESET_RANGE_DIGIT_OPTIONS[] = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

void settings_preset_range_confirm_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    uint16_t range_km = 0U;
    for (size_t index = 0; index < ARRAY_SIZE(s_ui.settings_preset_range_rollers); ++index) {
        if (!s_ui.settings_preset_range_rollers[index]) {
            return;
        }
        range_km = (uint16_t)(range_km * 10U +
                              lv_roller_get_selected(s_ui.settings_preset_range_rollers[index]));
    }

    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    s_ui.pending_event.action = ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE;
    s_ui.pending_event.numeric_delta = (int16_t)range_km;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, NUMERIC_DELTA_VALID, true);
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, COMMITTED, true);
    settings_show_bms_detail();
    lv_indev_wait_release(lv_indev_active());
}

static void settings_show_preset_range_edit(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const uint16_t preset_range_km = settings_current_snapshot()->preset_range_km;
    const uint16_t divisors[] = { 1000U, 100U, 10U, 1U };
    const int32_t card_x = settings_scaled_px(12);
    const int32_t card_w = s_ui.width - (card_x * 2);
    const int32_t card_h = settings_scaled_px(portrait ? 168 : 104);
    const int32_t gap = settings_scaled_px(6);
    const int32_t roller_h = settings_scaled_px(portrait ? 116 : 72);
    const int32_t roller_w = (card_w - (card_x * 2) - (gap * 3)) / 4;

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_BMS;
    s_ui.settings_bms_view = (uint8_t)SETTINGS_BMS_VIEW_PRESET_RANGE_EDIT;
    memset(s_ui.settings_preset_range_rollers,
           0,
           sizeof(s_ui.settings_preset_range_rollers));
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("预设里程", "Preset range"));
    settings_navigation_set_hidden(false, false);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    lv_obj_t *card = panel(s_ui.settings_detail,
                           card_x,
                           settings_scaled_px(12),
                           card_w,
                           card_h,
                           COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    for (size_t index = 0; index < ARRAY_SIZE(s_ui.settings_preset_range_rollers); ++index) {
        const uint32_t digit = (preset_range_km / divisors[index]) % 10U;
        s_ui.settings_preset_range_rollers[index] = settings_controller_roller(
            card,
            card_x + (int32_t)index * (roller_w + gap),
            (card_h - roller_h) / 2,
            roller_w,
            roller_h,
            PRESET_RANGE_DIGIT_OPTIONS,
            digit);
    }

    const int32_t button_w = clamp_i32(s_ui.width - settings_scaled_px(64),
                                       settings_scaled_px(160),
                                       settings_scaled_px(240));
    lv_obj_t *button = panel(s_ui.settings_detail,
                             (s_ui.width - button_w) / 2,
                             settings_scaled_px(24) + card_h,
                             button_w,
                             settings_scaled_px(42),
                             COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(button);
    lv_obj_add_event_cb(button,
                        settings_preset_range_confirm_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *text = label(button,
                           0,
                           settings_scaled_px(10),
                           button_w,
                           settings_scaled_px(20),
                           settings_title_font());
    lv_label_set_text(text, ui_t("确认", "Confirm"));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, COLOR_WHITE, LV_PART_MAIN);
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

void settings_controller_confirm_event_cb(lv_event_t *event)
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
    const int32_t button_w = clamp_i32(s_ui.width - settings_scaled_px(64),
                                       settings_scaled_px(160),
                                       settings_scaled_px(240));
    lv_obj_t *button = panel(parent,
                             (s_ui.width - button_w) / 2,
                             y,
                             button_w,
                             settings_scaled_px(42),
                             COLOR_SWITCH_ACTIVE);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    settings_add_swipe_handlers(button);
    lv_obj_add_event_cb(button, settings_controller_confirm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *text = label(button,
                           0,
                           settings_scaled_px(10),
                           button_w,
                           settings_scaled_px(20),
                           settings_title_font());
    lv_label_set_text(text, ui_t("确认", "Confirm"));
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
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("轮胎规格", "Tire size"));
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
    const int32_t card_x = settings_scaled_px(12);
    const int32_t card_w = s_ui.width - (card_x * 2);
    const int32_t card_h = settings_scaled_px(portrait ? 188 : 118);
    lv_obj_t *card = panel(s_ui.settings_detail,
                           card_x,
                           settings_scaled_px(12),
                           card_w,
                           card_h,
                           COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    const char *const titles[] = { ui_t("轮辋", "Rim"), ui_t("扁平比", "Aspect"), ui_t("胎宽", "Width") };
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
    const int32_t gap = settings_scaled_px(6);
    const int32_t roller_w = (card_w - (card_x * 2) - (gap * 2)) / 3;
    const int32_t roller_h = card_h - settings_scaled_px(42);
    for (uint8_t index = 0; index < 3U; ++index) {
        const int32_t x = card_x + index * (roller_w + gap);
        lv_obj_t *title = label(card,
                                x,
                                settings_scaled_px(7),
                                roller_w,
                                settings_scaled_px(20),
                                settings_subtitle_font());
        lv_label_set_text(title, titles[index]);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s_ui.settings_controller_tire_rollers[index] =
            settings_controller_roller(card,
                                       x,
                                       settings_scaled_px(30),
                                       roller_w,
                                       roller_h,
                                       options[index],
                                       selected[index]);
    }
    s_ui.settings_controller_ratio_roller = NULL;
    settings_controller_confirm_button(s_ui.settings_detail, settings_scaled_px(20) + card_h);
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
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("传动比", "Gear ratio"));
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
    const int32_t roller_w = clamp_i32(s_ui.width - settings_scaled_px(96),
                                       settings_scaled_px(128),
                                       settings_scaled_px(220));
    const int32_t roller_h = settings_scaled_px(s_ui.width < s_ui.height ? 178 : 112);
    s_ui.settings_controller_ratio_roller =
        settings_controller_roller(s_ui.settings_detail,
                                   (s_ui.width - roller_w) / 2,
                                   settings_scaled_px(12),
                                   roller_w,
                                   roller_h,
                                   options,
                                   ratio - ESP_BMS_CONTROLLER_RATIO_CENTI_MIN);
    lv_free(options);
    memset(s_ui.settings_controller_tire_rollers,
           0,
           sizeof(s_ui.settings_controller_tire_rollers));
    settings_controller_confirm_button(s_ui.settings_detail, settings_scaled_px(20) + roller_h);
}

void settings_controller_value_event_cb(lv_event_t *event)
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
        title,
        value,
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
    lv_obj_t *arrow = label(box, w - 26, 0, 16, 18, settings_disclosure_font());
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
}

void settings_show_gps_detail(void)
{
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    char fix_mode[32] = { 0 };
    char satellite_system[32] = { 0 };
    strncpy(satellite_system, ui_t("等待搜星", "Waiting for fix"), sizeof(satellite_system) - 1U);
    char satellites[48] = { 0 };
    char average_snr[64] = { 0 };
    char hdop[32] = { 0 };
    const settings_detail_row_t rows[] = {
        { "定位模式", "Fix mode", fix_mode, fix_mode, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "卫星系统", "Satellites", satellite_system, satellite_system, ESP_BMS_LVGL_ACTION_NONE,
          SETTINGS_SYSTEM_VIEW_ROOT },
        { "卫星", "Satellites in view", satellites, satellites, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
        { "平均 SNR 信噪比", "Avg SNR", average_snr, average_snr, ESP_BMS_LVGL_ACTION_NONE,
          SETTINGS_SYSTEM_VIEW_ROOT },
        { "HDOP", "HDOP", hdop, hdop, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT },
    };

    if (snapshot->gps_satellite_info_valid) {
        const char *const fix_text = snapshot->gps_fix_dimension == 3U ?
                                         ui_t("3D 定位，测速精准", "3D fix, accurate speed") :
                                     snapshot->gps_fix_dimension == 2U ?
                                         ui_t("2D 定位，测速偏差大", "2D fix, speed less accurate")
                                         : ui_t("无效定位", "No fix");
        const char *const satellite_hint = snapshot->gps_satellites_used < 5U ?
                                             ui_t("，少于5颗不稳", "; <5 sats, unstable")
                                             : ui_t("，测速稳定", "; speed stable");
        const char *const snr_hint = snapshot->gps_average_cn0 < 10U ?
                                       ui_t("，信号差易漂移", "; poor signal, drifts") : "";
        switch (snapshot->gps_constellation_mask) {
        case 0x07U:
            (void)snprintf(satellite_system, sizeof(satellite_system), ui_t("GPS+BDS+GLONASS 三模", "GPS+BDS+GLONASS"));
            break;
        case 0x03U:
            (void)snprintf(satellite_system, sizeof(satellite_system), ui_t("GPS+北斗 双模", "GPS+BDS"));
            break;
        case 0x01U:
            (void)snprintf(satellite_system, sizeof(satellite_system), ui_t("单 GPS", "GPS only"));
            break;
        case 0x02U:
            (void)snprintf(satellite_system, sizeof(satellite_system), ui_t("北斗 单模", "BDS only"));
            break;
        case 0x04U:
            (void)snprintf(satellite_system, sizeof(satellite_system), ui_t("GLONASS 单模", "GLONASS only"));
            break;
        default:
            break;
        }
        (void)snprintf(fix_mode, sizeof(fix_mode), "%s", fix_text);
        (void)snprintf(satellites, sizeof(satellites), ui_t("%u 可见 / %u 有效%s", "%u vis / %u used%s"),
                       snapshot->gps_satellites_visible,
                       snapshot->gps_satellites_used,
                       satellite_hint);
        (void)snprintf(average_snr, sizeof(average_snr), ui_t("%u dBHz，越大越好%s", "%u dBHz, higher is better%s"),
                       snapshot->gps_average_cn0, snr_hint);
        if (snapshot->gps_hdop_valid) {
            const char *const hdop_hint = snapshot->gps_hdop_centi < 150U ?
                                            ui_t("，优秀", "; excellent") :
                                        snapshot->gps_hdop_centi > 400U ?
                                            ui_t("，不建议测加速", "; not for acceleration") : "";
            (void)snprintf(hdop, sizeof(hdop), "%u.%02u%s",
                           snapshot->gps_hdop_centi / 100U,
                           snapshot->gps_hdop_centi % 100U,
                           hdop_hint);
        } else {
            (void)snprintf(hdop, sizeof(hdop), "--");
        }
    } else {
        (void)snprintf(fix_mode, sizeof(fix_mode), "%s", ui_t("无效定位", "No fix"));
        (void)snprintf(satellites, sizeof(satellites), "--");
        (void)snprintf(average_snr, sizeof(average_snr), "--");
        (void)snprintf(hdop, sizeof(hdop), "--");
    }

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_GPS;
    s_ui.settings_bms_ble_status = NULL;
    label_set_text_if_changed(s_ui.settings_detail_title, "GPS");

    const size_t row_count = ARRAY_SIZE(rows);
    lv_obj_t *card = settings_list_card(s_ui.settings_detail, card_x, 12, card_w, row_h,
                                        row_count);
    size_t row_index = 0U;
    for (size_t index = 0U; index < ARRAY_SIZE(rows); ++index) {
        lv_obj_t *row =
            settings_detail_row(card, 0, (int32_t)row_index++ * row_h, card_w, row_h, &rows[index]);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(row, LV_STATE_DISABLED);
        lv_obj_set_style_text_opa(row, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
    }
    lv_obj_update_layout(s_ui.settings_detail);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
}

void settings_show_dashboard_detail(void)
{
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const size_t row_count = 2U
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
                             + 1U
#endif
        ;

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_DASHBOARD;
    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT;
    s_ui.settings_bms_ble_status = NULL;
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("仪表", "Dashboard"));
    lv_obj_t *card = settings_list_card(s_ui.settings_detail, card_x, 12, card_w, row_h,
                                        row_count);
    size_t row_index = 0U;
    settings_controller_style_row(card,
                                  (int32_t)row_index++ * row_h,
                                  card_w,
                                  row_h,
                                  settings_dashboard_style_label(
                                      speed_dashboard_style_from_snapshot(snapshot)));
    settings_speed_unit_row(card,
                            (int32_t)row_index++ * row_h,
                            card_w,
                            row_h,
                            snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
    settings_speed_source_row(card,
                              (int32_t)row_index * row_h,
                              card_w,
                              row_h,
                              snapshot->speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER ?
                                  ui_t("控制器", "Controller") : "GPS");
#endif
    lv_obj_update_layout(s_ui.settings_detail);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
}

void settings_show_controller_detail(void)
{
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const bool online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    const bool controller_synced =
        snapshot->controller_param_source ==
        (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_CONTROLLER;
    const bool values_editable = online && !controller_synced;
    const size_t main_row_count = online ? 3U : 1U;
    char ble_status[ESP_BMS_BMS_SCAN_NAME_LEN + 1U] = { 0 };
    char tire[48] = { 0 };
    char ratio[40] = { 0 };

    lv_obj_clean(s_ui.settings_detail);
    s_ui.settings_detail_id = (uint8_t)SETTINGS_DETAIL_CONTROLLER;
    s_ui.settings_controller_view = (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    s_ui.settings_bms_ble_status = NULL;
    label_set_text_if_changed(s_ui.settings_detail_title, ui_t("控制器", "Controller"));
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    settings_bms_ble_format_status(ble_status,
                                   sizeof(ble_status),
                                   snapshot,
                                   SETTINGS_BLE_SOURCE_CONTROLLER,
                                   false);
    const settings_detail_row_t rows[] = {
        { "控制器连接", "Controller link", ble_status, ble_status,
          ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND, SETTINGS_SYSTEM_VIEW_ROOT },
    };
    const settings_detail_row_t type_row = {
        "控制器类型", "Controller type", "远驱", "FarDriver", ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT,
    };

    lv_obj_t *card = settings_list_card(s_ui.settings_detail,
                                        card_x,
                                        12,
                                        card_w,
                                        row_h,
                                        main_row_count);
    size_t visible_index = 0U;
    for (size_t index = 0; index < ARRAY_SIZE(rows); ++index) {
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
                           controller_synced ? ui_t("%u-%u-%u 控制器同步", "%u-%u-%u synced") : "%u-%u-%u",
                           snapshot->controller_tire_rim_inch,
                           snapshot->controller_tire_aspect_percent,
                           snapshot->controller_tire_width_mm);
        } else if (snapshot->controller_param_source ==
                   (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LEGACY_WHEEL) {
            (void)snprintf(tire,
                           sizeof(tire),
                           ui_t("旧周长 %u mm", "Old circ. %u mm"),
                           snapshot->controller_wheel_circumference_mm);
        } else {
            (void)snprintf(tire, sizeof(tire), "%s", ui_t("未设置", "Unset"));
        }
        (void)snprintf(ratio,
                       sizeof(ratio),
                       controller_synced ? ui_t("%u.%02u 控制器同步", "%u.%02u synced") : "%u.%02u",
                       snapshot->controller_gear_ratio_centi / 100U,
                       snapshot->controller_gear_ratio_centi % 100U);
        settings_controller_value_row(card,
                                      (int32_t)visible_index++ * row_h,
                                      card_w,
                                      row_h,
                                      ui_t("轮胎规格", "Tire size"),
                                      tire,
                                      SETTINGS_CONTROLLER_VIEW_TIRE_EDIT,
                                      values_editable);
        settings_controller_value_row(card,
                                      (int32_t)visible_index * row_h,
                                      card_w,
                                      row_h,
                                      ui_t("传动比", "Gear ratio"),
                                      ratio,
                                      SETTINGS_CONTROLLER_VIEW_RATIO_EDIT,
                                      values_editable);
    }
    lv_obj_t *type_card = settings_list_card(s_ui.settings_detail,
                                             card_x,
                                             12 + ((int32_t)main_row_count * row_h) + 8,
                                             card_w,
                                             row_h,
                                             1U);
    settings_detail_row(type_card, 0, 0, card_w, row_h, &type_row);
    lv_obj_update_layout(s_ui.settings_detail);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
}

void settings_show_bms_bind_confirm(const esp_bms_bms_scan_candidate_t *candidate)
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
                   candidate->has_name && candidate->name[0] != '\0' ? candidate->name
                                                                        : ui_t("设备", "Device"));

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
    lv_label_set_text(title, ui_t("蓝牙连接", "Bluetooth"));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    lv_obj_t *name = label(dialog, 12, 40, dialog_w - 24, 22, &settings_zh_13);
    lv_label_set_text_fmt(name, ui_t("连接 %s ?", "Connect %s ?"), s_ui.settings_bms_confirm_name);
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

const settings_detail_row_t *settings_detail_rows_for_id(settings_detail_id_t detail_id,
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

lv_obj_t *settings_detail_row(lv_obj_t *parent,
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
    const lv_font_t *title_font = settings_title_font();
    const lv_font_t *subtitle_font = settings_subtitle_font();
    const int32_t title_h = (int32_t)title_font->line_height + 4;
    const int32_t subtitle_h = (int32_t)subtitle_font->line_height + 4;
    const int32_t text_gap = has_subtitle ? 1 : 0;
    const int32_t total_text_h = title_h + (has_subtitle ? text_gap + subtitle_h : 0);
    const int32_t text_y = total_text_h < h ? (h - total_text_h) / 2 : 0;
    const int32_t action_w =
        has_action ? settings_scaled_px(has_switch ? 54 : 42) : settings_scaled_px(24);
    const int32_t text_w = w - settings_scaled_px(12) - action_w;

    lv_obj_t *title = label(box, settings_scaled_px(12), text_y, text_w, title_h, title_font);
    lv_label_set_text(title, row ? ui_t(row->title, row->title_en) : "");
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    if (has_subtitle) {
        const char *subtitle_text =
            row->system_view == SETTINGS_SYSTEM_VIEW_LEVEL_POSITION
                ? quick_level_position_text()
                : ui_t(row->subtitle, row->subtitle_en);
        lv_obj_t *subtitle = label(box,
                                   settings_scaled_px(12),
                                   text_y + title_h + text_gap,
                                   text_w,
                                   subtitle_h,
                                   subtitle_font);
        lv_label_set_text(subtitle, subtitle_text);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    }

    if (has_switch) {
        const int32_t switch_w = settings_scaled_px(34);
        const int32_t switch_h = settings_scaled_px(18);
        const int32_t switch_slot_w = settings_scaled_px(54);
        settings_detail_switch(box,
                               w - switch_slot_w + ((switch_slot_w - switch_w) / 2),
                               (h - switch_h) / 2,
                               settings_detail_action_switch_on(row->action));
    } else if (has_action) {
        lv_obj_t *arrow = label(box, w - 26, 0, 16, 18, settings_disclosure_font());
        lv_label_set_text(arrow, ">");
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    }
    return box;
}

void settings_show_system_slider(quick_level_kind_t kind)
{
    const int32_t page_w = s_ui.width - 24;
    const int32_t card_h = s_ui.width < s_ui.height ? 150 : 130;
    lv_obj_t *card = panel(s_ui.settings_detail, 12, 16, page_w, card_h, COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);

    const char *title_text = kind == QUICK_LEVEL_VOLUME ? ui_t("提示音量", "Beep volume")
                                                        : ui_t("屏幕亮度", "Brightness");
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

void settings_show_system_position(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const char *labels[QUICK_LEVEL_POSITION_COUNT] = {
        ui_t("中间", "Middle"),
        portrait ? ui_t("右边", "Right") : ui_t("下面", "Bottom"),
        portrait ? ui_t("左边", "Left") : ui_t("上面", "Top"),
    };
    lv_obj_t *description = label(s_ui.settings_detail, 12, 18, s_ui.width - 24, 24, &settings_zh_13);
    lv_label_set_text(description, ui_t("选择快捷调节条出现的位置", "Choose where the quick slider appears"));
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

lv_obj_t *settings_option_card(lv_obj_t *parent,
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

    const int32_t text_x = settings_scaled_px(12);
    const lv_font_t *title_font = settings_title_font();
    const lv_font_t *subtitle_font = settings_subtitle_font();
    const int32_t title_h = (int32_t)title_font->line_height + 4;
    const int32_t subtitle_h = (int32_t)subtitle_font->line_height + 4;
    const char *subtitle_text = option ? ui_t(option->subtitle, option->subtitle_en) : "";
    const bool show_subtitle = h >= 42 && subtitle_text[0] != '\0';
    const int32_t text_gap = show_subtitle ? 1 : 0;
    const int32_t total_text_h = title_h + (show_subtitle ? text_gap + subtitle_h : 0);
    const int32_t title_y = total_text_h < h ? (h - total_text_h) / 2 : 0;
    lv_obj_t *title = label(box,
                            text_x,
                            title_y,
                            w - text_x - settings_scaled_px(30),
                            title_h,
                            title_font);
    lv_label_set_text(title, option ? ui_t(option->title, option->title_en) : "");
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(title, COLOR_SETTINGS_TEXT, LV_PART_MAIN);

    if (show_subtitle) {
        lv_obj_t *subtitle = label(box,
                                   text_x,
                                   title_y + title_h + text_gap,
                                   w - text_x - settings_scaled_px(30),
                                   subtitle_h,
                                   subtitle_font);
        lv_label_set_text(subtitle, subtitle_text);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(subtitle, COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    }

    lv_obj_t *arrow = label(box,
                            w - 22,
                            0,
                            14,
                            16,
                            settings_disclosure_font());
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_MUTED, LV_PART_MAIN);

    return box;
}

bool settings_ble_candidate_rows_changed(
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

bool settings_controller_candidate_rows_changed(
    const esp_bms_dashboard_snapshot_t *previous,
    const esp_bms_dashboard_snapshot_t *current)
{
    return settings_ble_candidate_rows_changed(previous->controller_scan_candidates,
                                               previous->controller_scan_candidate_count,
                                               current->controller_scan_candidates,
                                               current->controller_scan_candidate_count);
}

bool settings_controller_view_changed(const esp_bms_dashboard_snapshot_t *previous,
                                             const esp_bms_dashboard_snapshot_t *current,
                                             bool had_previous)
{
    return !had_previous ||
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
