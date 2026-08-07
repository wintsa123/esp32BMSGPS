/*
 * UI 模块: settings_system
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

const char *settings_bms_type_label(uint8_t type)
{
    return type < ARRAY_SIZE(SETTINGS_BMS_TYPE_LABELS) ? SETTINGS_BMS_TYPE_LABELS[type] :
                                                          SETTINGS_BMS_TYPE_LABELS[0];
}

void settings_bms_ble_refresh_rows(const esp_bms_dashboard_snapshot_t *snapshot,
                                          settings_ble_source_t source,
                                          bool scan_requested,
                                          const char *phase)
{
    if (!snapshot || !s_ui.settings_bms_ble_status || !s_ui.settings_bms_ble_empty ||
        !s_ui.settings_bms_ble_list) {
        return;
    }

    const uint8_t source_count = source == SETTINGS_BLE_SOURCE_BMS
                                     ? snapshot->bms_scan_candidate_count
                                     : snapshot->controller_scan_candidate_count;
    const esp_bms_bms_scan_candidate_t *candidates =
        source == SETTINGS_BLE_SOURCE_BMS ? snapshot->bms_scan_candidates
                                          : snapshot->controller_scan_candidates;
    const uint8_t count = source_count > ESP_BMS_BMS_SCAN_MAX_CANDIDATES
                              ? ESP_BMS_BMS_SCAN_MAX_CANDIDATES
                              : source_count;

    settings_bms_ble_format_status(s_ui.settings_bms_ble_status_text,
                                   sizeof(s_ui.settings_bms_ble_status_text),
                                   snapshot,
                                   source,
                                   scan_requested);
    lv_label_set_text_static(s_ui.settings_bms_ble_status, s_ui.settings_bms_ble_status_text);

    if (count == 0U) {
        (void)snprintf(s_ui.settings_bms_ble_empty_text,
                       sizeof(s_ui.settings_bms_ble_empty_text),
                       "%s",
                       scan_requested ? "扫描..."
                                      : source == SETTINGS_BLE_SOURCE_BMS ? "未发现保护板"
                                                                          : "未发现控制器");
        lv_label_set_text_static(s_ui.settings_bms_ble_empty, s_ui.settings_bms_ble_empty_text);
    }
    set_obj_hidden(s_ui.settings_bms_ble_empty, count != 0U);

    size_t used = 0U;
    s_ui.settings_bms_ble_list_text[0] = '\0';
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SCAN_MAX_CANDIDATES; ++index) {
        if (index >= count || used >= sizeof(s_ui.settings_bms_ble_list_text)) {
            break;
        }
        const esp_bms_bms_scan_candidate_t *candidate = &candidates[index];
        char fallback_name[16] = { 0 };
        const bool has_name = candidate->has_name && candidate->name[0] != '\0';
        if (!has_name) {
            (void)snprintf(fallback_name, sizeof(fallback_name), "设备 %u", (unsigned)index + 1U);
        }
        const char *name = has_name ? candidate->name : fallback_name;
        const int written = candidate->rssi > INT8_MIN
                                ? snprintf(s_ui.settings_bms_ble_list_text + used,
                                           sizeof(s_ui.settings_bms_ble_list_text) - used,
                                           "%s%s  %d dBm",
                                           index == 0U ? "" : "\n",
                                           name,
                                           (int)candidate->rssi)
                                : snprintf(s_ui.settings_bms_ble_list_text + used,
                                           sizeof(s_ui.settings_bms_ble_list_text) - used,
                                           "%s%s  --",
                                           index == 0U ? "" : "\n",
                                           name);
        if (written < 0) {
            break;
        }
        const size_t remaining = sizeof(s_ui.settings_bms_ble_list_text) - used;
        used += (size_t)written < remaining ? (size_t)written : remaining - 1U;
    }
    lv_label_set_text_static(s_ui.settings_bms_ble_list, s_ui.settings_bms_ble_list_text);
    set_obj_hidden(s_ui.settings_bms_ble_list, count == 0U);

    settings_bms_ble_log_memory(phase, source, count);
}

bool settings_detail_action_uses_switch(esp_bms_lvgl_action_t action)
{
    return action == ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING ||
           action == ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION ||
           action == ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE;
}

bool settings_detail_action_switch_on(esp_bms_lvgl_action_t action)
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

static void settings_detail_switch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    /* Forward the click to the parent row so settings_detail_action_event_cb fires. */
    lv_obj_t *track = lv_event_get_current_target(event);
    lv_obj_t *box = lv_obj_get_parent(track);
    if (box) {
        lv_obj_send_event(box, LV_EVENT_CLICKED, NULL);
    }
}

