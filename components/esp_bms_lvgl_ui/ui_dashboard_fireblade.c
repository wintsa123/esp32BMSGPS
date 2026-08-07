/*
 * UI 模块: fireblade
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
static lv_point_t fireblade_circle_point(lv_point_t center, int32_t radius, int32_t angle)
{
    return speed_dashboard_point(
        center.x + ((radius * lv_trigo_sin((int16_t)(angle + 90))) >> LV_TRIGO_SHIFT),
        center.y + ((radius * lv_trigo_sin((int16_t)angle)) >> LV_TRIGO_SHIFT));
}

static void fireblade_format_date(char *buffer,
                                  size_t buffer_len,
                                  const esp_bms_dashboard_snapshot_t *snapshot)
{
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    static const char *const months[] = {
        "", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    if (!snapshot->gps_local_date_valid || snapshot->gps_local_weekday >= ARRAY_SIZE(weekdays) ||
        snapshot->gps_local_month == 0U || snapshot->gps_local_month >= ARRAY_SIZE(months)) {
        snprintf(buffer, buffer_len, "--- -- --- ----");
        return;
    }
    snprintf(buffer,
             buffer_len,
             "%s %02u %s %04u",
             weekdays[snapshot->gps_local_weekday],
             snapshot->gps_local_day,
             months[snapshot->gps_local_month],
             snapshot->gps_local_year);
}

static lv_obj_t *fireblade_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height,
                                 lv_color_t color,
                                 int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *fireblade_label(lv_obj_t *parent,
                                 const char *text,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height,
                                 const lv_font_t *font,
                                 lv_color_t color,
                                 lv_text_align_t align)
{
    lv_obj_t *obj = controller_dashboard_label(parent,
                                               text,
                                               x,
                                               y,
                                               width,
                                               height,
                                               font,
                                               color);
    lv_obj_set_style_text_align(obj, align, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *fireblade_arc(lv_obj_t *parent,
                               lv_point_t center,
                               int32_t radius,
                               int32_t start_angle,
                               int32_t end_angle,
                               lv_color_t color,
                               int32_t width)
{
    lv_obj_t *arc = lv_arc_create(parent);
    clear_style(arc);
    lv_obj_set_pos(arc, center.x - radius, center.y - radius);
    lv_obj_set_size(arc, radius * 2, radius * 2);
    lv_arc_set_bg_angles(arc, start_angle, end_angle);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return arc;
}

static lv_obj_t *fireblade_line(lv_obj_t *parent,
                                lv_point_precise_t points[2],
                                lv_color_t color,
                                int32_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    clear_style(line);
    lv_line_set_points_mutable(line, points, 2);
    lv_obj_set_style_line_color(line, color, LV_PART_MAIN);
    lv_obj_set_style_line_width(line, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, false, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

static void fireblade_needle_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    const lv_point_precise_t *points = lv_event_get_user_data(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);

    speed_dashboard_draw_triangle(
        layer,
        speed_dashboard_point(coords.x1 + (int32_t)points[0].x,
                              coords.y1 + (int32_t)points[0].y),
        speed_dashboard_point(coords.x1 + (int32_t)points[1].x,
                              coords.y1 + (int32_t)points[1].y),
        speed_dashboard_point(coords.x1 + (int32_t)points[2].x,
                              coords.y1 + (int32_t)points[2].y),
        lv_obj_get_style_bg_color(object, LV_PART_MAIN));
}

static lv_obj_t *fireblade_needle_triangle(lv_obj_t *parent,
                                           lv_point_precise_t points[3],
                                           lv_color_t color)
{
    lv_obj_t *needle = lv_obj_create(parent);
    clear_style(needle);
    lv_obj_set_size(needle, 1, 1);
    lv_obj_set_style_bg_color(needle, color, LV_PART_MAIN);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(needle, fireblade_needle_draw_event_cb, LV_EVENT_DRAW_MAIN, points);
    return needle;
}

static void fireblade_needle_line_set(lv_obj_t *needle,
                                      lv_point_precise_t points[3],
                                      lv_point_t start,
                                      lv_point_t end)
{
    if (!needle) {
        return;
    }

    const int32_t dx = end.x - start.x;
    const int32_t dy = end.y - start.y;
    const int32_t length = lv_sqrt32((uint32_t)(dx * dx + dy * dy));
    if (length == 0) {
        return;
    }

    const bool native_fireblade = s_ui.native_fireblade_dashboard;
    const bool compact_portrait = s_ui.width == 240 && s_ui.height == 320;
    const int32_t half_width = needle == s_ui.fireblade_needle_black
                                   ? (native_fireblade ? 7 : (compact_portrait ? 6 : 8))
                                   : (native_fireblade ? 5 : 4);
    const lv_point_t left = speed_dashboard_point(start.x - (dy * half_width / length),
                                                  start.y + (dx * half_width / length));
    const lv_point_t right = speed_dashboard_point(start.x + (dy * half_width / length),
                                                   start.y - (dx * half_width / length));
    const int32_t x = LV_MIN(LV_MIN(left.x, right.x), end.x);
    const int32_t y = LV_MIN(LV_MIN(left.y, right.y), end.y);
    const int32_t x2 = LV_MAX(LV_MAX(left.x, right.x), end.x);
    const int32_t y2 = LV_MAX(LV_MAX(left.y, right.y), end.y);

    lv_obj_invalidate(needle);
    lv_obj_set_pos(needle, x, y);
    lv_obj_set_size(needle, x2 - x + 1, y2 - y + 1);
    points[0].x = left.x - x;
    points[0].y = left.y - y;
    points[1].x = right.x - x;
    points[1].y = right.y - y;
    points[2].x = end.x - x;
    points[2].y = end.y - y;
    lv_obj_invalidate(needle);
}

static void fireblade_add_title(lv_obj_t *parent,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                const char *text)
{
    (void)fireblade_panel(parent, x, y, width, 15, COLOR_FIREBLADE_GRAY, 0);
    (void)fireblade_label(parent,
                          text,
                          x + 5,
                          y + 2,
                          width - 7,
                          12,
                          &settings_zh_10,
                          COLOR_FIREBLADE_BLACK,
                          LV_TEXT_ALIGN_LEFT);
}

static void fireblade_title_extension_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);

    const lv_point_t top_left = speed_dashboard_point(coords.x1, coords.y1);
    const lv_point_t bottom_left = speed_dashboard_point(coords.x1, coords.y2);
    const lv_point_t outer = lv_event_get_user_data(event)
                                 ? speed_dashboard_point(coords.x2, coords.y1)
                                 : speed_dashboard_point(coords.x2, coords.y2);
    speed_dashboard_draw_triangle(layer,
                                  top_left,
                                  outer,
                                  bottom_left,
                                  COLOR_FIREBLADE_GRAY);
}

static void fireblade_add_title_extension(lv_obj_t *parent,
                                          int32_t x,
                                          int32_t y,
                                          int32_t width,
                                          int32_t height,
                                          bool extend_top)
{
    static const bool top_marker = true;
    lv_obj_t *extensions = lv_obj_create(parent);
    clear_style(extensions);
    lv_obj_set_pos(extensions, x, y);
    lv_obj_set_size(extensions, width, height);
    lv_obj_clear_flag(extensions, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(extensions,
                        fireblade_title_extension_draw_event_cb,
                        LV_EVENT_DRAW_MAIN,
                        extend_top ? (void *)&top_marker : NULL);
}

static void fireblade_add_title_extensions(lv_obj_t *parent)
{
    fireblade_add_title_extension(parent, 64, 66, 12, 15, true);
    fireblade_add_title_extension(parent, 67, 161, 9, 15, false);
    fireblade_add_title_extension(parent, 99, 202, 25, 15, false);
}

static void fireblade_add_scale(lv_obj_t *parent, lv_point_t center, int32_t radius)
{
    const int32_t danger_start_angle =
        FIREBLADE_ARC_START_ANGLE +
        (int32_t)(16U * FIREBLADE_ARC_SWEEP_ANGLE / FIREBLADE_SPEED_TICK_COUNT);
    (void)fireblade_arc(parent,
                        center,
                        radius - 10,
                        FIREBLADE_ARC_START_ANGLE,
                        FIREBLADE_ARC_START_ANGLE + FIREBLADE_ARC_SWEEP_ANGLE,
                        COLOR_FIREBLADE_GRAY,
                        28);
    (void)fireblade_arc(parent,
                        center,
                        radius - 10,
                        danger_start_angle,
                        FIREBLADE_ARC_START_ANGLE + FIREBLADE_ARC_SWEEP_ANGLE,
                        COLOR_FIREBLADE_DANGER_BG,
                        28);
    (void)fireblade_arc(parent,
                        center,
                        radius + 1,
                        FIREBLADE_ARC_START_ANGLE,
                        FIREBLADE_ARC_START_ANGLE + FIREBLADE_ARC_SWEEP_ANGLE,
                        COLOR_FIREBLADE_MODE,
                        2);

    for (uint32_t index = 0U; index <= FIREBLADE_SPEED_TICK_COUNT; index += 2U) {
        const uint32_t label_index = index / 2U;
        const int32_t angle = FIREBLADE_ARC_START_ANGLE +
                              (int32_t)(index * FIREBLADE_ARC_SWEEP_ANGLE /
                                        FIREBLADE_SPEED_TICK_COUNT);
        const bool danger = index >= 16U;
        const lv_point_t outer = fireblade_circle_point(center, radius + 2, angle);
        const lv_point_t inner = fireblade_circle_point(center, radius - 12, angle);
        s_ui.fireblade_tick_points[label_index][0].x = outer.x;
        s_ui.fireblade_tick_points[label_index][0].y = outer.y;
        s_ui.fireblade_tick_points[label_index][1].x = inner.x;
        s_ui.fireblade_tick_points[label_index][1].y = inner.y;
        (void)fireblade_line(parent,
                             s_ui.fireblade_tick_points[label_index],
                             danger ? COLOR_FIREBLADE_RED : COLOR_FIREBLADE_MODE,
                             2);

        const lv_point_t label_center = fireblade_circle_point(center, radius - 27, angle);
        (void)fireblade_label(parent,
                              FIREBLADE_SCALE_LABELS[label_index],
                              label_center.x - 17,
                              label_center.y - 8,
                              35,
                              18,
                              &fireblade_scale_digits_14,
                              danger ? COLOR_FIREBLADE_RED : COLOR_FIREBLADE_BLACK,
                              LV_TEXT_ALIGN_CENTER);
    }

    (void)fireblade_arc(parent,
                        center,
                        radius + 1,
                        danger_start_angle,
                        FIREBLADE_ARC_START_ANGLE + FIREBLADE_ARC_SWEEP_ANGLE,
                        COLOR_FIREBLADE_RED,
                        3);
}

static void fireblade_add_needle(lv_obj_t *parent, lv_point_t center, int32_t radius)
{
    const lv_point_t start =
        fireblade_circle_point(center, FIREBLADE_GEAR_RADIUS, FIREBLADE_ARC_START_ANGLE);
    const lv_point_t tip = fireblade_circle_point(center, radius + 1, FIREBLADE_ARC_START_ANGLE);
    s_ui.fireblade_needle_center = center;
    s_ui.fireblade_needle_radius = radius + 1;
    memset(s_ui.fireblade_needle_black_points, 0, sizeof(s_ui.fireblade_needle_black_points));
    memset(s_ui.fireblade_needle_red_points, 0, sizeof(s_ui.fireblade_needle_red_points));
    s_ui.fireblade_needle_black =
        fireblade_needle_triangle(parent,
                                  s_ui.fireblade_needle_black_points,
                                  COLOR_FIREBLADE_BLACK);
    s_ui.fireblade_needle_red =
        fireblade_needle_triangle(parent,
                                  s_ui.fireblade_needle_red_points,
                                  COLOR_FIREBLADE_RED);
    fireblade_needle_line_set(s_ui.fireblade_needle_black,
                              s_ui.fireblade_needle_black_points,
                              start,
                              tip);
    fireblade_needle_line_set(s_ui.fireblade_needle_red,
                              s_ui.fireblade_needle_red_points,
                              start,
                              tip);
}

static lv_obj_t *fireblade_add_gear_circle(lv_obj_t *parent, lv_point_t center)
{
    lv_obj_t *circle = fireblade_panel(parent,
                                       center.x - FIREBLADE_GEAR_RADIUS,
                                       center.y - FIREBLADE_GEAR_RADIUS,
                                       FIREBLADE_GEAR_RADIUS * 2,
                                       FIREBLADE_GEAR_RADIUS * 2,
                                       COLOR_WHITE,
                                       LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(circle, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(circle, COLOR_FIREBLADE_GEAR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
    return circle;
}

static void fireblade_add_gear_dynamic(lv_obj_t *parent, lv_point_t center)
{
    s_ui.fireblade_gear_unit = fireblade_label(parent,
                                               s_ui.fireblade_gear_unit_buf,
                                               center.x - 32,
                                               center.y - 55,
                                               64,
                                               16,
                                               &settings_zh_10,
                                               COLOR_FIREBLADE_BLACK,
                                               LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_gear = fireblade_label(parent,
                                          s_ui.fireblade_gear_buf,
                                          center.x - 28,
                                          center.y - 25,
                                          57,
                                          54,
                                          &lv_font_montserrat_48,
                                          COLOR_FIREBLADE_GREEN,
                                          LV_TEXT_ALIGN_CENTER);
}

static void fireblade_add_gear(lv_obj_t *parent, lv_point_t center)
{
    lv_obj_t *circle = fireblade_add_gear_circle(parent, center);
    lv_obj_move_foreground(circle);
    fireblade_add_gear_dynamic(parent, center);
}

static void fireblade_native_title(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t width,
                                   const char *text)
{
    (void)fireblade_panel(parent, x, y, width, 20, COLOR_FIREBLADE_GRAY, 0);
    (void)fireblade_label(parent,
                          text,
                          x + 7,
                          y + 3,
                          width - 10,
                          15,
                          &settings_zh_13,
                          COLOR_FIREBLADE_BLACK,
                          LV_TEXT_ALIGN_LEFT);
}

static void fireblade_create_native_landscape(lv_obj_t *page)
{
    const int32_t width = s_ui.width;
    const int32_t height = s_ui.height;
    const int32_t bridge_h = (height * 27) / 100;
    const int32_t side_w = (width * 26) / 100;
    const int32_t center_x = width / 2;
    const int32_t center_y = height / 2;
    const int32_t base_radius = LV_MIN((height * 47) / 100, (width * 32) / 100);
    const int32_t bridge_radius = base_radius + 20;
    const int32_t speed_radius = base_radius - 5;
    const int32_t metric_x = 7;
    const int32_t metric_w = side_w - 12;
    const int32_t metric_unit_x = metric_x + 36;
    const int32_t temperature_value_h = 20;
    const int32_t motor_temp_y = bridge_h - temperature_value_h;
    const int32_t controller_temp_y = motor_temp_y - temperature_value_h - 3;
    const int32_t mode_y = bridge_h + 2;
    const int32_t metric_y[] = {
        bridge_h + 8,
        bridge_h + 60,
        bridge_h + 112,
        bridge_h + 164,
    };
    const int32_t metric_value_y[] = {
        metric_y[0] + 33,
        metric_y[1] + 33,
        metric_y[2] + 33,
        height - 36,
    };
    static const int32_t metric_title_w[] = { 82, 74, 80, 103 };
    const lv_point_t center = speed_dashboard_point(center_x, center_y);

    s_ui.native_fireblade_dashboard = true;
    lv_obj_t *static_layer = dashboard_native_layer(page, 0, 0, width, height);
    lv_obj_set_style_bg_color(static_layer, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(static_layer, LV_OPA_COVER, LV_PART_MAIN);
    (void)fireblade_panel(static_layer, 0, 0, width, bridge_h, COLOR_FIREBLADE_BRIDGE, 0);
    (void)fireblade_panel(static_layer,
                          width - 90,
                          mode_y,
                          90,
                          38,
                          COLOR_FIREBLADE_MODE,
                          0);
    (void)fireblade_panel(static_layer,
                          center.x - bridge_radius,
                          center.y - bridge_radius,
                          bridge_radius * 2,
                          bridge_radius * 2,
                          COLOR_WHITE,
                          LV_RADIUS_CIRCLE);
    fireblade_native_title(static_layer, 0, metric_y[0], metric_title_w[0], "电耗");
    fireblade_native_title(static_layer, 0, metric_y[1], metric_title_w[1], "剩余");
    fireblade_native_title(static_layer, 0, metric_y[2], metric_title_w[2], "均速");
    fireblade_native_title(static_layer, 0, metric_y[3], metric_title_w[3], "日期");
    fireblade_add_title_extension(static_layer,
                                  metric_title_w[0] - 1,
                                  metric_y[0],
                                  7,
                                  20,
                                  true);
    fireblade_add_title_extension(static_layer,
                                  metric_title_w[2] - 1,
                                  metric_y[2],
                                  6,
                                  20,
                                  false);
    fireblade_add_title_extension(static_layer,
                                  metric_title_w[3] - 1,
                                  metric_y[3],
                                  16,
                                  20,
                                  false);
    fireblade_add_scale(static_layer, center, speed_radius);

    (void)fireblade_label(static_layer, "控", 10, controller_temp_y + 1, 22, 18,
                          &settings_zh_13, COLOR_WHITE, LV_TEXT_ALIGN_LEFT);
    (void)fireblade_label(static_layer, "电机", 10, motor_temp_y + 1, 32, 18,
                          &settings_zh_13, COLOR_WHITE, LV_TEXT_ALIGN_LEFT);
    (void)fireblade_label(static_layer, "MODE 1", width - 82, mode_y + 13, 74, 12,
                          &settings_zh_10, COLOR_WHITE, LV_TEXT_ALIGN_CENTER);
    (void)fireblade_label(static_layer, "km", metric_unit_x, metric_value_y[1] + 6,
                          24, 10, &settings_zh_10, COLOR_FIREBLADE_BLACK,
                          LV_TEXT_ALIGN_LEFT);

    (void)dashboard_static_cache_finalize(&s_ui.fireblade_static_cache,
                                           page,
                                           static_layer,
                                           "fireblade");

    lv_obj_t *dynamic_layer = dashboard_native_layer(page, 0, 0, width, height);
    s_ui.fireblade_time = fireblade_label(dynamic_layer,
                                          s_ui.fireblade_time_buf,
                                          10,
                                          10,
                                          100,
                                          24,
                                          &settings_zh_16,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_controller_temp = fireblade_label(dynamic_layer,
                                                      s_ui.fireblade_controller_temp_buf,
                                                      44,
                                                      controller_temp_y,
                                                      48,
                                                      temperature_value_h,
                                                      &settings_zh_16,
                                                      COLOR_WHITE,
                                                      LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_motor_temp = fireblade_label(dynamic_layer,
                                                 s_ui.fireblade_motor_temp_buf,
                                                 44,
                                                 motor_temp_y,
                                                 48,
                                                 temperature_value_h,
                                                 &settings_zh_16,
                                                 COLOR_WHITE,
                                                 LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_soc = fireblade_label(dynamic_layer,
                                         s_ui.fireblade_soc_buf,
                                         width - 116,
                                         10,
                                         104,
                                         28,
                                         &lv_font_montserrat_28,
                                         COLOR_WHITE,
                                         LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_consumption = fireblade_label(dynamic_layer,
                                                 s_ui.fireblade_consumption_buf,
                                                 metric_x,
                                                 metric_value_y[0],
                                                 32,
                                                 16,
                                                 &fireblade_info_digits_12,
                                                 COLOR_FIREBLADE_BLACK,
                                                 LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_consumption_unit = fireblade_label(dynamic_layer,
                                                      s_ui.fireblade_consumption_unit_buf,
                                                      metric_unit_x,
                                                      metric_value_y[0] + 6,
                                                      35,
                                                      10,
                                                      &settings_zh_10,
                                                      COLOR_FIREBLADE_BLACK,
                                                      LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_range = fireblade_label(dynamic_layer,
                                           s_ui.fireblade_range_buf,
                                           metric_x,
                                           metric_value_y[1],
                                           32,
                                           16,
                                           &fireblade_info_digits_12,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_average_speed = fireblade_label(dynamic_layer,
                                                   s_ui.fireblade_average_speed_buf,
                                                   metric_x,
                                                   metric_value_y[2],
                                                   32,
                                                   16,
                                                   &fireblade_info_digits_12,
                                                   COLOR_FIREBLADE_BLACK,
                                                   LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_average_speed_unit = fireblade_label(dynamic_layer,
                                                        s_ui.fireblade_average_speed_unit_buf,
                                                        metric_unit_x,
                                                        metric_value_y[2] + 6,
                                                        35,
                                                        10,
                                                        &settings_zh_10,
                                                        COLOR_FIREBLADE_BLACK,
                                                        LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_date = fireblade_label(dynamic_layer,
                                          s_ui.fireblade_date_buf,
                                          metric_x,
                                          metric_value_y[3],
                                          metric_w - 8,
                                          14,
                                          &settings_zh_10,
                                          COLOR_FIREBLADE_BLACK,
                                          LV_TEXT_ALIGN_LEFT);

    fireblade_add_needle(dynamic_layer, center, speed_radius);
    lv_obj_t *gear_circle = fireblade_add_gear_circle(dynamic_layer, center);
    lv_obj_set_pos(gear_circle, center.x - 34, center.y - 34);
    lv_obj_set_size(gear_circle, 68, 68);
    fireblade_add_gear_dynamic(dynamic_layer, center);
    lv_obj_set_pos(s_ui.fireblade_gear, center.x - 32, center.y - 24);
    lv_obj_set_size(s_ui.fireblade_gear, 64, 64);
    lv_obj_set_style_text_font(s_ui.fireblade_gear, &lv_font_montserrat_48, LV_PART_MAIN);
    s_ui.fireblade_speed = fireblade_label(dynamic_layer,
                                           s_ui.fireblade_speed_buf,
                                           center.x + 18,
                                           center.y + 16,
                                           speed_radius,
                                           68,
                                           &fireblade_digits_64,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_speed_unit = fireblade_label(dynamic_layer,
                                                s_ui.fireblade_speed_unit_buf,
                                                center.x + 18,
                                                center.y + 82,
                                                speed_radius,
                                                18,
                                                &lv_font_montserrat_14,
                                                COLOR_FIREBLADE_BLACK,
                                                LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_transform_scale(s_ui.fireblade_speed, 272, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(s_ui.fireblade_speed_unit, 304, LV_PART_MAIN);
}

static void fireblade_create_native_portrait(lv_obj_t *page)
{
    const int32_t width = s_ui.width;
    const int32_t height = s_ui.height;
    const int32_t header_h = 42;
    const int32_t scale_radius = 136;
    const int32_t metric_left_x = 10;
    const int32_t metric_right_x = 168;
    const int32_t metric_w = 142;
    const int32_t metric_top_y = height - 130;
    const int32_t metric_bottom_y = height - 70;
    const int32_t range_value_y = metric_top_y + 24;
    const int32_t range_value_h = 28;
    const lv_point_t center = speed_dashboard_point(width / 2, 205);

    s_ui.native_fireblade_dashboard = true;
    (void)fireblade_panel(page, 0, 0, width, header_h, COLOR_FIREBLADE_BLACK, 0);
    fireblade_add_scale(page, center, scale_radius);
    fireblade_native_title(page, metric_left_x, metric_top_y, metric_w, "电耗");
    fireblade_native_title(page, metric_right_x, metric_top_y, metric_w, "剩余");
    fireblade_native_title(page, metric_left_x, metric_bottom_y, metric_w, "均速");
    fireblade_native_title(page, metric_right_x, metric_bottom_y, metric_w, "控 / 电机");

    (void)fireblade_label(page,
                          "km",
                          metric_right_x + 110,
                          range_value_y + range_value_h - settings_zh_10.line_height,
                          24,
                          settings_zh_10.line_height,
                          &settings_zh_10,
                          COLOR_FIREBLADE_BLACK,
                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_time = fireblade_label(page,
                                          s_ui.fireblade_time_buf,
                                          82,
                                          5,
                                          104,
                                          30,
                                          &lv_font_montserrat_28,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_date = fireblade_label(page,
                                          s_ui.fireblade_date_buf,
                                          8,
                                          14,
                                          68,
                                          14,
                                          &lv_font_montserrat_14,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_controller_temp = fireblade_label(page,
                                                      s_ui.fireblade_controller_temp_buf,
                                                      metric_right_x + 4,
                                                      metric_bottom_y + 24,
                                                      metric_w - 8,
                                                      28,
                                                      &lv_font_montserrat_24,
                                                      COLOR_FIREBLADE_BLACK,
                                                      LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_motor_temp = NULL;
    s_ui.fireblade_soc = fireblade_label(page,
                                         s_ui.fireblade_soc_buf,
                                         width - 76,
                                         5,
                                         68,
                                         30,
                                         &lv_font_montserrat_28,
                                         COLOR_WHITE,
                                         LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_consumption = fireblade_label(page,
                                                 s_ui.fireblade_consumption_buf,
                                                 metric_left_x + 4,
                                                 metric_top_y + 24,
                                                 76,
                                                 28,
                                                 &lv_font_montserrat_24,
                                                 COLOR_FIREBLADE_BLACK,
                                                 LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_consumption_unit = fireblade_label(page,
                                                      s_ui.fireblade_consumption_unit_buf,
                                                      metric_left_x + 88,
                                                      metric_top_y + 37,
                                                      50,
                                                      12,
                                                      &settings_zh_10,
                                                      COLOR_FIREBLADE_BLACK,
                                                      LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_range = fireblade_label(page,
                                           s_ui.fireblade_range_buf,
                                           metric_right_x + 10,
                                           range_value_y,
                                           122,
                                           range_value_h,
                                           &lv_font_montserrat_24,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_average_speed = fireblade_label(page,
                                                   s_ui.fireblade_average_speed_buf,
                                                   metric_left_x + 4,
                                                   metric_bottom_y + 24,
                                                   76,
                                                   28,
                                                   &lv_font_montserrat_24,
                                                   COLOR_FIREBLADE_BLACK,
                                                   LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_average_speed_unit = fireblade_label(page,
                                                        s_ui.fireblade_average_speed_unit_buf,
                                                        metric_left_x + 88,
                                                        metric_bottom_y + 37,
                                                        50,
                                                        12,
                                                        &settings_zh_10,
                                                        COLOR_FIREBLADE_BLACK,
                                                        LV_TEXT_ALIGN_LEFT);
    fireblade_add_needle(page, center, scale_radius);
    lv_obj_t *gear_circle = fireblade_add_gear_circle(page, center);
    lv_obj_set_pos(gear_circle, center.x - 35, center.y - 35);
    lv_obj_set_size(gear_circle, 70, 70);
    fireblade_add_gear_dynamic(page, center);
    lv_obj_set_pos(s_ui.fireblade_gear, center.x - 32, center.y - 24);
    lv_obj_set_size(s_ui.fireblade_gear, 64, 64);
    lv_obj_set_style_text_font(s_ui.fireblade_gear, &lv_font_montserrat_48, LV_PART_MAIN);
    s_ui.fireblade_speed = fireblade_label(page,
                                           s_ui.fireblade_speed_buf,
                                           center.x + 14,
                                           center.y + 29,
                                           130,
                                           68,
                                           &fireblade_digits_64,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_speed_unit = fireblade_label(page,
                                                s_ui.fireblade_speed_unit_buf,
                                                center.x + 14,
                                                center.y + 95,
                                                130,
                                                18,
                                                &lv_font_montserrat_14,
                                                COLOR_FIREBLADE_BLACK,
                                                LV_TEXT_ALIGN_RIGHT);
}

static void fireblade_create_landscape(lv_obj_t *parent)
{
    (void)fireblade_panel(parent, 0, 0, 320, 64, COLOR_FIREBLADE_BRIDGE, 0);
    (void)fireblade_panel(parent, 260, 66, 60, 30, COLOR_FIREBLADE_MODE, 0);
    const lv_point_t center = speed_dashboard_point(166, 120);
    (void)fireblade_panel(parent,
                          center.x - 122,
                          center.y - 122,
                          244,
                          244,
                          COLOR_WHITE,
                          LV_RADIUS_CIRCLE);
    fireblade_add_title(parent, 0, 66, 65, "电耗");
    fireblade_add_title(parent, 0, 113, 60, "剩余");
    fireblade_add_title(parent, 0, 161, 68, "均速");
    fireblade_add_title(parent, 0, 202, 100, "日期");
    fireblade_add_title_extensions(parent);
    fireblade_add_scale(parent, center, 102);

    (void)fireblade_label(parent, "控", 6, 27, 12, 12,
                          &settings_zh_10, COLOR_WHITE, LV_TEXT_ALIGN_LEFT);
    (void)fireblade_label(parent, "电机", 6, 43, 22, 12,
                          &settings_zh_10, COLOR_WHITE, LV_TEXT_ALIGN_LEFT);
    (void)fireblade_label(parent, "MODE", 284, 68, 32, 12,
                          &settings_zh_10, COLOR_WHITE, LV_TEXT_ALIGN_LEFT);
    (void)fireblade_label(parent, "1", 295, 80, 14, 14,
                          &fireblade_info_digits_12, COLOR_WHITE, LV_TEXT_ALIGN_CENTER);
    (void)fireblade_label(parent, "km", 42, 140, 22, 8,
                          &fireblade_info_units_8, COLOR_FIREBLADE_BLACK, LV_TEXT_ALIGN_LEFT);

    s_ui.fireblade_time = fireblade_label(parent,
                                          s_ui.fireblade_time_buf,
                                          6,
                                          4,
                                          70,
                                          20,
                                          &settings_zh_16,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_controller_temp =
        fireblade_label(parent,
                        s_ui.fireblade_controller_temp_buf,
                        28,
                        27,
                        32,
                        12,
                        &settings_zh_10,
                        COLOR_WHITE,
                        LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_motor_temp =
        fireblade_label(parent,
                        s_ui.fireblade_motor_temp_buf,
                        28,
                        43,
                        32,
                        12,
                        &settings_zh_10,
                        COLOR_WHITE,
                        LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_soc = fireblade_label(parent,
                                         s_ui.fireblade_soc_buf,
                                         264,
                                         5,
                                         51,
                                         27,
                                         &lv_font_montserrat_24,
                                         COLOR_WHITE,
                                         LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_consumption =
        fireblade_label(parent,
                        s_ui.fireblade_consumption_buf,
                        3,
                        84,
                        62,
                        14,
                        &fireblade_info_digits_12,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_consumption_unit =
        fireblade_label(parent,
                        s_ui.fireblade_consumption_unit_buf,
                        3,
                        101,
                        62,
                        8,
                        &fireblade_info_units_8,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_range = fireblade_label(parent,
                                           s_ui.fireblade_range_buf,
                                           3,
                                           136,
                                           37,
                                           14,
                                           &fireblade_info_digits_12,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_average_speed =
        fireblade_label(parent,
                        s_ui.fireblade_average_speed_buf,
                        3,
                        184,
                        37,
                        14,
                        &fireblade_info_digits_12,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_average_speed_unit =
        fireblade_label(parent,
                        s_ui.fireblade_average_speed_unit_buf,
                        42,
                        188,
                        28,
                        8,
                        &fireblade_info_units_8,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_date = fireblade_label(parent,
                                          s_ui.fireblade_date_buf,
                                          5,
                                          219,
                                          112,
                                          12,
                                          &settings_zh_10,
                                          COLOR_FIREBLADE_BLACK,
                                          LV_TEXT_ALIGN_LEFT);

    fireblade_add_needle(parent, center, 102);
    fireblade_add_gear(parent, center);
    s_ui.fireblade_speed = fireblade_label(parent,
                                           s_ui.fireblade_speed_buf,
                                           204,
                                           153,
                                           112,
                                           60,
                                           &fireblade_digits_64,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_speed_unit =
        fireblade_label(parent,
                        s_ui.fireblade_speed_unit_buf,
                        204,
                        211,
                        112,
                        20,
                        &lv_font_montserrat_14,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_RIGHT);
}

static void fireblade_create_portrait(lv_obj_t *parent)
{
    (void)fireblade_panel(parent, 0, 0, 240, 35, COLOR_FIREBLADE_BLACK, 0);
    const lv_point_t center = speed_dashboard_point(124, 145);
    fireblade_add_scale(parent, center, 102);

    s_ui.fireblade_time = fireblade_label(parent,
                                          s_ui.fireblade_time_buf,
                                          68,
                                          4,
                                          94,
                                          27,
                                          &lv_font_montserrat_24,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_date = fireblade_label(parent,
                                          s_ui.fireblade_date_buf,
                                          5,
                                          11,
                                          58,
                                          14,
                                          &lv_font_montserrat_14,
                                          COLOR_WHITE,
                                          LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_controller_temp =
        fireblade_label(parent,
                        s_ui.fireblade_controller_temp_buf,
                        126,
                        289,
                        101,
                        25,
                        &lv_font_montserrat_24,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_CENTER);
    s_ui.fireblade_motor_temp = NULL;
    s_ui.fireblade_soc = fireblade_label(parent,
                                         s_ui.fireblade_soc_buf,
                                         183,
                                         6,
                                         53,
                                         25,
                                         &lv_font_montserrat_24,
                                         COLOR_WHITE,
                                         LV_TEXT_ALIGN_RIGHT);

    fireblade_add_needle(parent, center, 102);
    fireblade_add_gear(parent, center);
    s_ui.fireblade_speed = fireblade_label(parent,
                                           s_ui.fireblade_speed_buf,
                                           124,
                                           174,
                                           112,
                                           60,
                                           &fireblade_digits_64,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_speed_unit =
        fireblade_label(parent,
                        s_ui.fireblade_speed_unit_buf,
                        124,
                        220,
                        112,
                        12,
                        &settings_zh_10,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_RIGHT);

    fireblade_add_title(parent, 10, 233, 107, "电耗");
    fireblade_add_title(parent, 123, 233, 107, "剩余");
    fireblade_add_title(parent, 10, 273, 107, "均速");
    fireblade_add_title(parent, 123, 273, 107, "控 / 电机");
    s_ui.fireblade_consumption =
        fireblade_label(parent,
                        s_ui.fireblade_consumption_buf,
                        13,
                        249,
                        64,
                        25,
                        &lv_font_montserrat_24,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_consumption_unit =
        fireblade_label(parent,
                        s_ui.fireblade_consumption_unit_buf,
                        82,
                        261,
                        35,
                        11,
                        &settings_zh_10,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_range = fireblade_label(parent,
                                           s_ui.fireblade_range_buf,
                                           126,
                                           249,
                                           64,
                                           25,
                                           &lv_font_montserrat_24,
                                           COLOR_FIREBLADE_BLACK,
                                           LV_TEXT_ALIGN_RIGHT);
    (void)fireblade_label(parent, "km", 195, 261, 35, 11,
                          &settings_zh_10, COLOR_FIREBLADE_BLACK, LV_TEXT_ALIGN_LEFT);
    s_ui.fireblade_average_speed =
        fireblade_label(parent,
                        s_ui.fireblade_average_speed_buf,
                        13,
                        289,
                        64,
                        25,
                        &lv_font_montserrat_24,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_RIGHT);
    s_ui.fireblade_average_speed_unit =
        fireblade_label(parent,
                        s_ui.fireblade_average_speed_unit_buf,
                        82,
                        301,
                        35,
                        11,
                        &settings_zh_10,
                        COLOR_FIREBLADE_BLACK,
                        LV_TEXT_ALIGN_LEFT);
}

void set_fireblade_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!s_ui.fireblade_page) {
        return;
    }

    char text[24];
    char date_text[24];
    const bool portrait_fireblade = s_ui.width < s_ui.height;
    if (portrait_fireblade) {
        if (snapshot->gps_local_date_valid) {
            snprintf(date_text,
                     sizeof(date_text),
                     "%02u-%02u-%02u",
                     snapshot->gps_local_year % 100U,
                     snapshot->gps_local_month,
                     snapshot->gps_local_day);
        } else {
            snprintf(date_text, sizeof(date_text), "-- -- --");
        }
        if (snapshot->gps_local_time_valid) {
            snprintf(text,
                     sizeof(text),
                     "%02u:%02u",
                     snapshot->gps_local_hour,
                     snapshot->gps_local_minute);
        } else {
            snprintf(text, sizeof(text), "--:--");
        }
    } else if (snapshot->gps_local_time_valid) {
        snprintf(text,
                 sizeof(text),
                 "%02u:%02u",
                 snapshot->gps_local_hour,
                 snapshot->gps_local_minute);
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    gps_label_set(s_ui.fireblade_time,
                  s_ui.fireblade_time_buf,
                  sizeof(s_ui.fireblade_time_buf),
                  text);
    if (portrait_fireblade) {
        gps_label_set(s_ui.fireblade_date,
                      s_ui.fireblade_date_buf,
                      sizeof(s_ui.fireblade_date_buf),
                      date_text);
    }

    const bool controller_online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    char controller_temperature[8];
    char motor_temperature[8];
    if (controller_online && SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID)) {
        snprintf(controller_temperature,
                 sizeof(controller_temperature),
                 "%dC",
                 snapshot->controller_temp_c);
    } else {
        snprintf(controller_temperature, sizeof(controller_temperature), "--C");
    }
    if (controller_online && SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID)) {
        snprintf(motor_temperature,
                 sizeof(motor_temperature),
                 "%dC",
                 snapshot->motor_temp_c);
    } else {
        snprintf(motor_temperature, sizeof(motor_temperature), "--C");
    }
    if (portrait_fireblade) {
        snprintf(text, sizeof(text), "%s / %s", controller_temperature, motor_temperature);
        gps_label_set(s_ui.fireblade_controller_temp,
                      s_ui.fireblade_controller_temp_buf,
                      sizeof(s_ui.fireblade_controller_temp_buf),
                      text);
    } else {
        gps_label_set(s_ui.fireblade_controller_temp,
                      s_ui.fireblade_controller_temp_buf,
                      sizeof(s_ui.fireblade_controller_temp_buf),
                      controller_temperature);
        gps_label_set(s_ui.fireblade_motor_temp,
                      s_ui.fireblade_motor_temp_buf,
                      sizeof(s_ui.fireblade_motor_temp_buf),
                      motor_temperature);
    }

    if (SNAPSHOT_FLAG(snapshot, BMS_ONLINE) && SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        snprintf(text, sizeof(text), "%u%%", LV_MIN(snapshot->soc_percent, 100U));
    } else {
        snprintf(text, sizeof(text), "--%%");
    }
    gps_label_set(s_ui.fireblade_soc,
                  s_ui.fireblade_soc_buf,
                  sizeof(s_ui.fireblade_soc_buf),
                  text);

    if (snapshot->average_consumption_valid) {
        const long consumption = snapshot->average_consumption_deci_wh_per_distance;
        snprintf(text,
                 sizeof(text),
                 "%ld.%01ld",
                 consumption / 10L,
                 labs(consumption % 10L));
    } else {
        snprintf(text, sizeof(text), "--.-");
    }
    gps_label_set(s_ui.fireblade_consumption,
                  s_ui.fireblade_consumption_buf,
                  sizeof(s_ui.fireblade_consumption_buf),
                  text);
    gps_label_set(s_ui.fireblade_consumption_unit,
                  s_ui.fireblade_consumption_unit_buf,
                  sizeof(s_ui.fireblade_consumption_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "Wh/mi" : "Wh/km");

    snprintf(text,
             sizeof(text),
             snapshot->remaining_range_valid ? "%u" : "--",
             snapshot->remaining_range_km);
    gps_label_set(s_ui.fireblade_range,
                  s_ui.fireblade_range_buf,
                  sizeof(s_ui.fireblade_range_buf),
                  text);
    snprintf(text,
             sizeof(text),
             snapshot->average_speed_valid ? "%u" : "--",
             (snapshot->average_speed_deci_units + 5U) / 10U);
    gps_label_set(s_ui.fireblade_average_speed,
                  s_ui.fireblade_average_speed_buf,
                  sizeof(s_ui.fireblade_average_speed_buf),
                  text);
    gps_label_set(s_ui.fireblade_average_speed_unit,
                  s_ui.fireblade_average_speed_unit_buf,
                  sizeof(s_ui.fireblade_average_speed_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");

    if (!portrait_fireblade) {
        fireblade_format_date(text, sizeof(text), snapshot);
        gps_label_set(s_ui.fireblade_date,
                      s_ui.fireblade_date_buf,
                      sizeof(s_ui.fireblade_date_buf),
                      text);
    }

    gps_label_set(s_ui.fireblade_gear,
                  s_ui.fireblade_gear_buf,
                  sizeof(s_ui.fireblade_gear_buf),
                  controller_gear_text(snapshot->controller_gear,
                                       controller_online,
                                       SNAPSHOT_FLAG(snapshot, CONTROLLER_GEAR_VALID)));
    gps_label_set(s_ui.fireblade_gear_unit,
                  s_ui.fireblade_gear_unit_buf,
                  sizeof(s_ui.fireblade_gear_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");

    const bool speed_valid = SNAPSHOT_FLAG(snapshot, SPEED_VALID);
    snprintf(text,
             sizeof(text),
             speed_valid ? "%u" : "--",
             (snapshot->speed_deci_units + 5U) / 10U);
    gps_label_set(s_ui.fireblade_speed,
                  s_ui.fireblade_speed_buf,
                  sizeof(s_ui.fireblade_speed_buf),
                  text);
    gps_label_set(s_ui.fireblade_speed_unit,
                  s_ui.fireblade_speed_unit_buf,
                  sizeof(s_ui.fireblade_speed_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");

    const uint32_t maximum_speed = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                       ? 1200U
                                       : 1800U;
    const uint32_t speed = speed_valid
                               ? LV_MIN(snapshot->speed_deci_units, maximum_speed)
                               : 0U;
    const uint32_t needle_signature =
        speed | ((uint32_t)snapshot->speed_unit << 16) | (speed_valid ? UINT32_C(1) << 24 : 0U);
    if (!s_ui.fireblade_needle_signature_valid ||
        s_ui.fireblade_needle_signature != needle_signature) {
        s_ui.fireblade_needle_signature = needle_signature;
        s_ui.fireblade_needle_signature_valid = true;
        const int32_t angle =
            FIREBLADE_ARC_START_ANGLE +
            (int32_t)(speed * FIREBLADE_ARC_SWEEP_ANGLE / maximum_speed);
        const lv_point_t tip = fireblade_circle_point(s_ui.fireblade_needle_center,
                                                       s_ui.fireblade_needle_radius,
                                                       angle);
        const lv_point_t start = fireblade_circle_point(s_ui.fireblade_needle_center,
                                                         FIREBLADE_GEAR_RADIUS,
                                                         angle);
        fireblade_needle_line_set(s_ui.fireblade_needle_black,
                                  s_ui.fireblade_needle_black_points,
                                  start,
                                  tip);
        fireblade_needle_line_set(s_ui.fireblade_needle_red,
                                  s_ui.fireblade_needle_red_points,
                                  start,
                                  tip);
    }
}

void create_fireblade_dashboard(void)
{
    s_ui.fireblade_page = lv_obj_create(s_ui.gps_page);
    clear_style(s_ui.fireblade_page);
    lv_obj_set_pos(s_ui.fireblade_page, 0, 0);
    lv_obj_set_size(s_ui.fireblade_page, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(s_ui.fireblade_page, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.fireblade_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.fireblade_page, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.fireblade_page, COLOR_FIREBLADE_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_ui.fireblade_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.fireblade_page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_ui.fireblade_needle_signature_valid = false;
    const bool portrait = s_ui.width < s_ui.height;
    if (dashboard_native_landscape_enabled()) {
        fireblade_create_native_landscape(s_ui.fireblade_page);
    } else if (s_ui.width == 320 && s_ui.height == 480) {
        fireblade_create_native_portrait(s_ui.fireblade_page);
    } else {
        lv_obj_t *viewport = dashboard_viewport(s_ui.fireblade_page, portrait);
        if (portrait) {
        fireblade_create_portrait(viewport);
        } else {
        fireblade_create_landscape(viewport);
        }
    }
}
#endif

