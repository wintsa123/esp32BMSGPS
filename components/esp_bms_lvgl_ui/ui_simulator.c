/*
 * UI 模块: simulator
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

#if ESP_BMS_LVGL_UI_SIMULATOR
extern bool settings_bms_ble_simulator_activate_relative_y(int32_t relative_y);

static uint32_t simulator_object_count(const lv_obj_t *obj)
{
    if (!obj) {
        return 0U;
    }
    uint32_t count = 1U;
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t index = 0U; index < child_count; ++index) {
        count += simulator_object_count(lv_obj_get_child(obj, index));
    }
    return count;
}

uint32_t esp_bms_lvgl_ui_simulator_object_count(void)
{
    return simulator_object_count(s_ui.root);
}

uint8_t esp_bms_lvgl_ui_simulator_static_cache_count(void)
{
    return (uint8_t)((s_ui.battery_static_cache.active ? 1U : 0U) +
                     (s_ui.fireblade_static_cache.active ? 1U : 0U) +
                     (s_ui.speed_static_cache.active ? 1U : 0U));
}

static bool simulator_soc_color_smoke(void)
{
    return lv_color_eq(dashboard_soc_fill_color(60U, true, false), COLOR_STATUS_OK) &&
           lv_color_eq(dashboard_soc_fill_color(59U, true, false), COLOR_WARN) &&
           lv_color_eq(dashboard_soc_fill_color(29U, true, false), COLOR_BAD) &&
           lv_color_eq(dashboard_soc_fill_color(0U, false, false), COLOR_PANEL_ALT);
}

static bool simulator_tree_has_label(lv_obj_t *obj, const char *text)
{
    if (!obj || !text) {
        return false;
    }
    if (lv_obj_check_type(obj, &lv_label_class) &&
        strcmp(lv_label_get_text(obj), text) == 0) {
        return true;
    }
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t index = 0U; index < child_count; ++index) {
        if (simulator_tree_has_label(lv_obj_get_child(obj, (int32_t)index), text)) {
            return true;
        }
    }
    return false;
}

static bool simulator_controller_gear_smoke(void)
{
    return strcmp(controller_gear_text(0U, true, true), "N") == 0 &&
           strcmp(controller_gear_text(1U, true, true), "D") == 0 &&
           strcmp(controller_gear_text(2U, true, true), "R") == 0 &&
           strcmp(controller_gear_text(3U, true, true), "-") == 0 &&
           strcmp(controller_gear_text(0U, false, true), "-") == 0 &&
           strcmp(controller_gear_text(0U, true, false), "-") == 0;
}

static bool simulator_native_bms_portrait_smoke(void)
{
    return !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY) ||
           !bms_native_portrait_enabled() ||
           (s_ui.battery_page && s_ui.soc_battery_level && s_ui.soc && s_ui.pack_voltage &&
            s_ui.current &&
            s_ui.capacity && s_ui.remaining_range_value && s_ui.bms_running_time &&
            simulator_tree_has_label(s_ui.battery_page, "BMS") &&
            simulator_tree_has_label(s_ui.battery_page, s_ui.bms_capacity_buf) &&
            simulator_tree_has_label(s_ui.battery_page, s_ui.bms_range_buf) &&
            simulator_tree_has_label(s_ui.battery_page, s_ui.bms_running_time_buf));
}

static bool simulator_native_speed_dashboard_smoke(void)
{
    if (!dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS)) {
        return true;
    }
    const bool native_controller_layout =
        (s_ui.width == 320 && s_ui.height == 480) ||
        (s_ui.width == 480 && s_ui.height == 320);
    if (!native_controller_layout) {
        return true;
    }
    const bool controller_ready =
        !s_ui.controller_page ||
        (s_ui.controller_speed && s_ui.controller_speed_unit && s_ui.controller_gear &&
         s_ui.controller_power && s_ui.controller_rpm && s_ui.controller_temp &&
         s_ui.controller_motor_temp);
    const bool fireblade_ready =
        !s_ui.fireblade_page ||
        (s_ui.native_fireblade_dashboard && s_ui.fireblade_time && s_ui.fireblade_soc &&
         s_ui.fireblade_speed && s_ui.fireblade_speed_unit && s_ui.fireblade_gear &&
         s_ui.fireblade_consumption && s_ui.fireblade_range && s_ui.fireblade_average_speed &&
         s_ui.fireblade_date && s_ui.fireblade_needle_black && s_ui.fireblade_needle_red);
    return controller_ready && fireblade_ready;
}

static bool simulator_native_bms_landscape_smoke(void)
{
    if (!dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY) ||
        s_ui.width != 480 || s_ui.height != 320) {
        return true;
    }
    return s_ui.native_bms_dashboard && s_ui.pack_voltage_unit && s_ui.current_unit &&
           s_ui.bms_cycle_capacity &&
           strcmp(lv_label_get_text(s_ui.pack_voltage_unit), "V") == 0 &&
           strcmp(lv_label_get_text(s_ui.current_unit), "A") == 0 &&
           simulator_tree_has_label(s_ui.battery_page, s_ui.bms_cycle_capacity_buf);
}

bool esp_bms_lvgl_ui_simulator_boot_gauge_matches(
    esp_bms_boot_animation_style_t animation_style,
    uint8_t progress_percent)
{
    const bool intro = progress_percent < BOOT_GAUGE_BRAND_INTRO_PERCENT;
    const esp_bms_speed_dashboard_style_t style = boot_gauge_dashboard_style();
    const bool fireblade = style == ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE;
    const bool rr_intro = animation_style == ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR &&
                          progress_percent >= BOOT_GAUGE_BMW_RR_PERCENT && intro;
    const bool brand_intro = intro && !rr_intro;
    const bool dashboard_matches = intro
                                       ? (fireblade
                                              ? s_ui.fireblade_page &&
                                                    lv_obj_has_flag(s_ui.fireblade_page,
                                                                    LV_OBJ_FLAG_HIDDEN)
                                              : s_ui.speed_art &&
                                                    lv_obj_has_flag(s_ui.speed_art,
                                                                    LV_OBJ_FLAG_HIDDEN))
                                       : fireblade
                                       ? s_ui.fireblade_page &&
                                             !lv_obj_has_flag(s_ui.fireblade_page,
                                                              LV_OBJ_FLAG_HIDDEN) &&
                                             s_ui.speed_art &&
                                             lv_obj_has_flag(s_ui.speed_art,
                                                             LV_OBJ_FLAG_HIDDEN)
                                       : s_ui.speed_art &&
                                             !lv_obj_has_flag(s_ui.speed_art,
                                                              LV_OBJ_FLAG_HIDDEN) &&
                                             (!s_ui.fireblade_page ||
                                              lv_obj_has_flag(s_ui.fireblade_page,
                                                              LV_OBJ_FLAG_HIDDEN));
    const bool brand_matches = s_ui.boot_brand_mark &&
                               (brand_intro
                                    ? !lv_obj_has_flag(s_ui.boot_brand_mark, LV_OBJ_FLAG_HIDDEN)
                                    : lv_obj_has_flag(s_ui.boot_brand_mark, LV_OBJ_FLAG_HIDDEN));
    const bool rr_matches = animation_style == ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR
                                ? s_ui.boot_rr_mark &&
                                      (rr_intro
                                           ? !lv_obj_has_flag(s_ui.boot_rr_mark,
                                                              LV_OBJ_FLAG_HIDDEN)
                                           : lv_obj_has_flag(s_ui.boot_rr_mark,
                                                             LV_OBJ_FLAG_HIDDEN))
                                : !s_ui.boot_rr_mark;
    const bool overlay_matches = s_ui.boot_overlay &&
                                 lv_obj_get_style_bg_opa(s_ui.boot_overlay, LV_PART_MAIN) ==
                                     (intro ? LV_OPA_COVER : LV_OPA_TRANSP);
    return boot_animation_style_is_gauge((uint8_t)animation_style) && s_ui.boot_active &&
           s_ui.boot_animation_style == (uint8_t)animation_style &&
           s_ui.boot_overlay && !s_ui.boot_status && !s_ui.boot_progress &&
           s_ui.last_snapshot.speed_dashboard_style == style &&
           s_ui.last_snapshot.speed_deci_units == boot_gauge_demo_speed(progress_percent) &&
           dashboard_matches && brand_matches && rr_matches && overlay_matches;
}

bool esp_bms_lvgl_ui_simulator_snapshot_matches(const esp_bms_dashboard_snapshot_t *snapshot)
{
    return snapshot && UI_FLAG(LAST_SNAPSHOT_VALID) && !UI_FLAG(DEFERRED_SNAPSHOT_VALID) &&
           memcmp(&s_ui.last_snapshot, snapshot, sizeof(s_ui.last_snapshot)) == 0 &&
           simulator_soc_color_smoke() && simulator_controller_gear_smoke() &&
           simulator_native_bms_portrait_smoke() &&
           simulator_native_bms_landscape_smoke() && simulator_native_speed_dashboard_smoke();
}

static bool simulator_page_transition_smoke(void)
{
    if (!s_ui.pages || !s_ui.page_transition_battery || !s_ui.page_transition_gps ||
        !s_ui.page_transition_battery_card || !s_ui.page_transition_gps_card ||
        !s_ui.battery_page || !s_ui.gps_page) {
        return false;
    }

    move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
    page_transition_show();
    lv_obj_update_layout(s_ui.pages);
    const int32_t last_x = page_last_scroll_x();
    const int32_t cast_x = s_ui.width * (s_ui.speed_page_renderable ? 2 : 1);
    const int32_t music_x = cast_x + (s_ui.cast_page ? s_ui.width : 0);
    const char *music_transition_title =
#if MEDIA_HID_PAGE_ENABLED
        "HID";
#else
        "MUSIC";
#endif
    const bool transition_pages =
        page_transition_active() &&
        lv_obj_get_style_bg_opa(s_ui.page_transition_battery, LV_PART_MAIN) == LV_OPA_TRANSP &&
        lv_color_eq(lv_obj_get_style_bg_color(s_ui.page_transition_battery_card, LV_PART_MAIN),
                    COLOR_DASHBOARD_BG) &&
        lv_obj_get_style_bg_opa(s_ui.page_transition_battery_card, LV_PART_MAIN) == LV_OPA_COVER &&
        lv_obj_get_x(s_ui.page_transition_battery_card) == 0 &&
        lv_obj_get_y(s_ui.page_transition_battery_card) == 0 &&
        lv_obj_get_width(s_ui.page_transition_battery_card) == s_ui.width &&
        lv_obj_get_height(s_ui.page_transition_battery_card) == s_ui.height &&
        lv_obj_has_flag(s_ui.battery_page, LV_OBJ_FLAG_HIDDEN) &&
        lv_obj_has_flag(s_ui.gps_page, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_has_flag(s_ui.page_transition_battery, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_has_flag(s_ui.page_transition_gps, LV_OBJ_FLAG_HIDDEN) &&
        (!s_ui.speed_page_renderable ||
         lv_obj_get_x(s_ui.page_transition_gps) == s_ui.width) &&
        (!s_ui.page_transition_cast ||
         lv_obj_get_x(s_ui.page_transition_cast) == cast_x) &&
        (!s_ui.page_transition_music ||
         lv_obj_get_x(s_ui.page_transition_music) == music_x) &&
        simulator_tree_has_label(s_ui.page_transition_battery, "BMS") &&
        simulator_tree_has_label(s_ui.page_transition_gps, "仪表") &&
        (!s_ui.page_transition_cast ||
         simulator_tree_has_label(s_ui.page_transition_cast, "投屏")) &&
        (!s_ui.page_transition_music ||
         simulator_tree_has_label(s_ui.page_transition_music, music_transition_title));
    const bool scroll_bounds_preserved = lv_obj_get_scroll_right(s_ui.pages) == last_x;
    lv_timer_handler();
    lv_delay_ms(PAGE_TRANSITION_CARD_ANIM_MS + 20U);
    lv_timer_handler();
    lv_obj_update_layout(s_ui.pages);
    const bool compact =
        lv_obj_get_x(s_ui.page_transition_battery_card) == PAGE_TRANSITION_CARD_MARGIN &&
        lv_obj_get_y(s_ui.page_transition_battery_card) == PAGE_TRANSITION_CARD_MARGIN &&
        lv_obj_get_width(s_ui.page_transition_battery_card) ==
            s_ui.width - (PAGE_TRANSITION_CARD_MARGIN * 2) &&
        lv_obj_get_height(s_ui.page_transition_battery_card) ==
            s_ui.height - (PAGE_TRANSITION_CARD_MARGIN * 2);
    page_transition_expand(ESP_BMS_LVGL_PAGE_BATTERY);
    const bool expanding = UI_FLAG(SETTLING) && page_transition_active();
    lv_timer_handler();
    lv_delay_ms(PAGE_TRANSITION_CARD_ANIM_MS + 20U);
    lv_timer_handler();
    const bool restored = !page_transition_active() &&
                          !UI_FLAG(SETTLING) &&
                          dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY) &&
                          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS) &&
                          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_CAST) &&
                          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_MUSIC) &&
                          !lv_obj_has_flag(s_ui.battery_page, LV_OBJ_FLAG_HIDDEN) &&
                          !lv_obj_has_flag(s_ui.gps_page, LV_OBJ_FLAG_HIDDEN) &&
                          (!s_ui.cast_page ||
                           !lv_obj_has_flag(s_ui.cast_page, LV_OBJ_FLAG_HIDDEN)) &&
                          (!s_ui.music_page ||
                           !lv_obj_has_flag(s_ui.music_page, LV_OBJ_FLAG_HIDDEN)) &&
                          lv_obj_has_flag(s_ui.page_transition_battery,
                                          LV_OBJ_FLAG_HIDDEN) &&
                          lv_obj_has_flag(s_ui.page_transition_gps,
                                          LV_OBJ_FLAG_HIDDEN) &&
                          (!s_ui.page_transition_cast ||
                           lv_obj_has_flag(s_ui.page_transition_cast,
                                           LV_OBJ_FLAG_HIDDEN)) &&
                          (!s_ui.page_transition_music ||
                           lv_obj_has_flag(s_ui.page_transition_music,
                                           LV_OBJ_FLAG_HIDDEN));
    move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
    return transition_pages && scroll_bounds_preserved && compact && expanding && restored;
}

bool esp_bms_lvgl_ui_simulator_native_gesture_smoke(void)
{
    if (!UI_FLAG(INITIALIZED) || !s_native_gestures_supported ||
        s_touch_calibration_supported) {
        return false;
    }

    show_dashboard_view();
    if (!simulator_page_transition_smoke()) {
        return false;
    }
    move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
    const esp_bms_lvgl_page_t initial_page = s_ui.page;
    const uint32_t initial_flags = s_ui.flags;
    if (esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_DOUBLE_TAP) != ESP_OK ||
        s_ui.page != initial_page || s_ui.flags != initial_flags) {
        return false;
    }

    const int32_t last_x = page_last_scroll_x();
    const esp_bms_lvgl_page_t next_page = page_from_scroll_x(clamp_i32(s_ui.width, 0, last_x));
    if (esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_RIGHT) != ESP_OK ||
        s_ui.page != next_page ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_LEFT) != ESP_OK ||
        s_ui.page != ESP_BMS_LVGL_PAGE_BATTERY) {
        return false;
    }

    if (esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_DOWN) != ESP_OK ||
        !UI_FLAG(QUICK_PANEL_OPEN) ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_KEY_NEXT) != ESP_OK ||
        native_focus_list().count == 0U ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_SWIPE_UP) != ESP_OK ||
        UI_FLAG(QUICK_PANEL_OPEN)) {
        return false;
    }

    show_settings_view();
    if (dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY) ||
        dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS) ||
        dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_CAST) ||
        dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_MUSIC)) {
        return false;
    }
    settings_show_detail(SETTINGS_DETAIL_SYSTEM);
    if (simulator_tree_has_label(s_ui.settings_detail, "屏幕校准") ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_KEY_NEXT) != ESP_OK ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_KEY_CONFIRM) != ESP_OK ||
        s_ui.settings_system_view != (uint8_t)SETTINGS_SYSTEM_VIEW_BRIGHTNESS ||
        esp_bms_lvgl_ui_handle_native_gesture(
            ESP_BMS_LVGL_NATIVE_GESTURE_KEY_BACK) != ESP_OK ||
        s_ui.settings_system_view != (uint8_t)SETTINGS_SYSTEM_VIEW_ROOT) {
        return false;
    }

    show_dashboard_view();
    move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
    if (!(dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY) &&
          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS) &&
          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_CAST) &&
          !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_MUSIC))) {
        return false;
    }
    /* After a page release/recreate the labels must be re-populated even when
     * the snapshot values did not change; otherwise the values disappear until
     * the next update (bms_label_set must not skip a label whose text pointer
     * does not yet point at the static buffer). */
    const bool soc_applied =
        s_ui.soc && lv_label_get_text(s_ui.soc) == s_ui.bms_soc_buf;
    const bool voltage_applied =
        s_ui.pack_voltage && lv_label_get_text(s_ui.pack_voltage) == s_ui.bms_pack_voltage_buf;
    return soc_applied && voltage_applied;
}