void settings_detail_switch(lv_obj_t *parent, int32_t x, int32_t y, bool enabled)
{
    const int32_t w = settings_scaled_px(34);
    const int32_t h = settings_scaled_px(18);
    const int32_t knob = settings_scaled_px(14);
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
    lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(track, settings_detail_switch_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *thumb = lv_obj_create(track);
    clear_style(thumb);
    lv_obj_set_size(thumb, knob, knob);
    lv_obj_set_pos(thumb,
                   enabled ? (w - knob - settings_scaled_px(2)) : settings_scaled_px(2),
                   settings_scaled_px(2));
    lv_obj_set_style_radius(thumb, knob / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(thumb, enabled ? COLOR_WHITE : COLOR_SETTINGS_MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

void settings_restore_popup_close(void)
{
    if (!s_ui.settings_restore_popup) {
        return;
    }
    lv_obj_delete(s_ui.settings_restore_popup);
    s_ui.settings_restore_popup = NULL;
}

void settings_restore_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        settings_restore_popup_close();
    }
}

void settings_restore_accept_event_cb(lv_event_t *event)
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
    lv_label_set_text(message,
                      s_touch_calibration_supported ? "清除设置与屏幕校准？" : "清除设置？");
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

void settings_detail_action_event_cb(lv_event_t *event)
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

void settings_bms_type_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    ESP_LOGI(TAG, "[bms-ui] open BMS type list page");
    settings_show_bms_type_picker();
}

void settings_bms_type_option_event_cb(lv_event_t *event)
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

void settings_bms_bind_confirm_cancel(void)
{
    const settings_ble_source_t source = (settings_ble_source_t)s_ui.settings_ble_source;
    settings_bms_popup_close();
    if (source == SETTINGS_BLE_SOURCE_BMS) {
        queue_action(ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION);
    }
    settings_show_bms_ble_popup(source, false);
}

void settings_bms_bind_confirm_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    settings_bms_bind_confirm_cancel();
}

void settings_bms_bind_confirm_accept_event_cb(lv_event_t *event)
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

void settings_bms_ble_candidate_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }

    lv_point_t point = { 0 };
    lv_area_t area = { 0 };
    lv_obj_t *list = lv_event_get_target_obj(event);
    if (!list || !get_active_pointer(&point)) {
        return;
    }
    lv_obj_get_coords(list, &area);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = s_ui.width < s_ui.height ? 7 : 5;
    const int32_t relative_y = point.y - area.y1;
    if (relative_y < 0 || relative_y % (row_h + gap) >= row_h) {
        return;
    }
    const uint8_t index = (uint8_t)(relative_y / (row_h + gap));
    const esp_bms_dashboard_snapshot_t *snapshot = settings_current_snapshot();
    const uint8_t count = s_ui.settings_ble_source == (uint8_t)SETTINGS_BLE_SOURCE_BMS
                              ? snapshot->bms_scan_candidate_count
                              : snapshot->controller_scan_candidate_count;
    if (index >= count || index >= ESP_BMS_BMS_SCAN_MAX_CANDIDATES) {
        return;
    }
    const esp_bms_bms_scan_candidate_t *candidate =
        s_ui.settings_ble_source == (uint8_t)SETTINGS_BLE_SOURCE_BMS
            ? &snapshot->bms_scan_candidates[index]
            : &snapshot->controller_scan_candidates[index];
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

void settings_bms_ble_refresh_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }
    const settings_ble_source_t source = (settings_ble_source_t)s_ui.settings_ble_source;
    if (settings_bms_ble_connection_in_progress(settings_current_snapshot(), source)) {
        label_set_text_if_changed(s_ui.settings_bms_ble_status, "已取消");
        quick_toast_cancel();
        queue_action(ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION);
        ESP_LOGI(TAG, "[ble-ui] cancel BMS connection from list page");
        return;
    }
    settings_bms_ble_start_scan();
}

void settings_system_slider_event_cb(lv_event_t *event)
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

