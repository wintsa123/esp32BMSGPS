/*
 * UI 模块: controller_dash
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
static void controller_label_set(lv_obj_t *label_obj,
                                 char *buffer,
                                 size_t buffer_len,
                                 const char *text)
{
    if (!label_obj) {
        return;
    }
    /* Skip only if the label already points at the buffer and the text is
     * unchanged; after a page release/recreate the label holds the LVGL
     * default placeholder text while the buffer still holds the old value. */
    if (lv_label_get_text(label_obj) == buffer && strcmp(buffer, text) == 0) {
        return;
    }
    snprintf(buffer, buffer_len, "%s", text);
    lv_label_set_text_static(label_obj, buffer);
}

void set_controller_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
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
    controller_label_set(s_ui.controller_gear, s_ui.controller_gear_buf,
                         sizeof(s_ui.controller_gear_buf),
                         controller_gear_text(snapshot->controller_gear,
                                              SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE),
                                              SNAPSHOT_FLAG(snapshot, CONTROLLER_GEAR_VALID)));
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
#endif

lv_obj_t *controller_dashboard_label(lv_obj_t *parent,
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

#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
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

static void create_controller_dashboard(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const bool native_portrait = s_ui.width == 320 && s_ui.height == 480;
    const bool native_landscape = s_ui.width == 480 && s_ui.height == 320;
    const bool native_layout = native_portrait || native_landscape;
    const int32_t frame_margin = native_layout ? 6 : 4;
    const int32_t panel_margin = native_layout ? 6 : 4;
    lv_obj_t *frame = controller_dashboard_panel(s_ui.controller_page,
                                                 frame_margin,
                                                 frame_margin,
                                                 s_ui.width - frame_margin * 2,
                                                 s_ui.height - frame_margin * 2,
                                                 COLOR_DASHBOARD_BG,
                                                 COLOR_DASHBOARD_BORDER);
    const int32_t speed_w = native_portrait ? 296 :
                            (native_landscape ? 312 : (portrait ? s_ui.width - 16 : 210));
    const int32_t speed_h = native_portrait ? 192 :
                            (native_landscape ? 194 : (portrait ? 120 : 154));
    const int32_t gear_x = native_portrait ? panel_margin :
                           (native_landscape ? 328 : (portrait ? 4 : 216));
    const int32_t gear_y = native_portrait ? 208 :
                           (native_landscape ? panel_margin : (portrait ? 126 : 4));
    const int32_t gear_w = native_portrait ? 296 :
                           (native_landscape ? 134 : (portrait ? s_ui.width - 16 : 92));
    const int32_t gear_h = native_portrait ? 122 :
                           (native_landscape ? 194 : (portrait ? 108 : 154));
    const int32_t stats_y = native_portrait ? 340 :
                            (native_landscape ? 210 : (portrait ? 236 : 160));
    const int32_t stats_w = native_layout ? s_ui.width - 24 : s_ui.width - 16;
    const int32_t stats_h = native_portrait ? 122 :
                            (native_landscape ? 92 : (portrait ? 72 : 68));

    lv_obj_t *speed_panel = controller_dashboard_panel(frame,
                                                       panel_margin,
                                                       panel_margin,
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
                                                       panel_margin,
                                                       stats_y,
                                                       stats_w,
                                                       stats_h,
                                                       COLOR_DASHBOARD_BG,
                                                       COLOR_DASHBOARD_BORDER);

    const int32_t title_inset = native_layout ? 14 : 8;
    lv_obj_t *speed_title = controller_dashboard_label(speed_panel,
                                                       "SPEED",
                                                       title_inset,
                                                       title_inset,
                                                       speed_w - title_inset * 2,
                                                       lv_font_montserrat_14.line_height,
                                                       &lv_font_montserrat_14,
                                                       COLOR_TEXT);
    lv_obj_set_style_text_align(speed_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_t *gear_title = controller_dashboard_label(gear_panel,
                                                      "GEAR",
                                                      title_inset,
                                                      title_inset,
                                                      gear_w - title_inset * 2,
                                                      lv_font_montserrat_14.line_height,
                                                      &lv_font_montserrat_14,
                                                      COLOR_TEXT);
    lv_obj_set_style_text_align(gear_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    s_ui.controller_speed = controller_dashboard_label(speed_panel,
                                                       s_ui.controller_speed_buf,
                                                       native_portrait ? 42 : (native_landscape ? 44 : 4),
                                                       native_portrait ? 62 :
                                                       (native_landscape ? 64 : (portrait ? 44 : 58)),
                                                       native_layout ? 144 : 116,
                                                       controller_digits_72.line_height,
                                                       &controller_digits_72,
                                                       COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.controller_speed, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_ui.controller_speed_unit = controller_dashboard_label(speed_panel,
                                                            s_ui.controller_speed_unit_buf,
                                                            native_portrait ? 190 :
                                                            (native_landscape ? 194 : 120),
                                                            native_portrait ? 82 :
                                                            (native_landscape ? 90 : (portrait ? 62 : 76)),
                                                            native_layout ? speed_w - 194 : speed_w - 124,
                                                            lv_font_montserrat_24.line_height,
                                                            &lv_font_montserrat_24,
                                                            COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.controller_speed_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.controller_gear = controller_dashboard_label(gear_panel,
                                                      s_ui.controller_gear_buf,
                                                      2,
                                                      native_portrait ? 42 :
                                                      (native_landscape ? 64 : (portrait ? 24 : 58)),
                                                      gear_w - 4,
                                                      lv_font_montserrat_48.line_height,
                                                      &lv_font_montserrat_48,
                                                      COLOR_TEXT);

    const lv_font_t *value_font = native_layout ? &lv_font_montserrat_24 :
                                                  &lv_font_montserrat_14;
    const lv_font_t *unit_font = &lv_font_montserrat_14;
    if (native_portrait) {
        const int32_t cell_w = stats_w / 2;
        const int32_t row_h = stats_h / 2;
        controller_dashboard_vertical_separator(stats_panel, cell_w, 4, stats_h - 8);
        dashboard_separator(stats_panel, 6, row_h, stats_w - 12);

        lv_obj_t *power_title = controller_dashboard_label(stats_panel, "POWER", 10, 8,
                                                            cell_w - 20,
                                                            unit_font->line_height, unit_font,
                                                            COLOR_TEXT);
        lv_obj_set_style_text_align(power_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_power = controller_dashboard_label(stats_panel,
                                                           s_ui.controller_power_buf,
                                                           30, 31, 50,
                                                           value_font->line_height, value_font,
                                                           COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "kW", 84, 36, 34,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *rpm_title = controller_dashboard_label(stats_panel, "RPM", cell_w + 10, 8,
                                                          cell_w - 20,
                                                          unit_font->line_height, unit_font,
                                                          COLOR_TEXT);
        lv_obj_set_style_text_align(rpm_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_rpm = controller_dashboard_label(stats_panel,
                                                         s_ui.controller_rpm_buf,
                                                         cell_w + 17, 31, 68,
                                                         value_font->line_height, value_font,
                                                         COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "RPM", cell_w + 89, 36, 42,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);

        lv_obj_t *controller_title = controller_dashboard_label(stats_panel, "CTRL", 10,
                                                                 row_h + 8, cell_w - 20,
                                                                 unit_font->line_height, unit_font,
                                                                 COLOR_TEXT);
        lv_obj_set_style_text_align(controller_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_temp = controller_dashboard_label(stats_panel,
                                                          s_ui.controller_temp_buf,
                                                          42, row_h + 31, 40,
                                                          value_font->line_height, value_font,
                                                          COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "C", 86, row_h + 36, 20,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *motor_title = controller_dashboard_label(stats_panel, "MOTOR", cell_w + 10,
                                                           row_h + 8, cell_w - 20,
                                                           unit_font->line_height, unit_font,
                                                           COLOR_TEXT);
        lv_obj_set_style_text_align(motor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_motor_temp = controller_dashboard_label(stats_panel,
                                                                s_ui.controller_motor_temp_buf,
                                                                cell_w + 42, row_h + 31, 40,
                                                                value_font->line_height, value_font,
                                                                COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "C", cell_w + 86, row_h + 36, 20,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
    } else if (native_landscape) {
        const int32_t col_w = stats_w / 4;
        for (int32_t index = 1; index < 4; ++index) {
            controller_dashboard_vertical_separator(stats_panel,
                                                    col_w * index,
                                                    6,
                                                    stats_h - 12);
        }
        lv_obj_t *power_title = controller_dashboard_label(stats_panel, "POWER", 10, 8,
                                                            col_w - 20,
                                                            unit_font->line_height, unit_font,
                                                            COLOR_TEXT);
        lv_obj_set_style_text_align(power_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_power = controller_dashboard_label(stats_panel,
                                                           s_ui.controller_power_buf,
                                                           10, 45, 46,
                                                           value_font->line_height, value_font,
                                                           COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "kW", 60, 50, 34,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *rpm_title = controller_dashboard_label(stats_panel, "RPM", col_w + 10, 8,
                                                          col_w - 20,
                                                          unit_font->line_height, unit_font,
                                                          COLOR_TEXT);
        lv_obj_set_style_text_align(rpm_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_rpm = controller_dashboard_label(stats_panel,
                                                         s_ui.controller_rpm_buf,
                                                         col_w + 4, 45, 68,
                                                         value_font->line_height, value_font,
                                                         COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "RPM", col_w + 76, 50, 34,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *controller_title = controller_dashboard_label(stats_panel, "CTRL", col_w * 2 + 10,
                                                                 8, col_w - 20,
                                                                 unit_font->line_height, unit_font,
                                                                 COLOR_TEXT);
        lv_obj_set_style_text_align(controller_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_temp = controller_dashboard_label(stats_panel,
                                                          s_ui.controller_temp_buf,
                                                          col_w * 2 + 22, 45, 40,
                                                          value_font->line_height, value_font,
                                                          COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "C", col_w * 2 + 66, 50, 20,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *motor_title = controller_dashboard_label(stats_panel, "MOTOR", col_w * 3 + 10,
                                                           8, col_w - 20,
                                                           unit_font->line_height, unit_font,
                                                           COLOR_TEXT);
        lv_obj_set_style_text_align(motor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_motor_temp = controller_dashboard_label(stats_panel,
                                                                s_ui.controller_motor_temp_buf,
                                                                col_w * 3 + 22, 45, 40,
                                                                value_font->line_height, value_font,
                                                                COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "C", col_w * 3 + 66, 50, 20,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
    } else if (portrait) {
        const int32_t cell_w = stats_w / 2;
        const int32_t row_h = stats_h / 2;
        const int32_t upper_value_y = native_portrait ? 22 : 10;
        const int32_t lower_title_y = row_h + (native_portrait ? 8 : 2);
        const int32_t lower_value_y = row_h + (native_portrait ? 34 : 18);
        controller_dashboard_vertical_separator(stats_panel, cell_w, 4, stats_h - 8);
        dashboard_separator(stats_panel, 6, row_h, stats_w - 12);

        s_ui.controller_power = controller_dashboard_label(stats_panel,
                                                           s_ui.controller_power_buf,
                                                           31,
                                                           upper_value_y,
                                                           24,
                                                           value_font->line_height,
                                                           value_font,
                                                           COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "kW", 58, upper_value_y, 28,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        s_ui.controller_rpm = controller_dashboard_label(stats_panel,
                                                         s_ui.controller_rpm_buf,
                                                         cell_w + 20,
                                                         upper_value_y,
                                                         38,
                                                         value_font->line_height,
                                                         value_font,
                                                         COLOR_TEXT);
        (void)controller_dashboard_label(stats_panel, "RPM", cell_w + 62, upper_value_y, 38,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);

        lv_obj_t *controller_title = controller_dashboard_label(stats_panel,
                                                                "CTRL",
                                                                6,
                                                                lower_title_y,
                                                                cell_w - 12,
                                                                unit_font->line_height,
                                                                unit_font,
                                                                COLOR_TEXT);
        lv_obj_set_style_text_align(controller_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_temp = controller_dashboard_label(stats_panel,
                                                          s_ui.controller_temp_buf,
                                                          40,
                                                          lower_value_y,
                                                          22,
                                                          value_font->line_height,
                                                          value_font,
                                                          COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", 66, lower_value_y, 14,
                                         unit_font->line_height, unit_font, COLOR_CONTROLLER_VALUE);
        lv_obj_t *motor_title = controller_dashboard_label(stats_panel,
                                                           "MOTOR",
                                                           cell_w + 6,
                                                           lower_title_y,
                                                           cell_w - 12,
                                                           unit_font->line_height,
                                                           unit_font,
                                                           COLOR_TEXT);
        lv_obj_set_style_text_align(motor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        s_ui.controller_motor_temp = controller_dashboard_label(stats_panel,
                                                                s_ui.controller_motor_temp_buf,
                                                                cell_w + 40,
                                                                lower_value_y,
                                                                22,
                                                                value_font->line_height,
                                                                value_font,
                                                                COLOR_CONTROLLER_VALUE);
        (void)controller_dashboard_label(stats_panel, "C", cell_w + 66, lower_value_y, 14,
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
#endif

void speed_dashboard_style_apply(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot || !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS)) {
        return;
    }

    const esp_bms_speed_dashboard_style_t style =
        speed_dashboard_style_from_snapshot(snapshot);
    const bool controller_monitor = style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
    const bool honda_fireblade = style == ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE;
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
    if (controller_monitor && !s_ui.controller_page) {
        s_ui.controller_page = lv_obj_create(s_ui.gps_page);
        clear_style(s_ui.controller_page);
        lv_obj_set_pos(s_ui.controller_page, 0, 0);
        lv_obj_set_size(s_ui.controller_page, s_ui.width, s_ui.height);
        lv_obj_set_style_bg_color(s_ui.controller_page, COLOR_DASHBOARD_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_ui.controller_page, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(s_ui.controller_page,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        create_controller_dashboard();
    }
#endif
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    if (honda_fireblade && !s_ui.fireblade_page) {
        create_fireblade_dashboard();
    }
#endif

    set_obj_hidden(s_ui.speed_static_background, controller_monitor || honda_fireblade);
    set_obj_hidden(s_ui.speed_art, controller_monitor || honda_fireblade);
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
    set_obj_hidden(s_ui.controller_page, !controller_monitor);
#endif
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    set_obj_hidden(s_ui.fireblade_page, !honda_fireblade);
#endif
#if ESP_BMS_FEATURE_DASHBOARD_CONTROLLER
    if (controller_monitor && s_ui.controller_page) {
        lv_obj_move_foreground(s_ui.controller_page);
    }
#endif
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    if (honda_fireblade && s_ui.fireblade_page) {
        lv_obj_move_foreground(s_ui.fireblade_page);
    }
#endif
}