bool esp_bms_lvgl_ui_simulator_boot_animation_preview_active(void)
{
    return s_ui.settings_boot_preview_timer != NULL;
}

bool esp_bms_lvgl_ui_simulator_boot_animation_settings_visible(void)
{
    return UI_FLAG(INITIALIZED) && s_ui.settings_page &&
           !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN) &&
           s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_SYSTEM &&
           s_ui.settings_system_view ==
               (uint8_t)SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION &&
           s_ui.settings_boot_preview_button &&
           !lv_obj_has_flag(s_ui.settings_boot_preview_button, LV_OBJ_FLAG_HIDDEN) &&
           simulator_tree_has_label(s_ui.settings_detail, "BMW S1000RR") &&
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
           simulator_tree_has_label(s_ui.settings_detail, "HONDA Fireblade") &&
#endif
           true;
}

bool esp_bms_lvgl_ui_simulator_gps_settings_visible(void)
{
    return UI_FLAG(INITIALIZED) && s_ui.settings_page &&
           !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN) &&
           s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_GPS;
}

esp_err_t esp_bms_lvgl_ui_simulator_open_gps_settings(void)
{
#if !ESP_BMS_FEATURE_GPS
    return ESP_ERR_INVALID_STATE;
#else
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    show_settings_view();
    settings_show_detail(SETTINGS_DETAIL_GPS);
    ESP_RETURN_ON_FALSE(esp_bms_lvgl_ui_simulator_gps_settings_visible(),
                        ESP_ERR_INVALID_STATE, TAG, "GPS settings did not open");
    return ESP_OK;
#endif
}

