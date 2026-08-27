/*
 * UI 模块: screen
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

/* 文件内前向声明 */
static void page_transition_hide(void);

static void invalidate_dashboard_viewport(void)
{
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE
    if (!s_ui.root ||
        (UI_FLAG(LAST_SNAPSHOT_VALID) &&
         speed_dashboard_style_from_snapshot(&s_ui.last_snapshot) ==
             ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE)) {
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

int32_t page_target_scroll_x(esp_bms_lvgl_page_t page)
{
    int32_t index = 1;
    if (page == ESP_BMS_LVGL_PAGE_BATTERY) {
        return 0;
    }
    if (s_ui.speed_page_renderable) {
        if (page == ESP_BMS_LVGL_PAGE_CONTROLLER || page == ESP_BMS_LVGL_PAGE_GPS) {
            return s_ui.width;
        }
        ++index;
    }
#if ESP_BMS_FEATURE_CAST
    if (page == ESP_BMS_LVGL_PAGE_CAST) {
        return s_ui.width * index;
    }
    ++index;
#endif
#if MUSIC_PAGE_ENABLED
    if (page == ESP_BMS_LVGL_PAGE_MUSIC) {
        return s_ui.width * index;
    }
#endif
    return 0;
}

int32_t page_last_scroll_x(void)
{
#if MUSIC_PAGE_ENABLED
    return page_target_scroll_x(ESP_BMS_LVGL_PAGE_MUSIC);
#elif ESP_BMS_FEATURE_CAST
    return page_target_scroll_x(ESP_BMS_LVGL_PAGE_CAST);
#else
    return page_target_scroll_x(ESP_BMS_LVGL_PAGE_GPS);
#endif
}

esp_bms_lvgl_page_t page_from_scroll_x(int32_t scroll_x)
{
    int32_t index = (scroll_x + (s_ui.width / 2)) / s_ui.width;
    if (index <= 0) {
        return ESP_BMS_LVGL_PAGE_BATTERY;
    }
    if (s_ui.speed_page_renderable) {
        if (index == 1) {
            return ESP_BMS_LVGL_PAGE_GPS;
        }
        --index;
    }
#if ESP_BMS_FEATURE_CAST
    if (index == 1) {
        return ESP_BMS_LVGL_PAGE_CAST;
    }
    --index;
#endif
#if MUSIC_PAGE_ENABLED
    if (index == 1) {
        return ESP_BMS_LVGL_PAGE_MUSIC;
    }
#endif
    return ESP_BMS_LVGL_PAGE_BATTERY;
}

static const char *page_transition_title(esp_bms_lvgl_page_t page)
{
    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        return "BMS";
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        return ui_t("仪表", "Dashboard");
    case ESP_BMS_LVGL_PAGE_CAST:
        return ui_t("投屏", "Cast");
    case ESP_BMS_LVGL_PAGE_MUSIC:
#if MEDIA_HID_PAGE_ENABLED
        return "HID";
#else
        return "MUSIC";
#endif
    default:
        return "";
    }
}

bool page_transition_active(void)
{
    return s_ui.page_transition_battery &&
           !lv_obj_has_flag(s_ui.page_transition_battery, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *page_transition_card_for_page(esp_bms_lvgl_page_t page)
{
    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        return s_ui.page_transition_battery_card;
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        return s_ui.page_transition_gps_card;
    case ESP_BMS_LVGL_PAGE_CAST:
        return s_ui.page_transition_cast_card;
    case ESP_BMS_LVGL_PAGE_MUSIC:
        return s_ui.page_transition_music_card;
    default:
        return NULL;
    }
}

static void page_transition_card_set_rect(lv_obj_t *card,
                                          int32_t x,
                                          int32_t y,
                                          int32_t width,
                                          int32_t height)
{
    if (!card) {
        return;
    }
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
}

static void page_transition_card_set_full(lv_obj_t *card)
{
    page_transition_card_set_rect(card, 0, 0, s_ui.width, s_ui.height);
}

static void page_transition_card_set_compact(lv_obj_t *card)
{
    page_transition_card_set_rect(card,
                                  PAGE_TRANSITION_CARD_MARGIN,
                                  PAGE_TRANSITION_CARD_MARGIN,
                                  s_ui.width - (PAGE_TRANSITION_CARD_MARGIN * 2),
                                  s_ui.height - (PAGE_TRANSITION_CARD_MARGIN * 2));
}

static void page_transition_card_anim_cb(void *var, int32_t value)
{
    lv_obj_t *card = (lv_obj_t *)var;
    if (card != s_ui.page_transition_anim_card) {
        return;
    }
    page_transition_card_set_rect(
        card,
        s_ui.page_transition_anim_from_x +
            ((s_ui.page_transition_anim_to_x - s_ui.page_transition_anim_from_x) * value) / 100,
        s_ui.page_transition_anim_from_y +
            ((s_ui.page_transition_anim_to_y - s_ui.page_transition_anim_from_y) * value) / 100,
        s_ui.page_transition_anim_from_w +
            ((s_ui.page_transition_anim_to_w - s_ui.page_transition_anim_from_w) * value) / 100,
        s_ui.page_transition_anim_from_h +
            ((s_ui.page_transition_anim_to_h - s_ui.page_transition_anim_from_h) * value) / 100);
}

static void page_transition_card_animation_cancel(void)
{
    if (s_ui.page_transition_anim_card) {
        lv_anim_delete(s_ui.page_transition_anim_card, page_transition_card_anim_cb);
    }
    s_ui.page_transition_anim_card = NULL;
    s_ui.page_transition_expanding = false;
}


static void page_transition_card_anim_completed_cb(lv_anim_t *anim)
{
    if (lv_anim_get_user_data(anim) != s_ui.page_transition_anim_card) {
        return;
    }
    const bool expanding = s_ui.page_transition_expanding;
    s_ui.page_transition_anim_card = NULL;
    s_ui.page_transition_expanding = false;
    if (expanding) {
        page_transition_hide();
        UI_SET_FLAG(SETTLING, false);
        flush_deferred_dashboard_snapshot();
    }
}

static void page_transition_card_animate(lv_obj_t *card, bool expanding)
{
    if (!card) {
        return;
    }

    lv_obj_update_layout(card);
    page_transition_card_animation_cancel();
    s_ui.page_transition_anim_card = card;
    s_ui.page_transition_expanding = expanding;
    s_ui.page_transition_anim_from_x = lv_obj_get_x(card);
    s_ui.page_transition_anim_from_y = lv_obj_get_y(card);
    s_ui.page_transition_anim_from_w = lv_obj_get_width(card);
    s_ui.page_transition_anim_from_h = lv_obj_get_height(card);
    s_ui.page_transition_anim_to_x = expanding ? 0 : PAGE_TRANSITION_CARD_MARGIN;
    s_ui.page_transition_anim_to_y = expanding ? 0 : PAGE_TRANSITION_CARD_MARGIN;
    s_ui.page_transition_anim_to_w = expanding
                                         ? s_ui.width
                                         : s_ui.width - (PAGE_TRANSITION_CARD_MARGIN * 2);
    s_ui.page_transition_anim_to_h = expanding
                                         ? s_ui.height
                                         : s_ui.height - (PAGE_TRANSITION_CARD_MARGIN * 2);
    if (s_ui.page_transition_anim_from_x == s_ui.page_transition_anim_to_x &&
        s_ui.page_transition_anim_from_y == s_ui.page_transition_anim_to_y &&
        s_ui.page_transition_anim_from_w == s_ui.page_transition_anim_to_w &&
        s_ui.page_transition_anim_from_h == s_ui.page_transition_anim_to_h) {
        if (expanding) {
            s_ui.page_transition_anim_card = NULL;
            s_ui.page_transition_expanding = false;
            page_transition_hide();
            UI_SET_FLAG(SETTLING, false);
            flush_deferred_dashboard_snapshot();
        }
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, card);
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, PAGE_TRANSITION_CARD_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, page_transition_card_anim_cb);
    lv_anim_set_completed_cb(&anim, page_transition_card_anim_completed_cb);
    lv_anim_set_user_data(&anim, card);
    lv_anim_start(&anim);
}

static void page_transition_hide(void)
{
    page_transition_card_animation_cancel();
    if (!s_ui.page_transition_battery) {
        return;
    }

    page_transition_card_set_full(s_ui.page_transition_battery_card);
    page_transition_card_set_full(s_ui.page_transition_gps_card);
    page_transition_card_set_full(s_ui.page_transition_cast_card);
    page_transition_card_set_full(s_ui.page_transition_music_card);
    set_obj_hidden(s_ui.battery_page, false);
    set_obj_hidden(s_ui.gps_page, false);
    set_obj_hidden(s_ui.cast_page, false);
    set_obj_hidden(s_ui.music_page, false);
    set_obj_hidden(s_ui.page_transition_battery, true);
    set_obj_hidden(s_ui.page_transition_gps, true);
    set_obj_hidden(s_ui.page_transition_cast, true);
    set_obj_hidden(s_ui.page_transition_music, true);
}

void page_transition_show(void)
{
    if (!s_ui.page_transition_battery || settings_view_is_visible() || UI_FLAG(SCREEN_LOCKED) ||
        UI_FLAG(QUICK_PANEL_OPEN) || s_ui.boot_active) {
        return;
    }
    if (page_transition_active()) {
        return;
    }

    lv_obj_set_x(s_ui.page_transition_battery,
                 page_target_scroll_x(ESP_BMS_LVGL_PAGE_BATTERY));
    lv_obj_set_x(s_ui.page_transition_gps,
                 page_target_scroll_x(ESP_BMS_LVGL_PAGE_GPS));
    if (s_ui.page_transition_cast) {
        lv_obj_set_x(s_ui.page_transition_cast,
                     page_target_scroll_x(ESP_BMS_LVGL_PAGE_CAST));
    }
    if (s_ui.page_transition_music) {
        lv_obj_set_x(s_ui.page_transition_music,
                     page_target_scroll_x(ESP_BMS_LVGL_PAGE_MUSIC));
    }
    set_obj_hidden(s_ui.page_transition_battery, false);
    set_obj_hidden(s_ui.page_transition_gps, false);
    set_obj_hidden(s_ui.page_transition_cast, false);
    set_obj_hidden(s_ui.page_transition_music, false);
    page_transition_card_set_full(s_ui.page_transition_battery_card);
    page_transition_card_set_full(s_ui.page_transition_gps_card);
    page_transition_card_set_full(s_ui.page_transition_cast_card);
    page_transition_card_set_full(s_ui.page_transition_music_card);
    lv_obj_t *current_card = page_transition_card_for_page(s_ui.page);
    if (current_card != s_ui.page_transition_battery_card) {
        page_transition_card_set_compact(s_ui.page_transition_battery_card);
    }
    if (current_card != s_ui.page_transition_gps_card) {
        page_transition_card_set_compact(s_ui.page_transition_gps_card);
    }
    if (current_card != s_ui.page_transition_cast_card) {
        page_transition_card_set_compact(s_ui.page_transition_cast_card);
    }
    if (current_card != s_ui.page_transition_music_card) {
        page_transition_card_set_compact(s_ui.page_transition_music_card);
    }
    set_obj_hidden(s_ui.battery_page, true);
    set_obj_hidden(s_ui.gps_page, true);
    set_obj_hidden(s_ui.cast_page, true);
    set_obj_hidden(s_ui.music_page, true);
    page_transition_card_animate(current_card, false);
}

void page_transition_expand(esp_bms_lvgl_page_t page)
{
    dashboard_pages_release_except(page);
    dashboard_page_content_ensure(page);
    if (!page_transition_active()) {
        UI_SET_FLAG(SETTLING, false);
        flush_deferred_dashboard_snapshot();
        return;
    }

    lv_obj_t *card = page_transition_card_for_page(page);
    if (!card) {
        page_transition_hide();
        UI_SET_FLAG(SETTLING, false);
        flush_deferred_dashboard_snapshot();
        return;
    }

    if (card == s_ui.page_transition_anim_card && !s_ui.page_transition_expanding) {
        page_transition_card_set_compact(card);
    }
    UI_SET_FLAG(SETTLING, true);
    page_transition_card_animate(card, true);
}

static lv_obj_t *page_transition_page_create(lv_obj_t *parent,
                                              esp_bms_lvgl_page_t page)
{
    lv_obj_t *transition = lv_obj_create(parent);
    clear_style(transition);
    lv_obj_set_pos(transition, page_target_scroll_x(page), 0);
    lv_obj_set_size(transition, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_opa(transition, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(transition, LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(transition, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(transition);
    clear_style(card);
    page_transition_card_set_full(card);
    lv_obj_set_style_bg_color(card, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, PAGE_TRANSITION_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label(card,
                            0,
                            0,
                            lv_pct(100),
                            settings_zh_16.line_height,
                            &settings_zh_16);
    lv_label_set_text(title, page_transition_title(page));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        s_ui.page_transition_battery_card = card;
        break;
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        s_ui.page_transition_gps_card = card;
        break;
    case ESP_BMS_LVGL_PAGE_CAST:
        s_ui.page_transition_cast_card = card;
        break;
    case ESP_BMS_LVGL_PAGE_MUSIC:
        s_ui.page_transition_music_card = card;
        break;
    default:
        break;
    }
    return transition;
}

static void page_transition_create(lv_obj_t *parent)
{
    s_ui.page_transition_battery = page_transition_page_create(parent,
                                                                ESP_BMS_LVGL_PAGE_BATTERY);
    s_ui.page_transition_gps = page_transition_page_create(parent,
                                                            ESP_BMS_LVGL_PAGE_GPS);
#if ESP_BMS_FEATURE_CAST
    s_ui.page_transition_cast = page_transition_page_create(parent,
                                                             ESP_BMS_LVGL_PAGE_CAST);
#else
    s_ui.page_transition_cast = NULL;
#endif
#if MUSIC_PAGE_ENABLED
    s_ui.page_transition_music = page_transition_page_create(parent,
                                                              ESP_BMS_LVGL_PAGE_MUSIC);
#else
    s_ui.page_transition_music = NULL;
#endif
}

void finish_page_scroll_state(bool flush_snapshot)
{
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
        s_ui.page = page_from_scroll_x(lv_obj_get_scroll_x(s_ui.pages));
        lv_obj_scroll_to_x(s_ui.pages, page_target_scroll_x(s_ui.page), LV_ANIM_OFF);
        invalidate_dashboard_viewport();
    }

    page_transition_hide();

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

void settings_root_build(void)
{
    if (!s_ui.settings_carousel || lv_obj_get_child_count(s_ui.settings_carousel) != 0U) {
        return;
    }

    const bool portrait = s_ui.width < s_ui.height;
    const int32_t row_h = portrait ? SETTINGS_LIST_ROW_H_PORTRAIT : SETTINGS_LIST_ROW_H_LANDSCAPE;
    const int32_t list_x = SETTINGS_LIST_MARGIN_X;
    const int32_t list_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    lv_obj_t *list_card = settings_list_card(s_ui.settings_carousel,
                                             list_x,
                                             SETTINGS_LIST_PAD_Y,
                                             list_w,
                                             row_h,
                                             ARRAY_SIZE(SETTINGS_OPTIONS));
    for (uint32_t index = 0; index < ARRAY_SIZE(SETTINGS_OPTIONS); ++index) {
        settings_option_card(list_card,
                             0,
                             (int32_t)index * row_h,
                             list_w,
                             row_h,
                             &SETTINGS_OPTIONS[index]);
    }
}

void move_to_page(esp_bms_lvgl_page_t page, bool animated)
{
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER) {
        page = ESP_BMS_LVGL_PAGE_GPS;
    }
    if (page == ESP_BMS_LVGL_PAGE_GPS && !s_ui.speed_page_renderable) {
        page = ESP_BMS_LVGL_PAGE_BATTERY;
    }
    if (page == ESP_BMS_LVGL_PAGE_CAST && !ESP_BMS_FEATURE_CAST) {
        page = ESP_BMS_LVGL_PAGE_BATTERY;
    }
    if (page == ESP_BMS_LVGL_PAGE_MUSIC &&
        !MUSIC_PAGE_ENABLED) {
        page = ESP_BMS_LVGL_PAGE_BATTERY;
    }
    if (!settings_view_is_visible()) {
        dashboard_pages_release_except(page);
        dashboard_page_content_ensure(page);
    }
    s_ui.page_scroll_programmatic = !animated;
    lv_obj_stop_scroll_anim(s_ui.pages);
    const int32_t target_x = page_target_scroll_x(page);
    const int32_t current_x = lv_obj_get_scroll_x(s_ui.pages);
    if (current_x == target_x) {
        s_ui.page = page;
        UI_SET_FLAG(DRAGGING, false);
        UI_SET_FLAG(SETTLING, false);
        flush_deferred_dashboard_snapshot();
        s_ui.page_scroll_programmatic = false;
        return;
    }

    s_ui.page = page;
    UI_SET_FLAG(DRAGGING, false);
    UI_SET_FLAG(SETTLING, animated);
    lv_obj_scroll_to_x(s_ui.pages, target_x, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animated) {
        flush_deferred_dashboard_snapshot();
    }
    s_ui.page_scroll_programmatic = false;
}

void screen_unlock_timer_cancel(void)
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

void screen_lock_reapply(void)
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

void screen_lock_enter(void)
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
    if (s_ui.page_scroll_programmatic) {
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
        page_transition_show();
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
        if (s_ui.page_scroll_gesture_active && !s_ui.page_scroll_throw_frozen) {
            s_ui.drag_pages_dx = lv_obj_get_scroll_x(s_ui.pages) - s_ui.drag_start_pages_x;
        }
        if (!page_transition_active()) {
            invalidate_dashboard_viewport();
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
        const int32_t last_x = page_last_scroll_x();
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
        page_transition_expand(target);
    }
}

static void create_gps_page_content(void)
{
    snprintf(s_ui.gps_speed_buf, sizeof(s_ui.gps_speed_buf), "-");
    snprintf(s_ui.gps_speed_unit_buf, sizeof(s_ui.gps_speed_unit_buf), "km/h");
    snprintf(s_ui.gps_uptime_buf, sizeof(s_ui.gps_uptime_buf), "--:--");
    s_ui.speed_soc_buf[0] = '\0';
    snprintf(s_ui.speed_consumption_buf, sizeof(s_ui.speed_consumption_buf), "-- Wh/km");
    s_ui.speed_controller_temp_buf[0] = '\0';
    s_ui.speed_motor_temp_buf[0] = '\0';
    snprintf(s_ui.speed_gear_buf, sizeof(s_ui.speed_gear_buf), "-");
    memset(s_ui.speed_scale_buf, 0, sizeof(s_ui.speed_scale_buf));
    create_gps_dashboard();
}

static void create_cast_page_content(void)
{
#if ESP_BMS_FEATURE_CAST
    const bool portrait = s_ui.width < s_ui.height;
    lv_obj_t *cast_title = label(s_ui.cast_page,
                                 0,
                                 portrait ? 28 : 16,
                                 s_ui.width,
                                 settings_zh_16.line_height,
                                 &settings_zh_16);
    lv_label_set_text(cast_title, ui_t("扫码投屏", "Scan to cast"));
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
#endif
}

static void create_music_page_content(void)
{
    memset(s_ui.music_controls, 0, sizeof(s_ui.music_controls));
    memset(s_ui.music_control_icons, 0, sizeof(s_ui.music_control_icons));
    memset(s_ui.music_control_captions, 0, sizeof(s_ui.music_control_captions));
    s_ui.music_play_paused = false;
#if MEDIA_HID_PAGE_ENABLED
    const bool portrait = s_ui.width < s_ui.height;
    s_ui.music_title = NULL;
    const bool native_320x480 = s_ui.width == 320 && s_ui.height == 480;
    const bool native_480x320 = s_ui.width == 480 && s_ui.height == 320;
    const int32_t status_w =
        native_320x480 ? 192 : native_480x320 ? 168 : portrait ? 156 : 128;
    const int32_t status_y = native_320x480 ? 42 : portrait ? 30 : 16;
    const int32_t status_h = native_320x480 ? 32 : 28;
    lv_obj_t *status_card = panel(s_ui.music_page,
                                  (s_ui.width - status_w) / 2,
                                  status_y,
                                  status_w,
                                  status_h,
                                  COLOR_PANEL_ALT);
    lv_obj_set_style_radius(status_card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_card, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
    s_ui.music_status = label(status_card,
                              4,
                              native_320x480 ? 6 : 5,
                              status_w - 8,
                              18,
                              &media_zh_13);
    lv_obj_set_style_text_align(s_ui.music_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    const esp_bms_lvgl_action_t music_actions[MUSIC_CONTROL_COUNT] = {
        ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS,
        ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE,
        ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT,
        ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN,
        ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP,
    };
    const char *const music_icons[MUSIC_CONTROL_COUNT] = {
        LV_SYMBOL_PREV,
        LV_SYMBOL_PLAY,
        LV_SYMBOL_NEXT,
        LV_SYMBOL_VOLUME_MID " " LV_SYMBOL_MINUS,
        LV_SYMBOL_VOLUME_MID " " LV_SYMBOL_PLUS,
    };
    /* The play/pause key also answers/hangs up phone calls (standard media key
     * behaviour on Android/iOS), so the label shows both roles. */
    const char *const music_labels[MUSIC_CONTROL_COUNT] = {
        ui_t("上一首", "Prev"), ui_t("播放/接听", "Play/Answer"), ui_t("下一首", "Next"),
        ui_t("音量-", "Vol-"), ui_t("音量+", "Vol+"),
    };
    for (size_t index = 0U; index < MUSIC_CONTROL_COUNT; ++index) {
        const bool track_control = index < 3U;
        /* 480x320 横屏按钮加大：第一行 3 个 112x96，第二行 2 个 172x112 */
        const int32_t button_w = track_control
                                     ? (native_320x480 ? 84 : native_480x320 ? 112 : portrait ? 64 : 80)
                                     : (native_320x480 ? 132 : native_480x320 ? 172 : portrait ? 100 : 128);
        const int32_t button_h = track_control
                                     ? (native_320x480 ? 104 : native_480x320 ? 96 : portrait ? 72 : 62)
                                     : (native_320x480 ? 120 : native_480x320 ? 112 : portrait ? 82 : 64);
        const int32_t gap = track_control
                                ? (native_320x480 ? 12 : native_480x320 ? 14 : portrait ? 8 : 16)
                                : (native_320x480 ? 16 : native_480x320 ? 20 : portrait ? 8 : 16);
        const int32_t button_count = track_control ? 3 : 2;
        const int32_t row_index = track_control ? (int32_t)index : (int32_t)(index - 3U);
        const int32_t x = (s_ui.width - (button_w * button_count) -
                           (gap * (button_count - 1))) /
                              2 +
                          row_index * (button_w + gap);
        const int32_t y = track_control
                              ? (native_320x480 ? 136 : native_480x320 ? 62 : portrait ? 82 : 62)
                              : (native_320x480 ? 280 : native_480x320 ? 174 : portrait ? 168 : 140);
        lv_obj_t *control = panel(s_ui.music_page,
                                  x,
                                  y,
                                  button_w,
                                  button_h,
                                  track_control ? COLOR_PANEL_ALT : COLOR_SETTINGS_LIST);
        lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(control, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(control, COLOR_SETTINGS_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_opa(control, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(control, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(control, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_set_style_opa(control, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_add_event_cb(control,
                            action_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)music_actions[index]);
        const int32_t icon_y = native_480x320
                                   ? (button_h - 44) / 2 + 4
                                   : track_control
                                         ? (native_320x480 ? 20 : portrait ? 12 : 7)
                                         : (native_320x480 ? 28 : portrait ? 17 : 9);
        lv_obj_t *icon = label(control,
                               0,
                               icon_y,
                               button_w,
                               26,
                               &lv_font_montserrat_24);
        lv_label_set_text(icon, music_icons[index]);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon,
                                    track_control ? COLOR_ACCENT : COLOR_WHITE,
                                    LV_PART_MAIN);
        const int32_t caption_y = native_480x320 ? button_h - 24 : button_h - 22;
        lv_obj_t *caption = label(control,
                                  0,
                                  caption_y,
                                  button_w,
                                  16,
                                  &media_zh_13);
        lv_label_set_text(caption, music_labels[index]);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(caption, COLOR_MUTED, LV_PART_MAIN);
        s_ui.music_controls[index] = control;
        s_ui.music_control_icons[index] = icon;
        s_ui.music_control_captions[index] = caption;
    }
#endif
    set_music_page(&s_ui.last_snapshot);
}

static void create_battery_page_content(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t content_w = (portrait ? 240 : 320) - 16;

    if (bms_native_landscape_enabled()) {
        create_native_bms_dashboard();
        return;
    }
    if (bms_native_portrait_enabled()) {
        create_native_bms_portrait_dashboard();
        return;
    }

    lv_obj_t *battery_viewport = dashboard_viewport(s_ui.battery_page, portrait);
    if (portrait) {
        lv_obj_t *soc_panel = dashboard_panel(battery_viewport,
                                              8,
                                              8,
                                              108,
                                              112,
                                              COLOR_DASHBOARD_SOC_PANEL,
                                              COLOR_DASHBOARD_SOC_BORDER);
        s_ui.soc = label(soc_panel, 4, 8, 100, 30, &lv_font_montserrat_24);
        dashboard_battery_icon(soc_panel, 19, 43, 66, 22);
        s_ui.capacity = label(soc_panel, 4, 76, 100, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = dashboard_panel(battery_viewport,
                                               124,
                                               8,
                                               108,
                                               112,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        s_ui.pack_voltage = label(pack_panel, 4, 12, 100, 34, &lv_font_montserrat_28);
        dashboard_separator(pack_panel, 8, 52, 92);
        s_ui.current = label(pack_panel, 4, 58, 100, 34, &lv_font_montserrat_28);

        lv_obj_t *bms_panel = dashboard_panel(battery_viewport,
                                              8,
                                              128,
                                              108,
                                              120,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);
        s_ui.bms_error = label(bms_panel, 4, 4, 100, 12, &settings_zh_10);
        s_ui.bms_status_ok = label(bms_panel, 4, 21, 100, 16, &lv_font_montserrat_14);
        lv_label_set_text(s_ui.bms_status_ok, "OK");
        s_ui.remaining_range_separator = dashboard_separator(bms_panel, 8, 52, 92);
        s_ui.remaining_range_title = label(bms_panel, 4, 59, 100, 16, &settings_zh_13);
        lv_label_set_text(s_ui.remaining_range_title, ui_t("剩余里程", "Range"));
        s_ui.remaining_range_value = label(bms_panel, 8, 77, 68, 30, &lv_font_montserrat_24);
        s_ui.remaining_range_unit = label(bms_panel, 72, 87, 28, 16, &lv_font_montserrat_14);
        lv_label_set_text(s_ui.remaining_range_unit, "km");

        lv_obj_t *cell_panel = dashboard_panel(battery_viewport,
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
            s_ui.cell_stat_values[index] =
                label(cell_panel, 49, row_y, 53, 20, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index],
                                        LV_TEXT_ALIGN_RIGHT,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.cell_stat_values[index],
                                        COLOR_DASHBOARD_VALUE,
                                        LV_PART_MAIN);
            if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
                dashboard_separator(cell_panel, 8, row_y + 23, 92);
            }
        }

        lv_obj_t *temp_panel = dashboard_panel(battery_viewport,
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
            s_ui.temperature_values[index] =
                label(temp_panel, col_x, 34, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index],
                                        LV_TEXT_ALIGN_CENTER,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.temperature_values[index],
                                        COLOR_DASHBOARD_VALUE,
                                        LV_PART_MAIN);
        }
    } else {
        lv_obj_t *soc_panel = dashboard_panel(battery_viewport,
                                              8,
                                              8,
                                              148,
                                              84,
                                              COLOR_DASHBOARD_SOC_PANEL,
                                              COLOR_DASHBOARD_SOC_BORDER);
        s_ui.soc = label(soc_panel, 4, 3, 140, 30, &lv_font_montserrat_24);
        dashboard_battery_icon(soc_panel, 34, 35, 76, 19);
        s_ui.capacity = label(soc_panel, 4, 58, 140, 20, &lv_font_montserrat_14);

        lv_obj_t *pack_panel = dashboard_panel(battery_viewport,
                                               164,
                                               8,
                                               148,
                                               84,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        s_ui.pack_voltage = label(pack_panel, 4, 3, 140, 34, &lv_font_montserrat_28);
        dashboard_separator(pack_panel, 10, 40, 128);
        s_ui.current = label(pack_panel, 4, 44, 140, 34, &lv_font_montserrat_28);

        lv_obj_t *bms_panel = dashboard_panel(battery_viewport,
                                              8,
                                              100,
                                              148,
                                              70,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);
        s_ui.bms_error = label(bms_panel, 4, 4, 68, 12, &settings_zh_10);
        s_ui.bms_status_ok = label(bms_panel, 4, 27, 68, 16, &lv_font_montserrat_14);
        lv_label_set_text(s_ui.bms_status_ok, "OK");
        s_ui.remaining_range_separator = dashboard_separator(bms_panel, 74, 8, 1);
        lv_obj_set_size(s_ui.remaining_range_separator, 1, 54);
        s_ui.remaining_range_title = label(bms_panel, 78, 6, 64, 16, &settings_zh_13);
        lv_label_set_text(s_ui.remaining_range_title, ui_t("剩余里程", "Range"));
        s_ui.remaining_range_value = label(bms_panel, 78, 21, 64, 30, &lv_font_montserrat_24);
        s_ui.remaining_range_unit = label(bms_panel, 78, 50, 64, 16, &lv_font_montserrat_14);
        lv_label_set_text(s_ui.remaining_range_unit, "km");

        lv_obj_t *cell_panel = dashboard_panel(battery_viewport,
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
            s_ui.cell_stat_values[index] =
                label(cell_panel, 74, row_y, 62, 16, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.cell_stat_values[index],
                                        LV_TEXT_ALIGN_RIGHT,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.cell_stat_values[index],
                                        COLOR_DASHBOARD_VALUE,
                                        LV_PART_MAIN);
            if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
                dashboard_separator(cell_panel, 12, row_y + 15, 124);
            }
        }

        lv_obj_t *temp_panel = dashboard_panel(battery_viewport,
                                               8,
                                               178,
                                               304,
                                               54,
                                               COLOR_DASHBOARD_PANEL,
                                               COLOR_DASHBOARD_BORDER);
        const int32_t temp_col_w = 304 / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
        const int32_t temp_left =
            (304 - (temp_col_w * (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT)) / 2;
        for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            const int32_t col_x = temp_left + ((int32_t)index * temp_col_w);
            lv_obj_t *key = label(temp_panel, col_x, 1, temp_col_w, 18, &lv_font_montserrat_14);
            lv_label_set_text(key, DASHBOARD_TEMP_KEYS[index]);
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (index == 0U) {
                s_ui.temperature = key;
            }
            dashboard_thermometer_icon(temp_panel, col_x + (temp_col_w / 2), 17);
            s_ui.temperature_values[index] =
                label(temp_panel, col_x, 32, temp_col_w, 18, &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s_ui.temperature_values[index],
                                        LV_TEXT_ALIGN_CENTER,
                                        LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.temperature_values[index],
                                        COLOR_DASHBOARD_VALUE,
                                        LV_PART_MAIN);
        }
    }
    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.current, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_status_ok, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_ui.bms_error, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.soc, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.capacity, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.pack_voltage, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.current, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.bms_status_ok, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_title, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_value, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_unit, COLOR_ACCENT, LV_PART_MAIN);
}

static lv_obj_t *dashboard_page_shell(esp_bms_lvgl_page_t page)
{
    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        return s_ui.battery_page;
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        return s_ui.gps_page;
    case ESP_BMS_LVGL_PAGE_CAST:
        return s_ui.cast_page;
    case ESP_BMS_LVGL_PAGE_MUSIC:
        return s_ui.music_page;
    default:
        return NULL;
    }
}

bool dashboard_page_content_ready(esp_bms_lvgl_page_t page)
{
    lv_obj_t *shell = dashboard_page_shell(page);
    return shell && lv_obj_get_child_count(shell) > 0U;
}

static void dashboard_battery_pointers_reset(void)
{
    s_ui.soc = NULL;
    s_ui.soc_arc = NULL;
    s_ui.soc_battery_level = NULL;
    s_ui.pack_voltage = NULL;
    s_ui.pack_voltage_unit = NULL;
    s_ui.current = NULL;
    s_ui.current_unit = NULL;
    s_ui.capacity = NULL;
    s_ui.bms_running_time = NULL;
    s_ui.bms_cycle_capacity = NULL;
    s_ui.cell_stats = NULL;
    memset(s_ui.cell_stat_values, 0, sizeof(s_ui.cell_stat_values));
    memset(s_ui.bms_safety_values, 0, sizeof(s_ui.bms_safety_values));
    memset(s_ui.bms_safety_checks, 0, sizeof(s_ui.bms_safety_checks));
    s_ui.bms_error = NULL;
    s_ui.bms_status_ok = NULL;
    s_ui.remaining_range_separator = NULL;
    s_ui.remaining_range_title = NULL;
    s_ui.remaining_range_value = NULL;
    s_ui.remaining_range_unit = NULL;
    s_ui.temperature = NULL;
    memset(s_ui.temperature_values, 0, sizeof(s_ui.temperature_values));
    s_ui.local_battery = NULL;
    s_ui.native_bms_dashboard = false;
}

static void dashboard_gps_pointers_reset(void)
{
    s_ui.controller_page = NULL;
    s_ui.speed = NULL;
    s_ui.gps_detail = NULL;
    s_ui.gps_speed_unit = NULL;
    s_ui.speed_static_background = NULL;
    s_ui.speed_art = NULL;
    s_ui.fireblade_page = NULL;
    s_ui.fireblade_time = NULL;
    s_ui.fireblade_controller_temp = NULL;
    s_ui.fireblade_motor_temp = NULL;
    s_ui.fireblade_soc = NULL;
    s_ui.fireblade_consumption = NULL;
    s_ui.fireblade_consumption_unit = NULL;
    s_ui.fireblade_range = NULL;
    s_ui.fireblade_average_speed = NULL;
    s_ui.fireblade_average_speed_unit = NULL;
    s_ui.fireblade_date = NULL;
    s_ui.fireblade_gear = NULL;
    s_ui.fireblade_gear_unit = NULL;
    s_ui.fireblade_speed = NULL;
    s_ui.fireblade_speed_unit = NULL;
    s_ui.fireblade_needle_black = NULL;
    s_ui.fireblade_needle_red = NULL;
    s_ui.speed_soc = NULL;
    s_ui.speed_consumption = NULL;
    s_ui.speed_controller_temp = NULL;
    s_ui.speed_motor_temp = NULL;
    s_ui.speed_gear = NULL;
    memset(s_ui.speed_scale_labels, 0, sizeof(s_ui.speed_scale_labels));
    s_ui.controller_speed = NULL;
    s_ui.controller_speed_unit = NULL;
    s_ui.controller_gear = NULL;
    s_ui.controller_power = NULL;
    s_ui.controller_rpm = NULL;
    s_ui.controller_temp = NULL;
    s_ui.controller_motor_temp = NULL;
    s_ui.native_fireblade_dashboard = false;
    s_ui.fireblade_needle_signature_valid = false;
    s_ui.speed_art_signature_valid = false;
}

void dashboard_page_content_release(esp_bms_lvgl_page_t page)
{
    lv_obj_t *shell = dashboard_page_shell(page);
    if (!shell || lv_obj_get_child_count(shell) == 0U) {
        return;
    }

    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        dashboard_static_cache_release_one(&s_ui.battery_static_cache);
        lv_obj_clean(shell);
        dashboard_battery_pointers_reset();
        break;
    case ESP_BMS_LVGL_PAGE_CONTROLLER:
    case ESP_BMS_LVGL_PAGE_GPS:
        dashboard_static_cache_release_one(&s_ui.fireblade_static_cache);
        dashboard_static_cache_release_one(&s_ui.speed_static_cache);
        lv_obj_clean(shell);
        dashboard_gps_pointers_reset();
        break;
    case ESP_BMS_LVGL_PAGE_CAST:
        lv_obj_clean(shell);
        s_ui.cast_qr = NULL;
        break;
    case ESP_BMS_LVGL_PAGE_MUSIC:
        lv_obj_clean(shell);
        s_ui.music_status = NULL;
        s_ui.music_title = NULL;
        memset(s_ui.music_controls, 0, sizeof(s_ui.music_controls));
        break;
    default:
        return;
    }

#if !ESP_BMS_LVGL_UI_SIMULATOR
    ESP_LOGI(TAG,
             "[page-memory] released page=%d heap_free=%u heap_largest=%u",
             (int)page,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#endif
}

void dashboard_pages_release_except(esp_bms_lvgl_page_t page)
{
    if (page != ESP_BMS_LVGL_PAGE_BATTERY) {
        dashboard_page_content_release(ESP_BMS_LVGL_PAGE_BATTERY);
    }
    if (page != ESP_BMS_LVGL_PAGE_GPS && page != ESP_BMS_LVGL_PAGE_CONTROLLER) {
        dashboard_page_content_release(ESP_BMS_LVGL_PAGE_GPS);
    }
    if (page != ESP_BMS_LVGL_PAGE_CAST) {
        dashboard_page_content_release(ESP_BMS_LVGL_PAGE_CAST);
    }
    if (page != ESP_BMS_LVGL_PAGE_MUSIC) {
        dashboard_page_content_release(ESP_BMS_LVGL_PAGE_MUSIC);
    }
}

void dashboard_page_content_ensure(esp_bms_lvgl_page_t page)
{
    if (page == ESP_BMS_LVGL_PAGE_CONTROLLER) {
        page = ESP_BMS_LVGL_PAGE_GPS;
    }
    if (dashboard_page_content_ready(page)) {
        return;
    }

    switch (page) {
    case ESP_BMS_LVGL_PAGE_BATTERY:
        create_battery_page_content();
        break;
    case ESP_BMS_LVGL_PAGE_GPS:
        create_gps_page_content();
        break;
    case ESP_BMS_LVGL_PAGE_CAST:
        create_cast_page_content();
        break;
    case ESP_BMS_LVGL_PAGE_MUSIC:
        create_music_page_content();
        break;
    default:
        return;
    }

    if (UI_FLAG(LAST_SNAPSHOT_VALID)) {
        switch (page) {
        case ESP_BMS_LVGL_PAGE_BATTERY:
            set_dashboard(&s_ui.last_snapshot);
            break;
        case ESP_BMS_LVGL_PAGE_GPS:
            speed_dashboard_style_apply(&s_ui.last_snapshot);
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
            set_controller_dashboard(&s_ui.last_snapshot);
#endif
            set_gps_dashboard(&s_ui.last_snapshot);
            break;
        case ESP_BMS_LVGL_PAGE_CAST:
            set_cast_page(&s_ui.last_snapshot);
            break;
        case ESP_BMS_LVGL_PAGE_MUSIC:
            set_music_page(&s_ui.last_snapshot);
            break;
        default:
            break;
        }
    }

#if !ESP_BMS_LVGL_UI_SIMULATOR
    ESP_LOGI(TAG,
             "[page-memory] created page=%d heap_free=%u heap_largest=%u",
             (int)page,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#endif
}

void create_screen(lv_display_t *display)
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

    s_ui.header = panel(screen, 0, 0, s_ui.width, 20, COLOR_BG);
    lv_obj_set_style_radius(s_ui.header, 0, LV_PART_MAIN);
    s_ui.settings_button = label(s_ui.header, 6, 2, 34, 16, &lv_font_montserrat_14);
    lv_label_set_text(s_ui.settings_button, "SET");
    lv_obj_set_style_text_color(s_ui.settings_button, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.settings_button, action_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)ESP_BMS_LVGL_ACTION_SHOW_SETTINGS);
    if (portrait) {
        s_ui.gps_state = label(s_ui.header, 42, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 100, 2, 56, 16, &lv_font_montserrat_14);
        s_ui.ap_state = label(s_ui.header, 160, 2, 72, 16, &lv_font_montserrat_14);
    } else {
        s_ui.gps_state = label(s_ui.header, 48, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 106, 2, 54, 16, &lv_font_montserrat_14);
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
    if (s_native_gestures_supported) {
        lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_SCROLL_ONE);
    } else {
        lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    }
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
    s_ui.speed_page_renderable = true;

    s_ui.gps_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.gps_page);
    lv_obj_set_pos(s_ui.gps_page, s_ui.width, 0);
    lv_obj_set_size(s_ui.gps_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.gps_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.gps_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.gps_page, LV_OBJ_FLAG_SNAPPABLE);

#if ESP_BMS_FEATURE_CAST
    s_ui.cast_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.cast_page);
    lv_obj_set_pos(s_ui.cast_page, s_ui.width * 2, 0);
    lv_obj_set_size(s_ui.cast_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.cast_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.cast_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.cast_page, LV_OBJ_FLAG_SNAPPABLE);
#else
    s_ui.cast_page = NULL;
    s_ui.cast_qr = NULL;
#endif

#if MUSIC_PAGE_ENABLED
    s_ui.music_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.music_page);
    lv_obj_set_pos(s_ui.music_page, page_target_scroll_x(ESP_BMS_LVGL_PAGE_MUSIC), 0);
    lv_obj_set_size(s_ui.music_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.music_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.music_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.music_page, LV_OBJ_FLAG_SNAPPABLE);
#else
    s_ui.music_page = NULL;
    s_ui.music_status = NULL;
    s_ui.music_title = NULL;
    memset(s_ui.music_controls, 0, sizeof(s_ui.music_controls));
#endif

    create_battery_page_content();

    s_ui.staging_screen = lv_obj_create(NULL);
    s_ui.settings_page = lv_obj_create(s_ui.staging_screen);
    clear_style(s_ui.settings_page);
    lv_obj_set_pos(s_ui.settings_page, 0, settings_y);
    lv_obj_set_size(s_ui.settings_page, s_ui.width, settings_h);
    lv_obj_set_style_bg_color(s_ui.settings_page, COLOR_SETTINGS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
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

    s_ui.settings_detail = lv_obj_create(s_ui.settings_page);
    clear_style(s_ui.settings_detail);
    lv_obj_set_pos(s_ui.settings_detail, 0, 0);
    lv_obj_set_size(s_ui.settings_detail, s_ui.width, settings_h);
    lv_obj_set_style_pad_top(s_ui.settings_detail,
                             SETTINGS_DETAIL_HEADER_H,
                             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ui.settings_detail, settings_scaled_px(16), LV_PART_MAIN);
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

    const int32_t nav_x = settings_scaled_px(4);
    const int32_t nav_y = settings_scaled_px(3);
    const int32_t nav_width = settings_scaled_px(48);
    const int32_t nav_inset = settings_scaled_px(6);
    const int32_t nav_title_x = settings_scaled_px(56);
    const int32_t nav_title_y = settings_scaled_px(7);
    const int32_t nav_title_h = SETTINGS_DETAIL_HEADER_H - settings_scaled_px(12);
    lv_obj_t *detail_back = panel(s_ui.settings_detail_header,
                                  nav_x,
                                  nav_y,
                                  nav_width,
                                  SETTINGS_DETAIL_HEADER_H - nav_inset,
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
                                       settings_scaled_px(4),
                                       nav_width,
                                       SETTINGS_DETAIL_HEADER_H - settings_scaled_px(10),
                                       &lv_font_montserrat_24);
    lv_label_set_text(detail_back_icon, "<");
    lv_obj_set_style_text_align(detail_back_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(detail_back_icon, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);

    s_ui.settings_detail_title = label(s_ui.settings_detail_header,
                                       nav_title_x,
                                       nav_title_y,
                                       s_ui.width - (nav_title_x * 2),
                                       nav_title_h,
                                       settings_title_font());
    lv_label_set_text(s_ui.settings_detail_title, ui_t("设置", "Settings"));
    lv_obj_set_style_text_align(s_ui.settings_detail_title,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.settings_detail_title,
                                COLOR_SETTINGS_TEXT,
                                LV_PART_MAIN);

    s_ui.settings_boot_preview_button = panel(s_ui.settings_detail_header,
                                              s_ui.width - nav_x - nav_width,
                                              nav_y,
                                              nav_width,
                                              SETTINGS_DETAIL_HEADER_H - nav_inset,
                                              COLOR_SETTINGS_CARD);
    lv_obj_set_style_radius(s_ui.settings_boot_preview_button, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.settings_boot_preview_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.settings_boot_preview_button,
                              COLOR_SETTINGS_LIST,
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(s_ui.settings_boot_preview_button, 4);
    lv_obj_add_flag(s_ui.settings_boot_preview_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.settings_boot_preview_button,
                        settings_boot_preview_button_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *boot_preview_icon = label(s_ui.settings_boot_preview_button,
                                        0,
                                        settings_scaled_px(4),
                                        nav_width,
                                        SETTINGS_DETAIL_HEADER_H - settings_scaled_px(10),
                                        &lv_font_montserrat_24);
    lv_label_set_text(boot_preview_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_align(boot_preview_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(boot_preview_icon,
                                COLOR_SETTINGS_ACCENT,
                                LV_PART_MAIN);
    set_obj_hidden(s_ui.settings_boot_preview_button, true);
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

#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
    (void)quick_level_tile(s_ui.quick_panel,
                           quick_layout->brightness.x,
                           quick_layout->brightness.y,
                           quick_layout->brightness.w,
                           quick_layout->brightness.h,
                           QUICK_LEVEL_BRIGHTNESS,
                           85U);
#endif
#if ESP_BMS_FEATURE_AUDIO
    (void)quick_level_tile(s_ui.quick_panel,
                           quick_layout->volume.x,
                           quick_layout->volume.y,
                           quick_layout->volume.w,
                           quick_layout->volume.h,
                           QUICK_LEVEL_VOLUME,
                           65U);
#endif

    const int32_t quick_edit_size = quick_edit_button_size();
    const int32_t quick_edit_icon_size =
        settings_uses_s3_layout() ? quick_edit_size : quick_edit_size - 8;
    s_ui.quick_edit_button = panel(s_ui.quick_panel,
                                   s_ui.width - quick_edit_size - 8,
                                   8,
                                   quick_edit_size,
                                   quick_edit_size,
                                   COLOR_PANEL_ALT);
    lv_obj_set_style_radius(s_ui.quick_edit_button, 8, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.quick_edit_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.quick_edit_button, quick_edit_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.quick_edit_icon = quick_symbol_icon(s_ui.quick_edit_button,
                                             quick_edit_icon_size,
                                             quick_edit_icon_size,
                                             LV_SYMBOL_EDIT,
                                             quick_edit_icon_font());
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
    page_transition_create(s_ui.pages);
    screen_lock_create(screen);
    dashboard_pages_release_except(ESP_BMS_LVGL_PAGE_BATTERY);
    lv_obj_add_flag(s_ui.header, LV_OBJ_FLAG_HIDDEN);
}