void settings_system_position_option_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || UI_FLAG(SETTINGS_SWIPE_CONSUMED)) {
        return;
    }
    s_ui.quick_level_position =
        (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    refresh_quick_level_layouts();
    settings_show_system_view(SETTINGS_SYSTEM_VIEW_LEVEL_POSITION);
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


static const settings_boot_animation_option_t SETTINGS_BOOT_ANIMATION_OPTIONS[] = {
    { ESP_BMS_BOOT_ANIMATION_CHARGE, "电量充能" },
    { ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR, "BMW S1000RR" },
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    { ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE, "HONDA Fireblade" },
#endif
};

void settings_boot_preview_timer_cancel(void)
{
    if (!s_ui.settings_boot_preview_timer) {
        return;
    }
    lv_timer_t *timer = s_ui.settings_boot_preview_timer;
    s_ui.settings_boot_preview_timer = NULL;
    lv_timer_delete(timer);
}

static const char *settings_boot_preview_status(uint8_t progress_percent)
{
    if (progress_percent < 15U) {
        return "POWER ON";
    }
    if (progress_percent < 30U) {
        return "DISPLAY READY";
    }
    if (progress_percent < 40U) {
        return "SETTINGS LOADED";
    }
    if (progress_percent < 50U) {
        return "BLE START";
    }
    if (progress_percent < 90U) {
        return "GPS CHECK";
    }
    if (progress_percent < 100U) {
        return "GPS READY";
    }
    return "SYSTEM READY";
}

static void settings_boot_preview_finish(void)
{
    esp_bms_dashboard_snapshot_t snapshot = s_ui.last_snapshot;
    settings_boot_preview_timer_cancel();
    if (s_ui.boot_active) {
        const esp_err_t ret = esp_bms_lvgl_ui_boot_finish(&snapshot);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "boot preview finish failed: %d", (int)ret);
        }
    }
    show_settings_view();
    settings_show_system_view(SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION);
}

static void settings_boot_preview_timer_cb(lv_timer_t *timer)
{
    if (timer != s_ui.settings_boot_preview_timer) {
        return;
    }
    const uint32_t elapsed_ms = lv_tick_elaps(s_ui.settings_boot_preview_started_ms);
    if (elapsed_ms >=
        SETTINGS_BOOT_PREVIEW_DURATION_MS + SETTINGS_BOOT_PREVIEW_READY_HOLD_MS) {
        settings_boot_preview_finish();
        return;
    }
    const uint8_t progress = elapsed_ms >= SETTINGS_BOOT_PREVIEW_DURATION_MS
                                 ? 100U
                                 : (uint8_t)((elapsed_ms * 100U) /
                                             SETTINGS_BOOT_PREVIEW_DURATION_MS);
    const esp_err_t ret = esp_bms_lvgl_ui_boot_update(
        progress, settings_boot_preview_status(progress));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "boot preview update failed: %d", (int)ret);
        settings_boot_preview_finish();
    }
}

static esp_err_t settings_boot_preview_start(void)
{
    ESP_RETURN_ON_FALSE(s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_SYSTEM &&
                            s_ui.settings_system_view ==
                                (uint8_t)SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION,
                        ESP_ERR_INVALID_STATE, TAG,
                        "boot animation settings view is not active");
    settings_boot_preview_timer_cancel();
    esp_bms_dashboard_snapshot_t snapshot = *settings_current_snapshot();
    const esp_err_t ret = esp_bms_lvgl_ui_boot_start(&snapshot);
    if (ret != ESP_OK) {
        return ret;
    }
    s_ui.settings_boot_preview_started_ms = lv_tick_get();
    s_ui.settings_boot_preview_timer = lv_timer_create(
        settings_boot_preview_timer_cb, SETTINGS_BOOT_PREVIEW_TIMER_MS, NULL);
    if (!s_ui.settings_boot_preview_timer) {
        settings_boot_preview_finish();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void settings_boot_preview_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const esp_err_t ret = settings_boot_preview_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "boot preview start failed: %d", (int)ret);
    }
}