bool esp_bms_lvgl_ui_simulator_gps_settings_smoke(void)
{
#if !ESP_BMS_FEATURE_GPS
    return false;
#else
    if (esp_bms_lvgl_ui_simulator_open_gps_settings() != ESP_OK) {
        return false;
    }
    settings_show_detail(SETTINGS_DETAIL_DASHBOARD);
    settings_show_speed_unit_picker();
    settings_navigate_back();
    if (s_ui.settings_detail_id != (uint8_t)SETTINGS_DETAIL_DASHBOARD ||
        s_ui.settings_dashboard_view != (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT) {
        return false;
    }
#if ESP_BMS_FEATURE_CONTROLLER
    settings_show_speed_source_picker();
    settings_navigate_back();
    if (s_ui.settings_detail_id != (uint8_t)SETTINGS_DETAIL_DASHBOARD ||
        s_ui.settings_dashboard_view != (uint8_t)SETTINGS_DASHBOARD_VIEW_ROOT) {
        return false;
    }
#endif
    settings_show_detail(SETTINGS_DETAIL_GPS);
    return true;
#endif
}

bool esp_bms_lvgl_ui_simulator_settings_scroll_smoke(void)
{
    if (!UI_FLAG(INITIALIZED) || !s_ui.settings_carousel) {
        return false;
    }
    show_settings_view();
    lv_obj_update_layout(s_ui.settings_carousel);
    const int32_t bottom = lv_obj_get_scroll_bottom(s_ui.settings_carousel);
    lv_obj_scroll_by(s_ui.settings_carousel, 0, bottom, LV_ANIM_OFF);
    bool scrolled = bottom == 0 || lv_obj_get_scroll_y(s_ui.settings_carousel) < 0;
    lv_obj_scroll_to_y(s_ui.settings_carousel, 0, LV_ANIM_OFF);
#if ESP_BMS_FEATURE_GPS
    settings_show_detail(SETTINGS_DETAIL_GPS);
    lv_obj_update_layout(s_ui.settings_detail);
    const int32_t gps_bottom = lv_obj_get_scroll_bottom(s_ui.settings_detail);
    lv_obj_scroll_by(s_ui.settings_detail, 0, gps_bottom, LV_ANIM_OFF);
    scrolled = scrolled && gps_bottom > 0 && lv_obj_get_scroll_y(s_ui.settings_detail) < 0;
    lv_obj_scroll_to_y(s_ui.settings_detail, 0, LV_ANIM_OFF);
#endif
#if ESP_BMS_FEATURE_BMS
    settings_show_detail(SETTINGS_DETAIL_BMS);
    s_ui.last_snapshot.bms_scan_candidate_count = 0U;
    memset(s_ui.last_snapshot.bms_scan_candidates,
           0,
           sizeof(s_ui.last_snapshot.bms_scan_candidates));
    settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_BMS, false);
    const uint32_t empty_bms_candidate_list_objects = esp_bms_lvgl_ui_simulator_object_count();
    lv_obj_t *const initial_bms_ble_status = s_ui.settings_bms_ble_status;
    lv_obj_t *const initial_bms_ble_list = s_ui.settings_bms_ble_list;
    s_ui.last_snapshot.bms_scan_candidate_count = 6U;
    for (uint8_t index = 0; index < 6U; ++index) {
        esp_bms_bms_scan_candidate_t *candidate = &s_ui.last_snapshot.bms_scan_candidates[index];
        (void)snprintf(candidate->mac,
                       sizeof(candidate->mac),
                       "00:11:22:33:44:%02X",
                       (unsigned)index);
        (void)snprintf(candidate->name, sizeof(candidate->name), "BMS %u", (unsigned)index + 1U);
        candidate->rssi = (int8_t)(-45 - (int8_t)index);
        candidate->has_name = true;
    }
    (void)snprintf(s_ui.last_snapshot.bms_scan_candidates[0].name,
                   sizeof(s_ui.last_snapshot.bms_scan_candidates[0].name),
                   "midea");
    (void)snprintf(s_ui.last_snapshot.bms_scan_candidates[1].name,
                   sizeof(s_ui.last_snapshot.bms_scan_candidates[1].name),
                   "Other BMS");
    (void)snprintf(s_ui.last_snapshot.bms_scan_candidates[2].name,
                   sizeof(s_ui.last_snapshot.bms_scan_candidates[2].name),
                   "midea");
    s_ui.last_snapshot.bms_scan_candidates[3].name[0] = '\0';
    s_ui.last_snapshot.bms_scan_candidates[3].has_name = false;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_BMS,
                                  false,
                                  "simulator-six");
    const char *bms_list_text = lv_label_get_text(s_ui.settings_bms_ble_list);
    const bool bms_six_direct = strstr(bms_list_text, "midea 44:00 -45 dBm") != NULL &&
                                strstr(bms_list_text, "Other BMS 44:01 -46 dBm") != NULL &&
                                strstr(bms_list_text, "midea 44:02 -47 dBm") != NULL &&
                                strstr(bms_list_text, "设备 44:03 -48 dBm") != NULL &&
                                strstr(bms_list_text, "BMS 6 44:05 -50 dBm") != NULL &&
                                strstr(bms_list_text, "More devices") == NULL;

    s_ui.last_snapshot.bms_scan_candidate_count = 7U;
    esp_bms_bms_scan_candidate_t *candidate = &s_ui.last_snapshot.bms_scan_candidates[6];
    (void)snprintf(candidate->mac, sizeof(candidate->mac), "00:11:22:33:44:06");
    (void)snprintf(candidate->name, sizeof(candidate->name), "BMS 7");
    candidate->rssi = -51;
    candidate->has_name = true;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_BMS,
                                  false,
                                  "simulator-seven-first");
    bms_list_text = lv_label_get_text(s_ui.settings_bms_ble_list);
    bool more_action = false;
    const bool bms_seven_first = strstr(bms_list_text, "More devices") != NULL &&
                                 strstr(bms_list_text, "BMS 6") == NULL &&
                                 settings_bms_ble_candidate_index(7U, false, 5U, &more_action) ==
                                     UINT8_MAX &&
                                 more_action;
    const int32_t bms_row_stride =
        (s_ui.width < s_ui.height ? SETTINGS_CHOICE_ROW_H_PORTRAIT :
                                    SETTINGS_CHOICE_ROW_H_LANDSCAPE) +
        (s_ui.width < s_ui.height ? 7 : 5);
    s_ui.settings_bms_confirm_mac[0] = '\0';
    const bool bms_more_clicked =
        settings_bms_ble_simulator_activate_relative_y(5 * bms_row_stride) &&
        s_ui.settings_ble_more_page && !s_ui.settings_bms_popup &&
        s_ui.settings_bms_confirm_mac[0] == '\0';
    bms_list_text = lv_label_get_text(s_ui.settings_bms_ble_list);
    const bool bms_seven_more = strstr(bms_list_text, "BMS 6") != NULL &&
                                strstr(bms_list_text, "BMS 7") != NULL &&
                                strstr(bms_list_text, "More devices") == NULL &&
                                settings_bms_ble_candidate_index(7U, true, 0U, NULL) == 5U &&
                                bms_more_clicked;

    s_ui.last_snapshot.bms_scan_candidate_count = ESP_BMS_BMS_SCAN_MAX_CANDIDATES;
    for (uint8_t index = 7U; index < ESP_BMS_BMS_SCAN_MAX_CANDIDATES; ++index) {
        candidate = &s_ui.last_snapshot.bms_scan_candidates[index];
        (void)snprintf(candidate->mac,
                       sizeof(candidate->mac),
                       "00:11:22:33:44:%02X",
                       (unsigned)index);
        (void)snprintf(candidate->name, sizeof(candidate->name), "BMS %u", (unsigned)index + 1U);
        candidate->rssi = (int8_t)(-45 - (int8_t)index);
        candidate->has_name = true;
    }
    candidate = &s_ui.last_snapshot.bms_scan_candidates[11];
    candidate->name[0] = '\0';
    candidate->has_name = false;
    s_ui.settings_ble_more_page = false;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_BMS,
                                  false,
                                  "simulator-twelve-first");
    const bool bms_twelve_first =
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "More devices") != NULL &&
        settings_bms_ble_candidate_index(12U, false, 6U, NULL) == UINT8_MAX;
    s_ui.settings_ble_more_page = true;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_BMS,
                                  false,
                                  "simulator-twelve-more");
    bms_list_text = lv_label_get_text(s_ui.settings_bms_ble_list);
    const bool bms_candidate_list_compact =
        esp_bms_lvgl_ui_simulator_object_count() == empty_bms_candidate_list_objects &&
        s_ui.settings_bms_ble_status == initial_bms_ble_status &&
        s_ui.settings_bms_ble_list == initial_bms_ble_list &&
        !lv_obj_has_flag(s_ui.settings_bms_ble_list, LV_OBJ_FLAG_HIDDEN) &&
        strstr(bms_list_text, "BMS 6 44:05") != NULL &&
        strstr(bms_list_text, "设备 44:0B") != NULL &&
        settings_bms_ble_candidate_index(12U, true, 6U, NULL) == 11U;
    settings_navigate_back();
    const bool bms_more_back_returns_first =
        !s_ui.settings_ble_more_page &&
        s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_BLE_LIST &&
        s_ui.pending_event.action == ESP_BMS_LVGL_ACTION_NONE &&
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "More devices") != NULL;
    settings_navigate_back();
    const bool bms_back_cancels_scan =
        s_ui.settings_bms_view == (uint8_t)SETTINGS_BMS_VIEW_ROOT &&
        s_ui.pending_event.action == ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION;
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_BMS, false);
    candidate = &s_ui.last_snapshot.bms_scan_candidates[0];
    (void)snprintf(candidate->mac, sizeof(candidate->mac), "00:11:22:33:44:55");
    settings_show_bms_bind_confirm(candidate);
    native_gesture_back();
    const bool bms_confirm_back_cancels_scan =
        !s_ui.settings_bms_popup &&
        s_ui.pending_event.action == ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION;
    memset(&s_ui.pending_event, 0, sizeof(s_ui.pending_event));
    settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_BMS, false);
    s_ui.settings_ble_more_page = true;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_BMS,
                                  false,
                                  "simulator-bms-more-click");
    s_ui.settings_bms_confirm_mac[0] = '\0';
    s_ui.settings_bms_confirm_name[0] = '\0';
    const bool bms_more_candidate_clicked =
        settings_bms_ble_simulator_activate_relative_y(6 * bms_row_stride) &&
        s_ui.settings_bms_popup &&
        strcmp(s_ui.settings_bms_confirm_mac, "00:11:22:33:44:0B") == 0 &&
        strcmp(s_ui.settings_bms_confirm_name, "设备") == 0;
    settings_bms_popup_close();
