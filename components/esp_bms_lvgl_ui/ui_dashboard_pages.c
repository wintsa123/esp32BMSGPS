/*
 * UI 模块: pages_common
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

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

void set_header(const esp_bms_dashboard_snapshot_t *snapshot)
{
#if ESP_BMS_FEATURE_GPS
    const bool gps_fix_valid = SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID);

    if (snapshot->gps_module_state == (uint8_t)ESP_BMS_GPS_MODULE_PROBING) {
        label_set_text_color_if_changed(s_ui.gps_state, COLOR_WARN);
        label_set_text_if_changed(s_ui.gps_state, "GPS...");
    } else if (snapshot->gps_module_state == (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE) {
        label_set_text_color_if_changed(s_ui.gps_state,
                                        gps_fix_valid ? COLOR_ACCENT : COLOR_WARN);
        label_set_text_if_changed(s_ui.gps_state, gps_fix_valid ? "GPS OK" : "GPS --");
    } else {
        label_set_text_color_if_changed(s_ui.gps_state, COLOR_BAD);
        label_set_text_if_changed(s_ui.gps_state, "GPS OFF");
    }
#else
    lv_obj_add_flag(s_ui.gps_state, LV_OBJ_FLAG_HIDDEN);
#endif

    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);

    label_set_text_color_if_changed(s_ui.bms_state, bms_online ? COLOR_ACCENT : COLOR_BAD);
    label_set_text_if_changed(s_ui.bms_state, bms_online ? "BMS OK" : "BMS OFF");

    label_set_text_color_if_changed(s_ui.ap_state,
                                    SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? COLOR_ACCENT : COLOR_MUTED);
    label_set_text_if_changed(s_ui.ap_state,
                              SNAPSHOT_FLAG(snapshot, SETUP_AP_ENABLED) ? "AP" : "AP OFF");
}

void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot)
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

void set_cast_page(const esp_bms_dashboard_snapshot_t *snapshot)
{
#if !ESP_BMS_FEATURE_CAST
    (void)snapshot;
    return;
#else
    if (!snapshot || !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_CAST)) {
        return;
    }
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
#endif
}

#if MUSIC_PAGE_ENABLED
static void music_control_set_enabled(lv_obj_t *control, bool enabled)
{
    if (!control) {
        return;
    }
    if (enabled) {
        lv_obj_remove_state(control, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
    }
}
#endif

void set_music_page(const esp_bms_dashboard_snapshot_t *snapshot)
{
#if MEDIA_HID_PAGE_ENABLED
    if (!snapshot || !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_MUSIC) ||
        !s_ui.music_status) {
        return;
    }

    const bool ready = snapshot->ble_media_hid_connected && !snapshot->ble_media_hid_suspended;
    const char *status = snapshot->ble_media_hid_suspended
                             ? "挂起"
                             : snapshot->ble_media_hid_connected ? "已连接" : "未连接";
    label_set_text_if_changed(s_ui.music_status, status);
    label_set_text_color_if_changed(s_ui.music_status,
                                    ready ? COLOR_SWITCH_ACTIVE : COLOR_SETTINGS_MUTED);
    for (size_t index = 0U; index < MUSIC_CONTROL_COUNT; ++index) {
        music_control_set_enabled(s_ui.music_controls[index], ready);
    }
#else
    (void)snapshot;
#endif
}

void set_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot || !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_BATTERY)) {
        return;
    }

    char soc_text[8];
    char voltage[24];
    char current[24];
    char ah[40];
    char running_time[32];
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
            if (bms_native_portrait_enabled() && s_ui.native_bms_dashboard) {
                (void)snprintf(ah,
                               sizeof(ah),
                               "%lu.%01luAh",
                               (unsigned long)(snapshot->capacity_remaining_mah / 1000U),
                               (unsigned long)((snapshot->capacity_remaining_mah % 1000U) / 100U));
            } else {
                (void)snprintf(ah,
                               sizeof(ah),
                               "%lu.%01lu/%lu.%01luAh",
                               (unsigned long)(snapshot->capacity_remaining_mah / 1000U),
                               (unsigned long)((snapshot->capacity_remaining_mah % 1000U) / 100U),
                               (unsigned long)(snapshot->total_capacity_mah / 1000U),
                               (unsigned long)((snapshot->total_capacity_mah % 1000U) / 100U));
            }
        } else {
            (void)snprintf(ah, sizeof(ah), "--/--Ah");
        }
        (void)snprintf(soc_text, sizeof(soc_text), "%u %%", (unsigned)soc_percent);
    } else {
        (void)snprintf(ah, sizeof(ah), "--/--Ah");
        (void)snprintf(soc_text, sizeof(soc_text), "--");
    }
    bms_label_set(s_ui.soc, s_ui.bms_soc_buf, sizeof(s_ui.bms_soc_buf), soc_text);
    bms_label_set(s_ui.capacity, s_ui.bms_capacity_buf, sizeof(s_ui.bms_capacity_buf), ah);
    if (snapshot->bms_running_time_valid) {
        const uint32_t seconds = snapshot->bms_running_time_seconds;
        (void)snprintf(running_time,
                       sizeof(running_time),
                       "%lu天%02lu时%02lu分",
                       (unsigned long)(seconds / 86400U),
                       (unsigned long)((seconds / 3600U) % 24U),
                       (unsigned long)((seconds / 60U) % 60U));
    } else {
        (void)snprintf(running_time, sizeof(running_time), "--");
    }
    bms_label_set(s_ui.bms_running_time,
                  s_ui.bms_running_time_buf,
                  sizeof(s_ui.bms_running_time_buf),
                  running_time);

    const bool current_valid = SNAPSHOT_FLAG(snapshot, CURRENT_VALID);
    format_mv(voltage, sizeof(voltage), SNAPSHOT_FLAG(snapshot, PACK_VOLTAGE_VALID), snapshot->pack_voltage_mv);
    const int32_t display_current_deci_amps = -(int32_t)snapshot->current_deci_amps;
    format_deci_amps(current, sizeof(current), current_valid, display_current_deci_amps);
    if (s_ui.native_bms_dashboard && current_valid &&
        (current[0] == '+' || current[0] == '-')) {
        const size_t current_len = strlen(current);
        if (current_len + 1U < sizeof(current)) {
            memmove(current + 2, current + 1, current_len);
            current[1] = ' ';
        }
    }
    if (bms_native_landscape_enabled()) {
        if (SNAPSHOT_FLAG(snapshot, PACK_VOLTAGE_VALID)) {
            voltage[strlen(voltage) - 1U] = '\0';
        }
        if (current_valid) {
            current[strlen(current) - 1U] = '\0';
        }
        set_obj_hidden(s_ui.pack_voltage_unit, !SNAPSHOT_FLAG(snapshot, PACK_VOLTAGE_VALID));
        set_obj_hidden(s_ui.current_unit, !current_valid);
    }
    const bool charging = current_valid && display_current_deci_amps < 0;
    if (s_ui.soc_arc) {
        lv_arc_set_value(s_ui.soc_arc, soc_valid ? soc_percent : 0U);
        lv_obj_set_style_arc_color(s_ui.soc_arc,
                                   dashboard_soc_fill_color(soc_percent, soc_valid, charging),
                                   LV_PART_INDICATOR);
    }
    update_dashboard_battery_icon(soc_percent, soc_valid, charging);
    bms_label_set(s_ui.pack_voltage,
                  s_ui.bms_pack_voltage_buf,
                  sizeof(s_ui.bms_pack_voltage_buf),
                  voltage);
    bms_label_set(s_ui.current, s_ui.bms_current_buf, sizeof(s_ui.bms_current_buf), current);

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
    bms_label_set(s_ui.cell_stat_values[0],
                  s_ui.bms_cell_stat_buf[0], sizeof(s_ui.bms_cell_stat_buf[0]), max_cell);
    bms_label_set(s_ui.cell_stat_values[1],
                  s_ui.bms_cell_stat_buf[1], sizeof(s_ui.bms_cell_stat_buf[1]), min_cell);
    bms_label_set(s_ui.cell_stat_values[2],
                  s_ui.bms_cell_stat_buf[2], sizeof(s_ui.bms_cell_stat_buf[2]), delta_cell);
    bms_label_set(s_ui.cell_stat_values[3],
                  s_ui.bms_cell_stat_buf[3], sizeof(s_ui.bms_cell_stat_buf[3]), avg_cell);

    if (!s_ui.native_bms_dashboard) {
        const bool portrait = s_ui.width < s_ui.height;
        const int32_t status_area_height = portrait ? 52 : 70;
        const int32_t status_width = portrait ? 100 : 68;
        const int32_t title_height = (int32_t)settings_zh_10.line_height + 1;
        const char *status_title = "BLE STATUS";
        const char *status_value = "未连接";
        lv_color_t status_color = COLOR_MUTED;
        const lv_font_t *status_value_font = &settings_zh_16;
        if (SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
            status_value = "已连接";
            status_color = COLOR_ACCENT;
        } else if (strstr(snapshot->bms_info_text, "FAIL") != NULL ||
                   strstr(snapshot->bms_info_text, "ERR") != NULL ||
                   strstr(snapshot->bms_info_text, "NO ") != NULL) {
            status_value = "未连接";
            status_color = COLOR_BAD;
        } else if (snapshot->bms_info_text[0] != '\0' &&
                   strcmp(snapshot->bms_info_text, "BMS OFF") != 0) {
            status_value = "连接中";
            status_color = COLOR_WARN;
        }
        const int32_t status_value_height = (int32_t)status_value_font->line_height + 1;
        bms_label_set(s_ui.bms_error, s_ui.bms_error_buf, sizeof(s_ui.bms_error_buf), status_title);
        label_set_text_color_if_changed(s_ui.bms_error, status_color);
        lv_obj_set_pos(s_ui.bms_error, 4, 4);
        lv_obj_set_size(s_ui.bms_error, status_width, title_height);
        bms_label_set(s_ui.bms_status_ok,
                      s_ui.bms_status_buf,
                      sizeof(s_ui.bms_status_buf),
                      status_value);
        label_set_text_color_if_changed(s_ui.bms_status_ok, status_color);
        lv_obj_set_style_text_font(s_ui.bms_status_ok, status_value_font, LV_PART_MAIN);
        lv_obj_set_pos(s_ui.bms_status_ok,
                       4,
                       (status_area_height - status_value_height) / 2);
        lv_obj_set_size(s_ui.bms_status_ok, status_width, status_value_height);
        if (snapshot->remaining_range_valid) {
            (void)snprintf(s_ui.bms_range_buf,
                           sizeof(s_ui.bms_range_buf),
                           "%u",
                           (unsigned)snapshot->remaining_range_km);
        } else {
            (void)snprintf(s_ui.bms_range_buf, sizeof(s_ui.bms_range_buf), "--");
        }
        lv_label_set_text_static(s_ui.remaining_range_value, s_ui.bms_range_buf);
    }
    if (s_ui.native_bms_dashboard && s_ui.remaining_range_value) {
        if (snapshot->remaining_range_valid) {
            (void)snprintf(s_ui.bms_range_buf,
                           sizeof(s_ui.bms_range_buf),
                           "%u",
                           (unsigned)snapshot->remaining_range_km);
        } else {
            (void)snprintf(s_ui.bms_range_buf, sizeof(s_ui.bms_range_buf), "--");
        }
        lv_label_set_text_static(s_ui.remaining_range_value, s_ui.bms_range_buf);
    }

    format_temp_c(t1, sizeof(t1), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 0U), snapshot->bms_temperature_celsius[0]);
    format_temp_c(t2, sizeof(t2), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 1U), snapshot->bms_temperature_celsius[1]);
    format_temp_c(t3, sizeof(t3), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 2U), snapshot->bms_temperature_celsius[2]);
    format_temp_c(t4, sizeof(t4), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 3U), snapshot->bms_temperature_celsius[3]);
    format_temp_c(mos, sizeof(mos), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 4U), snapshot->bms_temperature_celsius[4]);
    format_temp_c(bal, sizeof(bal), esp_bms_dashboard_snapshot_temperature_valid(snapshot, 5U), snapshot->bms_temperature_celsius[5]);
    bms_label_set(s_ui.temperature_values[0],
                  s_ui.bms_temperature_buf[0], sizeof(s_ui.bms_temperature_buf[0]), t1);
    bms_label_set(s_ui.temperature_values[1],
                  s_ui.bms_temperature_buf[1], sizeof(s_ui.bms_temperature_buf[1]), t2);
    bms_label_set(s_ui.temperature_values[2],
                  s_ui.bms_temperature_buf[2], sizeof(s_ui.bms_temperature_buf[2]), t3);
    bms_label_set(s_ui.temperature_values[3],
                  s_ui.bms_temperature_buf[3], sizeof(s_ui.bms_temperature_buf[3]), t4);
    bms_label_set(s_ui.temperature_values[4],
                  s_ui.bms_temperature_buf[4], sizeof(s_ui.bms_temperature_buf[4]), bal);
    bms_label_set(s_ui.temperature_values[5],
                  s_ui.bms_temperature_buf[5], sizeof(s_ui.bms_temperature_buf[5]), mos);
    if (s_ui.native_bms_dashboard) {
        bms_native_set_safety_status(snapshot);
    }

    set_quick_brightness_value(snapshot->brightness_percent, false, true);
    set_quick_volume_value(snapshot->volume_percent, false, true);
    update_quick_item_colors(snapshot);
}

