/*
 * UI 模块: settings_pickers
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

bool esp_bms_lvgl_ui_speed_dashboard_style_available(esp_bms_speed_dashboard_style_t style)
{
    switch (style) {
    case ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR:
        return ESP_BMS_FEATURE_DASHBOARD_S1000RR != 0;
    case ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER:
        return ESP_BMS_FEATURE_DASHBOARD_CONTROLLER != 0;
    case ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE:
        return ESP_BMS_FEATURE_DASHBOARD_FIREBLADE != 0;
    default:
        return false;
    }
}

esp_bms_speed_dashboard_style_t esp_bms_lvgl_ui_default_speed_dashboard_style(void)
{
    static const esp_bms_speed_dashboard_style_t styles[] = {
        ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER,
        ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR,
        ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE,
    };
    for (size_t index = 0U; index < ARRAY_SIZE(styles); ++index) {
        if (esp_bms_lvgl_ui_speed_dashboard_style_available(styles[index])) {
            return styles[index];
        }
    }
    return ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR;
}

esp_bms_speed_dashboard_style_t speed_dashboard_style_from_snapshot(
    const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot) {
        return esp_bms_lvgl_ui_default_speed_dashboard_style();
    }
    esp_bms_speed_dashboard_style_t style = snapshot->speed_dashboard_style;
    if (style == ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR &&
        SNAPSHOT_FLAG(snapshot, CONTROLLER_PAGE_ENABLED)) {
        style = ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
    }
    return esp_bms_lvgl_ui_speed_dashboard_style_available(style)
               ? style
               : esp_bms_lvgl_ui_default_speed_dashboard_style();
}


static const settings_dashboard_style_option_t SETTINGS_DASHBOARD_STYLE_OPTIONS[] = {
    { ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR, "宝马 S1000RR" },
    { ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER, "控制器监控" },
    { ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE, "本田火刃" },
};

const char *settings_dashboard_style_label(esp_bms_speed_dashboard_style_t style)
{
    for (size_t index = 0U; index < ARRAY_SIZE(SETTINGS_DASHBOARD_STYLE_OPTIONS); ++index) {
        if (SETTINGS_DASHBOARD_STYLE_OPTIONS[index].style == style) {
            return SETTINGS_DASHBOARD_STYLE_OPTIONS[index].label;
        }
    }
    return "--";
}


static void settings_controller_style_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    ESP_LOGI(TAG, "[controller-ui] open speed dashboard style list");
    settings_show_controller_style_picker();
}

void settings_controller_style_option_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }

    const esp_bms_speed_dashboard_style_t selected =
        (esp_bms_speed_dashboard_style_t)(uintptr_t)lv_event_get_user_data(event);
    if (!esp_bms_lvgl_ui_speed_dashboard_style_available(selected)) {
        return;
    }

    const esp_bms_speed_dashboard_style_t current = speed_dashboard_style_from_snapshot(
        settings_current_snapshot());
    if (selected != current) {
        ESP_LOGI(TAG,
                 "[controller-ui] speed dashboard style selected: %s",
                 settings_dashboard_style_label(selected));
        queue_action_with_commit(ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE, true);
        s_ui.pending_event.numeric_delta = (int16_t)selected;
        ACTION_EVENT_SET_FLAG(&s_ui.pending_event, NUMERIC_DELTA_VALID, true);
    }
    lv_indev_wait_release(lv_indev_active());
}

void settings_show_controller_style_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = settings_scaled_px(portrait ? 8 : 6);
    const int32_t first_y = settings_scaled_px(12);
    const esp_bms_speed_dashboard_style_t current = speed_dashboard_style_from_snapshot(
        settings_current_snapshot());

    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_STYLE_LIST;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "选择仪表 UI");
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    size_t visible_index = 0U;
    for (size_t index = 0U; index < ARRAY_SIZE(SETTINGS_DASHBOARD_STYLE_OPTIONS); ++index) {
        const settings_dashboard_style_option_t *option = &SETTINGS_DASHBOARD_STYLE_OPTIONS[index];
        if (!esp_bms_lvgl_ui_speed_dashboard_style_available(option->style)) {
            continue;
        }
        const bool active = option->style == current;
        lv_obj_t *row = panel(s_ui.settings_detail,
                              card_x,
                              first_y + ((int32_t)visible_index++ * (row_h + gap)),
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
                            settings_controller_style_option_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)option->style);

        const lv_font_t *text_font = settings_title_font();
        const int32_t text_h = (int32_t)text_font->line_height + 4;
        lv_obj_t *text = label(row,
                               12,
                               (row_h - text_h) / 2,
                               card_w - 52,
                               text_h,
                               text_font);
        lv_label_set_text(text, option->label);
        lv_obj_set_style_text_color(text,
                                    active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_TEXT,
                                    LV_PART_MAIN);
        if (active) {
            lv_obj_t *check = label(row,
                                    card_w - 38,
                                    (row_h - 20) / 2,
                                    26,
                                    20,
                                    &lv_font_montserrat_14);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);
        }
    }
}

static const char *const SETTINGS_SPEED_UNIT_LABELS[] = {
    "km/h",
    "mph",
};


#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
static const settings_speed_source_option_t SETTINGS_SPEED_SOURCE_OPTIONS[] = {
    { ESP_BMS_SPEED_SOURCE_GPS, "GPS" },
    { ESP_BMS_SPEED_SOURCE_CONTROLLER, "控制器" },
};
#endif

void settings_speed_unit_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    settings_show_speed_unit_picker();
}

void settings_speed_unit_option_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }
    const size_t selected = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (selected >= ARRAY_SIZE(SETTINGS_SPEED_UNIT_LABELS)) {
        return;
    }
    const size_t current = settings_current_snapshot()->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                               ? 1U
                               : 0U;
    if (selected != current) {
        queue_action_with_commit(ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT, true);
    }
    lv_indev_wait_release(lv_indev_active());
}

void settings_show_speed_unit_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = settings_scaled_px(portrait ? 8 : 6);
    const size_t current = settings_current_snapshot()->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                               ? 1U
                               : 0U;

    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_SPEED_UNIT_LIST;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "速度单位");
    settings_navigation_set_hidden(false, false);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_SPEED_UNIT_LABELS); ++index) {
        const bool active = index == current;
        lv_obj_t *row = panel(s_ui.settings_detail,
                              card_x,
                              settings_scaled_px(12) + ((int32_t)index * (row_h + gap)),
                              card_w,
                              row_h,
                              COLOR_SETTINGS_CARD);
        lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, active ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row,
                                      active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                      LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        settings_add_swipe_handlers(row);
        lv_obj_add_event_cb(row,
                            settings_speed_unit_option_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        const lv_font_t *text_font = settings_title_font();
        const int32_t text_h = (int32_t)text_font->line_height + 4;
        lv_obj_t *text = label(row,
                               12,
                               (row_h - text_h) / 2,
                               card_w - 52,
                               text_h,
                               text_font);
        lv_label_set_text(text, SETTINGS_SPEED_UNIT_LABELS[index]);
        lv_obj_set_style_text_color(text,
                                    active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_TEXT,
                                    LV_PART_MAIN);
        if (active) {
            lv_obj_t *check = label(row,
                                    card_w - 38,
                                    (row_h - 20) / 2,
                                    26,
                                    20,
                                    &lv_font_montserrat_14);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);
        }
    }
}

#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
void settings_speed_source_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    settings_show_speed_source_picker();
}

void settings_speed_source_option_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }
    const esp_bms_speed_source_t selected =
        (esp_bms_speed_source_t)(uintptr_t)lv_event_get_user_data(event);
    if ((selected != ESP_BMS_SPEED_SOURCE_GPS &&
         selected != ESP_BMS_SPEED_SOURCE_CONTROLLER) ||
        selected == settings_current_snapshot()->speed_source) {
        return;
    }
    queue_action_with_commit(ESP_BMS_LVGL_ACTION_SET_SPEED_SOURCE, true);
    s_ui.pending_event.numeric_delta = (int16_t)selected;
    ACTION_EVENT_SET_FLAG(&s_ui.pending_event, NUMERIC_DELTA_VALID, true);
    lv_indev_wait_release(lv_indev_active());
}

void settings_show_speed_source_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = settings_scaled_px(portrait ? 8 : 6);
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const esp_bms_speed_source_t current = snapshot->speed_source;

    s_ui.settings_dashboard_view = (uint8_t)SETTINGS_DASHBOARD_VIEW_SPEED_SOURCE_LIST;
    s_ui.settings_bms_ble_status = NULL;
    lv_obj_clean(s_ui.settings_detail);
    label_set_text_if_changed(s_ui.settings_detail_title, "速度来源");
    settings_navigation_set_hidden(false, false);
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);

    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_SPEED_SOURCE_OPTIONS); ++index) {
        const settings_speed_source_option_t *option = &SETTINGS_SPEED_SOURCE_OPTIONS[index];
        const bool active = option->source == current;
        const bool available = option->source == ESP_BMS_SPEED_SOURCE_CONTROLLER ||
                               snapshot->gps_module_state ==
                                   (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE;
        lv_obj_t *row = panel(s_ui.settings_detail,
                              card_x,
                              settings_scaled_px(12) + ((int32_t)index * (row_h + gap)),
                              card_w,
                              row_h,
                              COLOR_SETTINGS_CARD);
        lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, active ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row,
                                      active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_BORDER,
                                      LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        if (available) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            settings_add_swipe_handlers(row);
            lv_obj_add_event_cb(row,
                                settings_speed_source_option_event_cb,
                                LV_EVENT_CLICKED,
                                (void *)(uintptr_t)option->source);
        } else {
            lv_obj_set_style_opa(row, LV_OPA_50, LV_PART_MAIN);
        }
        const lv_font_t *text_font = settings_title_font();
        const int32_t text_h = (int32_t)text_font->line_height + 4;
        lv_obj_t *text = label(row, 12, (row_h - text_h) / 2, card_w - 52, text_h, text_font);
        lv_label_set_text(text, option->label);
        lv_obj_set_style_text_color(text,
                                    active ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_TEXT,
                                    LV_PART_MAIN);
        if (active) {
            lv_obj_t *check = label(row, card_w - 38, (row_h - 20) / 2, 26, 20,
                                    &lv_font_montserrat_14);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, COLOR_SWITCH_ACTIVE, LV_PART_MAIN);
        }
    }
}
#endif

lv_obj_t *settings_speed_unit_row(lv_obj_t *parent,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         const char *value)
{
    const settings_detail_row_t descriptor = {
        "速度单位",
        value,
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *box = settings_detail_row(parent, 0, y, w, h, &descriptor);
    lv_obj_add_event_cb(box,
                        settings_speed_unit_button_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *arrow = label(box, w - 26, 0, 16, 18, settings_disclosure_font());
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    return box;
}

#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
lv_obj_t *settings_speed_source_row(lv_obj_t *parent,
                                           int32_t y,
                                           int32_t w,
                                           int32_t h,
                                           const char *value)
{
    const settings_detail_row_t descriptor = {
        "速度来源", value, ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *box = settings_detail_row(parent, 0, y, w, h, &descriptor);
    lv_obj_add_event_cb(box, settings_speed_source_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = label(box, w - 26, 0, 16, 18, settings_disclosure_font());
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
    return box;
}
#endif

void settings_controller_style_row(lv_obj_t *parent,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          const char *value)
{
    const settings_detail_row_t descriptor = {
        "仪表 UI",
        value,
        ESP_BMS_LVGL_ACTION_NONE,
        SETTINGS_SYSTEM_VIEW_ROOT,
    };
    lv_obj_t *box = settings_detail_row(parent, 0, y, w, h, &descriptor);
    lv_obj_add_event_cb(box,
                        settings_controller_style_button_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *arrow = label(box, w - 26, 0, 16, 18, settings_disclosure_font());
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, COLOR_SETTINGS_ACCENT, LV_PART_MAIN);
}