#else
    const bool bms_back_cancels_scan = true;
    const bool bms_confirm_back_cancels_scan = true;
    const bool bms_candidate_list_compact = true;
    const bool bms_more_back_returns_first = true;
    const bool bms_six_direct = true;
    const bool bms_seven_first = true;
    const bool bms_seven_more = true;
    const bool bms_twelve_first = true;
    const bool bms_more_candidate_clicked = true;
#endif
#if ESP_BMS_FEATURE_CONTROLLER
    settings_show_detail(SETTINGS_DETAIL_CONTROLLER);
    s_ui.last_snapshot.controller_scan_candidate_count = 0U;
    memset(s_ui.last_snapshot.controller_scan_candidates,
           0,
           sizeof(s_ui.last_snapshot.controller_scan_candidates));
    settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_CONTROLLER, false);
    const bool controller_zero_empty =
        lv_obj_has_flag(s_ui.settings_bms_ble_list, LV_OBJ_FLAG_HIDDEN);
    s_ui.last_snapshot.controller_scan_candidate_count = 6U;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SCAN_MAX_CANDIDATES; ++index) {
        esp_bms_bms_scan_candidate_t *candidate =
            &s_ui.last_snapshot.controller_scan_candidates[index];
        (void)snprintf(candidate->mac,
                       sizeof(candidate->mac),
                       "10:11:22:33:44:%02X",
                       (unsigned)index);
        (void)snprintf(candidate->name, sizeof(candidate->name), "CTL %u", (unsigned)index + 1U);
        candidate->rssi = (int8_t)(-55 - (int8_t)index);
        candidate->has_name = true;
    }
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_CONTROLLER,
                                  false,
                                  "simulator-controller-six");
    const bool controller_six_direct =
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "CTL 6") != NULL &&
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "More devices") == NULL;
    s_ui.last_snapshot.controller_scan_candidate_count = 7U;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_CONTROLLER,
                                  false,
                                  "simulator-controller-seven");
    const bool controller_seven_first =
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "More devices") != NULL &&
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "CTL 6") == NULL;
    s_ui.last_snapshot.controller_scan_candidate_count = ESP_BMS_BMS_SCAN_MAX_CANDIDATES;
    s_ui.last_snapshot.controller_scan_candidates[11].name[0] = '\0';
    s_ui.last_snapshot.controller_scan_candidates[11].has_name = false;
    s_ui.settings_ble_more_page = true;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_CONTROLLER,
                                  false,
                                  "simulator-controller-twelve-more");
    const char *controller_list_text = lv_label_get_text(s_ui.settings_bms_ble_list);
    const bool controller_twelve_more = strstr(controller_list_text, "CTL 6") != NULL &&
                                        strstr(controller_list_text, "设备 44:0B") != NULL &&
                                        strcmp(s_ui.last_snapshot.controller_scan_candidates[
                                                   settings_bms_ble_candidate_index(
                                                       12U, true, 0U, NULL)]
                                                   .mac,
                                               "10:11:22:33:44:05") == 0;
    settings_navigate_back();
    const bool controller_more_back_returns_first =
        !s_ui.settings_ble_more_page &&
        s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_BLE_LIST &&
        strstr(lv_label_get_text(s_ui.settings_bms_ble_list), "More devices") != NULL;
    settings_navigate_back();
    const bool controller_back_returns_root =
        s_ui.settings_controller_view == (uint8_t)SETTINGS_CONTROLLER_VIEW_ROOT;
    settings_show_bms_ble_popup(SETTINGS_BLE_SOURCE_CONTROLLER, false);
    s_ui.settings_ble_more_page = true;
    settings_bms_ble_refresh_rows(&s_ui.last_snapshot,
                                  SETTINGS_BLE_SOURCE_CONTROLLER,
                                  false,
                                  "simulator-controller-more-click");
    s_ui.settings_bms_confirm_mac[0] = '\0';
    s_ui.settings_bms_confirm_name[0] = '\0';
    const bool controller_more_candidate_clicked =
        settings_bms_ble_simulator_activate_relative_y(0) && s_ui.settings_bms_popup &&
        strcmp(s_ui.settings_bms_confirm_mac, "10:11:22:33:44:05") == 0 &&
        strcmp(s_ui.settings_bms_confirm_name, "CTL 6") == 0;
    settings_bms_popup_close();