void settings_boot_animation_option_event_cb(lv_event_t *event)
{
    if (!settings_bms_popup_click_ready(event)) {
        return;
    }

    const size_t selected = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (!boot_animation_style_is_available((uint8_t)selected)) {
        return;
    }
    const size_t current = boot_animation_style_is_available(
                               settings_current_snapshot()->boot_animation_style)
                               ? settings_current_snapshot()->boot_animation_style
                               : (size_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    if (selected != current) {
        queue_action_with_commit(ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE, true);
        s_ui.pending_event.numeric_delta = (int16_t)selected;
        ACTION_EVENT_SET_FLAG(&s_ui.pending_event, NUMERIC_DELTA_VALID, true);
    }
    lv_indev_wait_release(lv_indev_active());
}

static void settings_show_boot_animation_picker(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = portrait ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                     SETTINGS_CHOICE_ROW_H_LANDSCAPE;
    const int32_t gap = settings_scaled_px(portrait ? 8 : 6);
    const uint8_t saved_style = settings_current_snapshot()->boot_animation_style;
    const size_t current = boot_animation_style_is_available(saved_style)
                               ? saved_style
                               : (size_t)ESP_BMS_BOOT_ANIMATION_CHARGE;

    for (size_t index = 0; index < ARRAY_SIZE(SETTINGS_BOOT_ANIMATION_OPTIONS); ++index) {
        const settings_boot_animation_option_t *option = &SETTINGS_BOOT_ANIMATION_OPTIONS[index];
        const bool active = option->style == current;
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
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        settings_add_swipe_handlers(row);
        lv_obj_add_event_cb(row,
                            settings_boot_animation_option_event_cb,
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

void settings_show_system_view(settings_system_view_t view)
{
    if (view == SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION &&
        !s_touch_calibration_supported) {
        view = SETTINGS_SYSTEM_VIEW_ROOT;
    }
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
#if ESP_BMS_FEATURE_AUDIO
    case SETTINGS_SYSTEM_VIEW_VOLUME:
        label_set_text_if_changed(s_ui.settings_detail_title, "音量");
        settings_show_system_slider(QUICK_LEVEL_VOLUME);
        break;
#endif
    case SETTINGS_SYSTEM_VIEW_LEVEL_POSITION:
        label_set_text_if_changed(s_ui.settings_detail_title, "调节条位置");
        settings_show_system_position();
        break;
    case SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION:
        label_set_text_if_changed(s_ui.settings_detail_title, "启动动画");
        settings_show_boot_animation_picker();
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

void settings_show_detail(settings_detail_id_t detail_id)
{
    if (!s_ui.settings_detail || !settings_detail_is_enabled(detail_id)) {
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
    if (detail_id == SETTINGS_DETAIL_GPS) {
        settings_show_gps_detail();
        return;
    }
    if (detail_id == SETTINGS_DETAIL_DASHBOARD) {
        settings_show_dashboard_detail();
        return;
    }
    if (detail_id == SETTINGS_DETAIL_CONTROLLER) {
        settings_show_controller_detail();
        return;
    }

    const int32_t card_x = SETTINGS_LIST_MARGIN_X;
    const int32_t card_w = s_ui.width - (SETTINGS_LIST_MARGIN_X * 2);
    const int32_t row_h = s_ui.width < s_ui.height ? SETTINGS_DETAIL_ROW_H_PORTRAIT :
                                                     SETTINGS_DETAIL_ROW_H_LANDSCAPE;
    const int32_t first_y = 12;

    size_t row_count = 0;
    const settings_detail_row_t *rows = settings_detail_rows_for_id(detail_id, &row_count);
    size_t visible_row_count = row_count;
    if (detail_id == SETTINGS_DETAIL_SYSTEM && !s_touch_calibration_supported) {
        --visible_row_count;
    }
    lv_obj_t *list_card = rows ? settings_list_card(s_ui.settings_detail,
                                                    card_x,
                                                    first_y,
                                                    card_w,
                                                    row_h,
                                                    visible_row_count) : NULL;
    size_t visible_index = 0U;
    for (size_t index = 0; rows && index < row_count; ++index) {
        settings_detail_row_t row = rows[index];
        if (detail_id == SETTINGS_DETAIL_SYSTEM &&
            row.system_view == SETTINGS_SYSTEM_VIEW_TOUCH_CALIBRATION &&
            !s_touch_calibration_supported) {
            continue;
        }
        if (detail_id == SETTINGS_DETAIL_ABOUT && index == 1U) {
            row.subtitle = settings_current_snapshot()->firmware_version[0] != '\0'
                               ? settings_current_snapshot()->firmware_version
                               : "--";
        }
        settings_detail_row(list_card,
                            0,
                            (int32_t)visible_index * row_h,
                            card_w,
                            row_h,
                            &row);
        ++visible_index;
    }
}

void settings_option_event_cb(lv_event_t *event)
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

