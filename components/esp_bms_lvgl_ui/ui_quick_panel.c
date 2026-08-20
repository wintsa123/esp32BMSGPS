/*
 * UI 模块: quick
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

/* 文件内前向声明 */
static void quick_edit_set_pressed(bool pressed);
static lv_obj_t *quick_level_tile_for_kind(quick_level_kind_t kind);

int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
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

#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
    layout->brightness = slots[0];
#endif
#if ESP_BMS_FEATURE_AUDIO
    layout->volume = slots[CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING ? 1 : 0];
#endif
    for (uint32_t index = 0; index < QUICK_PANEL_BUTTON_COUNT; ++index) {
        layout->items[index] = slots[index + QUICK_PANEL_LEVEL_COUNT];
    }
}

quick_panel_layout_t *quick_layout_ensure_current(void)
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
#if ESP_BMS_FEATURE_AUDIO
    quick_obj_apply_rect(s_ui.quick_volume_tile, &layout->volume);
#endif
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
    quick_drag_target_kind_t best_kind = QUICK_DRAG_TARGET_ITEM;
    uint8_t best_index = 0;
    int32_t best_distance = INT32_MAX;

    for (uint32_t slot = 0; slot < QUICK_PANEL_CONTROL_COUNT; ++slot) {
        quick_drag_target_kind_t kind = QUICK_DRAG_TARGET_ITEM;
        uint8_t index = (uint8_t)(slot - QUICK_PANEL_LEVEL_COUNT);
#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
        if (slot == 0U) {
            kind = QUICK_DRAG_TARGET_BRIGHTNESS;
            index = 0U;
        }
#endif
#if ESP_BMS_FEATURE_AUDIO
#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
        else if (slot == 1U) {
#else
        if (slot == 0U) {
#endif
            kind = QUICK_DRAG_TARGET_VOLUME;
            index = 0U;
        }
#endif
        quick_tile_rect_t *rect = quick_layout_rect_for_target(layout, kind, index);
        if (!rect) {
            continue;
        }
        if (quick_rect_contains_point(rect, x, y)) {
            best_kind = kind;
            best_index = index;
            break;
        }
        const int32_t distance = quick_rect_center_distance_sq(rect, x, y);
        if (distance < best_distance) {
            best_distance = distance;
            best_kind = kind;
            best_index = index;
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

static const char *quick_level_icon(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_EYE_OPEN;
}

uint8_t quick_level_current_value(quick_level_kind_t kind)
{
    return kind == QUICK_LEVEL_VOLUME ? s_ui.quick_volume_percent : s_ui.quick_brightness_percent;
}

quick_level_position_t quick_level_position(void)
{
    return s_ui.quick_level_position < QUICK_LEVEL_POSITION_COUNT ?
               (quick_level_position_t)s_ui.quick_level_position :
               QUICK_LEVEL_POSITION_MIDDLE;
}

const char *quick_level_position_text(void)
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

void quick_level_queue_value(quick_level_kind_t kind, uint8_t value, bool committed)
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

uint8_t quick_level_snap_drag_value(quick_level_kind_t kind, int32_t value)
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

void set_quick_brightness_value(uint8_t brightness_percent, bool queue, bool committed)
{
    const uint8_t clamped = clamp_brightness_percent(brightness_percent);
    const bool changed = s_ui.quick_brightness_percent != clamped;
    s_ui.quick_brightness_percent = clamped;
    if (changed && s_ui.quick_brightness_label) {
        label_set_text_if_changed(s_ui.quick_brightness_label, LV_SYMBOL_EYE_OPEN);
    }
    if (changed) {
        quick_level_overlay_update(QUICK_LEVEL_BRIGHTNESS, clamped);
    }
    if (queue) {
        quick_level_queue_value(QUICK_LEVEL_BRIGHTNESS, clamped, committed);
    }
}

void set_quick_volume_value(uint8_t volume_percent, bool queue, bool committed)
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

void refresh_quick_level_layouts(void)
{
    set_quick_brightness_value(s_ui.quick_brightness_percent ? s_ui.quick_brightness_percent : 85U, false, true);
    set_quick_volume_value(s_ui.quick_volume_percent, false, true);
}

static void quick_panel_y_anim_cb(void *var, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)var, value);
}

void quick_panel_stop_settle_anim(void)
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

int32_t quick_pull_open_threshold(void)
{
    return clamp_i32(s_ui.height / 3, QUICK_PULL_OPEN_DY, s_ui.height / 2);
}

void quick_panel_animate_to_open_state(bool open)
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

void quick_toast_cancel(void)
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

void quick_toast_show_text(const char *text)
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

void quick_toast_show_connecting(void)
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

void quick_rotate_toast_show(void)
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

void set_quick_panel_open(bool open)
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

void set_quick_edit_mode(bool edit_mode)
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

void quick_edit_event_cb(lv_event_t *event)
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
#if ESP_BMS_FEATURE_GPS
    case QUICK_ITEM_SPEED:
        return SETTINGS_DETAIL_GPS;
#endif
    case QUICK_ITEM_ROTATE:
#if !ESP_BMS_FEATURE_GPS
    case QUICK_ITEM_SPEED:
#endif
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

int32_t quick_edit_button_size(void)
{
    return settings_uses_s3_layout() ? QUICK_EDIT_BUTTON_SIZE_S3 : QUICK_EDIT_BUTTON_SIZE;
}

const lv_font_t *quick_edit_icon_font(void)
{
    return settings_uses_s3_layout() ? &lv_font_montserrat_24 : &lv_font_montserrat_14;
}

static void quick_edit_set_pressed(bool pressed)
{
    const int32_t button_size = quick_edit_button_size();
    const quick_tile_rect_t rect = {
        .x = s_ui.width - button_size - 8,
        .y = 8,
        .w = button_size,
        .h = button_size,
    };
    quick_tile_apply_press_inset(s_ui.quick_edit_button, &rect, pressed);
    const int32_t extent = quick_tile_pressed_extent(button_size, pressed);
    const int32_t icon_extent = settings_uses_s3_layout() ? extent : extent - 8;
    quick_symbol_icon_recenter(s_ui.quick_edit_icon,
                               icon_extent,
                               icon_extent,
                               LV_SYMBOL_EDIT,
                               quick_edit_icon_font());
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

void quick_panel_item_event_cb(lv_event_t *event)
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

void quick_symbol_icon_recenter(lv_obj_t *icon,
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

lv_obj_t *quick_symbol_icon(lv_obj_t *parent,
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

lv_obj_t *quick_panel_tile(lv_obj_t *parent,
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

void update_quick_item_colors(const esp_bms_dashboard_snapshot_t *snapshot)
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

void quick_level_overlay_hide(void)
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

void quick_level_event_cb(lv_event_t *event)
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

lv_obj_t *quick_level_tile(lv_obj_t *parent,
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

void quick_level_overlay_create(lv_obj_t *parent)
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

void quick_toast_create(lv_obj_t *parent)
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