#else
    const bool controller_zero_empty = true;
    const bool controller_six_direct = true;
    const bool controller_seven_first = true;
    const bool controller_twelve_more = true;
    const bool controller_more_back_returns_first = true;
    const bool controller_back_returns_root = true;
    const bool controller_more_candidate_clicked = true;
#endif
    esp_bms_dashboard_snapshot_t bluetooth_snapshot = s_ui.last_snapshot;
    bluetooth_snapshot.bms_error_text[0] = '\0';
    apply_dashboard_snapshot(&bluetooth_snapshot);
    settings_show_detail(SETTINGS_DETAIL_BLUETOOTH);
    const bool bluetooth_default_hides_pin =
        s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BLUETOOTH &&
        !simulator_tree_has_label(s_ui.settings_detail, "PIN 123456");
    (void)snprintf(bluetooth_snapshot.bms_error_text,
                   sizeof(bluetooth_snapshot.bms_error_text),
                   "PIN 123456");
    apply_dashboard_snapshot(&bluetooth_snapshot);
    const bool bluetooth_pin_visible =
        s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BLUETOOTH &&
        simulator_tree_has_label(s_ui.settings_detail, "PIN 123456");
    bluetooth_snapshot.bms_error_text[0] = '\0';
    apply_dashboard_snapshot(&bluetooth_snapshot);
    const bool bluetooth_pin_hidden =
        s_ui.settings_detail_id == (uint8_t)SETTINGS_DETAIL_BLUETOOTH &&
        !simulator_tree_has_label(s_ui.settings_detail, "PIN 123456");
    show_dashboard_view();
    return scrolled && bms_six_direct && bms_seven_first && bms_seven_more &&
           bms_twelve_first && bms_candidate_list_compact && bms_more_back_returns_first &&
           bms_more_candidate_clicked && bms_back_cancels_scan &&
           bms_confirm_back_cancels_scan && controller_zero_empty &&
           controller_six_direct && controller_seven_first && controller_twelve_more &&
           controller_more_candidate_clicked && controller_more_back_returns_first &&
           controller_back_returns_root &&
           bluetooth_default_hides_pin &&
           bluetooth_pin_visible && bluetooth_pin_hidden;
}

esp_err_t esp_bms_lvgl_ui_simulator_open_boot_animation_settings(void)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    ESP_RETURN_ON_FALSE(!s_ui.settings_boot_preview_timer,
                        ESP_ERR_INVALID_STATE, TAG,
                        "boot animation preview is active");
    show_settings_view();
    settings_show_system_view(SETTINGS_SYSTEM_VIEW_BOOT_ANIMATION);
    ESP_RETURN_ON_FALSE(
        esp_bms_lvgl_ui_simulator_boot_animation_settings_visible(),
        ESP_ERR_INVALID_STATE, TAG,
        "boot animation settings view did not open");
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_simulator_play_boot_animation(void)
{
    ESP_RETURN_ON_FALSE(
        esp_bms_lvgl_ui_simulator_boot_animation_settings_visible(),
        ESP_ERR_INVALID_STATE, TAG,
        "boot animation settings view is not active");
    (void)lv_obj_send_event(s_ui.settings_boot_preview_button,
                            LV_EVENT_CLICKED,
                            NULL);
    ESP_RETURN_ON_FALSE(
        esp_bms_lvgl_ui_simulator_boot_animation_preview_active(),
        ESP_ERR_INVALID_STATE, TAG,
        "boot animation preview did not start");
    return ESP_OK;
}
#endif
