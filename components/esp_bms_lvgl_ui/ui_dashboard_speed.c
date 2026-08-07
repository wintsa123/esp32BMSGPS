/*
 * UI 模块: speed
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

void gps_label_set(lv_obj_t *label_obj,
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

lv_point_t speed_dashboard_point(int32_t x, int32_t y)
{
    const lv_point_t point = { .x = x, .y = y };
    return point;
}

static void speed_dashboard_draw_line(lv_layer_t *layer,
                                      lv_point_t start,
                                      lv_point_t end,
                                      lv_color_t color,
                                      int32_t width,
                                      bool rounded)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1.x = start.x;
    line.p1.y = start.y;
    line.p2.x = end.x;
    line.p2.y = end.y;
    line.color = color;
    line.width = width;
    line.opa = LV_OPA_COVER;
    line.round_start = rounded;
    line.round_end = rounded;
    lv_draw_line(layer, &line);
}

void speed_dashboard_draw_triangle(lv_layer_t *layer,
                                          lv_point_t p0,
                                          lv_point_t p1,
                                          lv_point_t p2,
                                          lv_color_t color)
{
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.p[0].x = p0.x;
    triangle.p[0].y = p0.y;
    triangle.p[1].x = p1.x;
    triangle.p[1].y = p1.y;
    triangle.p[2].x = p2.x;
    triangle.p[2].y = p2.y;
    triangle.color = color;
    triangle.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &triangle);
}

static void speed_dashboard_draw_rect(lv_layer_t *layer,
                                      lv_area_t area,
                                      lv_color_t color,
                                      bool filled,
                                      int32_t radius)
{
    lv_draw_rect_dsc_t rectangle;
    lv_draw_rect_dsc_init(&rectangle);
    rectangle.radius = radius;
    rectangle.bg_color = color;
    rectangle.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    rectangle.border_color = color;
    rectangle.border_opa = LV_OPA_COVER;
    rectangle.border_width = filled ? 0 : 1;
    lv_draw_rect(layer, &rectangle, &area);
}

static uint32_t speed_dashboard_smooth_step(uint32_t index)
{
    const uint32_t position = index * 1024U / SPEED_DASHBOARD_SEGMENT_COUNT;
    return (uint32_t)(((uint64_t)position * position * (3072U - (2U * position))) /
                      UINT64_C(1048576));
}

static int32_t speed_dashboard_scaled_x(const lv_area_t *coords,
                                        bool portrait,
                                        int32_t coordinate)
{
    const int32_t base_width = portrait ? 240 : 320;
    return coords->x1 +
           (int32_t)((int64_t)coordinate * lv_area_get_width(coords) / base_width);
}

static int32_t speed_dashboard_scaled_y(const lv_area_t *coords,
                                        bool portrait,
                                        int32_t coordinate)
{
    const int32_t base_height = portrait ? 320 : 240;
    return coords->y1 +
           (int32_t)((int64_t)coordinate * lv_area_get_height(coords) / base_height);
}

static void speed_dashboard_geometry(bool portrait,
                                     const lv_area_t *coords,
                                     lv_point_t *outer,
                                     lv_point_t *inner)
{
    for (uint32_t index = 0U; index <= SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        const uint32_t smooth = speed_dashboard_smooth_step(index);
        if (portrait) {
            outer[index] = speed_dashboard_point(
                speed_dashboard_scaled_x(coords,
                                         true,
                                         28 + (int32_t)(180U * smooth / 1024U)),
                speed_dashboard_scaled_y(coords,
                                         true,
                                         292 - (int32_t)(228U * index /
                                                         SPEED_DASHBOARD_SEGMENT_COUNT)));
            inner[index] = speed_dashboard_point(
                speed_dashboard_scaled_x(coords,
                                         true,
                                         86 + (int32_t)(126U * smooth / 1024U)),
                speed_dashboard_scaled_y(coords,
                                         true,
                                         292 - (int32_t)(175U * index /
                                                         SPEED_DASHBOARD_SEGMENT_COUNT)));
        } else {
            outer[index] = speed_dashboard_point(
                speed_dashboard_scaled_x(coords,
                                         false,
                                         14 + (int32_t)(292U * index /
                                                        SPEED_DASHBOARD_SEGMENT_COUNT)),
                speed_dashboard_scaled_y(coords,
                                         false,
                                         185 - (int32_t)(88U * smooth / 1024U)));
            inner[index] = speed_dashboard_point(
                speed_dashboard_scaled_x(coords,
                                         false,
                                         14 + (int32_t)(286U * index /
                                                        SPEED_DASHBOARD_SEGMENT_COUNT)),
                speed_dashboard_scaled_y(coords,
                                         false,
                                         222 - (int32_t)(78U * smooth / 1024U)));
        }
    }
}

static lv_color_t speed_dashboard_segment_color(uint32_t index,
                                                uint32_t active_segments,
                                                bool speed_valid)
{
    if (index >= SPEED_DASHBOARD_DANGER_START) {
        return COLOR_SPEED_BAND_DANGER;
    }
    if (!speed_valid || index >= active_segments) {
        return COLOR_SPEED_BAND_IDLE;
    }
    const uint32_t denominator = active_segments > 1U ? active_segments - 1U : 1U;
    const uint8_t progress = (uint8_t)(index * 255U / denominator);
    if (progress < 176U) {
        return lv_color_mix(COLOR_SPEED_BAND_BLUE,
                            COLOR_SPEED_BAND_DARK,
                            (uint8_t)(progress * 255U / 176U));
    }
    return lv_color_mix(COLOR_WHITE,
                        COLOR_SPEED_BAND_BLUE,
                        (uint8_t)((progress - 176U) * 255U / 79U));
}

static bool speed_dashboard_static_landscape_enabled(const lv_area_t *coords)
{
#if defined(CONFIG_IDF_TARGET_ESP32) && ESP_BMS_FEATURE_DASHBOARD_S1000RR
    return coords && lv_area_get_width(coords) == (int32_t)SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH &&
           lv_area_get_height(coords) == (int32_t)SPEED_DASHBOARD_STATIC_LANDSCAPE_HEIGHT;
#else
    (void)coords;
    return false;
#endif
}

static bool speed_dashboard_s3_static_background_enabled(void)
{
#if ESP_BMS_LVGL_UI_SIMULATOR || defined(CONFIG_IDF_TARGET_ESP32S3)
    return s_ui.width == (int32_t)DASHBOARD_STATIC_CACHE_WIDTH &&
           s_ui.height == (int32_t)DASHBOARD_STATIC_CACHE_HEIGHT;
#else
    return false;
#endif
}

static bool speed_dashboard_static_background_enabled(const lv_area_t *coords)
{
    return speed_dashboard_static_landscape_enabled(coords) ||
           (s_ui.speed_static_background && coords &&
            lv_area_get_width(coords) == (int32_t)DASHBOARD_STATIC_CACHE_WIDTH &&
            lv_area_get_height(coords) == (int32_t)DASHBOARD_STATIC_CACHE_HEIGHT);
}

static void speed_dashboard_static_background_apply(void)
{
#if defined(CONFIG_IDF_TARGET_ESP32) && ESP_BMS_FEATURE_DASHBOARD_S1000RR
    if (!s_ui.speed_art) {
        return;
    }
    const bool enabled = s_ui.width == (int32_t)SPEED_DASHBOARD_STATIC_LANDSCAPE_WIDTH &&
                         s_ui.height == (int32_t)SPEED_DASHBOARD_STATIC_LANDSCAPE_HEIGHT;
    lv_obj_set_style_bg_image_src(s_ui.speed_art,
                                  enabled ? &SPEED_DASHBOARD_STATIC_LANDSCAPE : NULL,
                                  LV_PART_MAIN);
    lv_obj_set_style_bg_image_opa(s_ui.speed_art,
                                  enabled ? LV_OPA_COVER : LV_OPA_TRANSP,
                                  LV_PART_MAIN);
#endif
}

static uint32_t speed_dashboard_active_segments(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, SPEED_VALID)) {
        return 0U;
    }
    const uint32_t maximum = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? 1200U : 1800U;
    const uint32_t clamped_speed = snapshot->speed_deci_units > maximum
                                       ? maximum
                                       : snapshot->speed_deci_units;
    return (clamped_speed * SPEED_DASHBOARD_SEGMENT_COUNT + maximum - 1U) / maximum;
}

static uint32_t speed_dashboard_battery_active_segments(
    const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, BMS_ONLINE) || !SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        return 0U;
    }
    const uint32_t soc = snapshot->soc_percent > 100U ? 100U : snapshot->soc_percent;
    return (soc * 8U + 99U) / 100U;
}

static uint32_t speed_dashboard_render_signature(
    const esp_bms_dashboard_snapshot_t *snapshot)
{
    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
    uint32_t signature = speed_dashboard_active_segments(snapshot);
    signature |= SNAPSHOT_FLAG(snapshot, SPEED_VALID) ? UINT32_C(1) << 6 : 0U;
#if ESP_BMS_FEATURE_GPS
    signature |= SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID) ? UINT32_C(1) << 7 : 0U;
#endif
    signature |= bms_online ? UINT32_C(1) << 8 : 0U;
    signature |= bms_online && SNAPSHOT_FLAG(snapshot, SOC_VALID)
                     ? UINT32_C(1) << 9
                     : 0U;
    signature |= speed_dashboard_battery_active_segments(snapshot) << 10;
    const bool controller_online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    signature |= controller_online && SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID)
                     ? UINT32_C(1) << 14
                     : 0U;
    signature |= controller_online && SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID)
                     ? UINT32_C(1) << 15
                     : 0U;
    return signature;
}

static int32_t speed_dashboard_band_width(bool portrait,
                                          lv_point_t outer_start,
                                          lv_point_t inner_start,
                                          lv_point_t outer_end,
                                          lv_point_t inner_end)
{
    const int32_t start_width = portrait ? abs(inner_start.x - outer_start.x)
                                         : abs(inner_start.y - outer_start.y);
    const int32_t end_width = portrait ? abs(inner_end.x - outer_end.x)
                                       : abs(inner_end.y - outer_end.y);
    const int32_t width = (start_width + end_width + 1) / 2;
    return width > 2 ? width : 2;
}

static void speed_dashboard_overlap_band_endpoints(uint32_t index,
                                                   lv_point_t *start,
                                                   lv_point_t *end)
{
    const int32_t dx = end->x - start->x;
    const int32_t dy = end->y - start->y;
    const int32_t span = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (span == 0) {
        return;
    }
    if (index > 0U) {
        start->x -= (dx * SPEED_DASHBOARD_BAND_OVERLAP) / span;
        start->y -= (dy * SPEED_DASHBOARD_BAND_OVERLAP) / span;
    }
    if (index + 1U < SPEED_DASHBOARD_SEGMENT_COUNT) {
        end->x += (dx * SPEED_DASHBOARD_BAND_OVERLAP) / span;
        end->y += (dy * SPEED_DASHBOARD_BAND_OVERLAP) / span;
    }
}

static void speed_dashboard_draw_battery(lv_layer_t *layer,
                                         const lv_area_t *coords,
                                         bool portrait,
                                         const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!SNAPSHOT_FLAG(snapshot, BMS_ONLINE)) {
        return;
    }
    const int32_t x = speed_dashboard_scaled_x(coords, portrait, 8);
    const int32_t y = speed_dashboard_scaled_y(coords, portrait, portrait ? 6 : 8);
    const int32_t width = speed_dashboard_scaled_x(coords, portrait, 1) - coords->x1;
    const int32_t height = speed_dashboard_scaled_y(coords, portrait, 1) - coords->y1;
    const int32_t battery_width = width > 0 ? width : 1;
    const int32_t battery_height = height > 0 ? height : 1;
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x, y + battery_height, x + 6 * battery_width, y + 16 * battery_height },
                              COLOR_WHITE,
                              true,
                              1);
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x + 2 * battery_width, y, x + 4 * battery_width, y + battery_height },
                              COLOR_WHITE,
                              true,
                              0);

    if (!SNAPSHOT_FLAG(snapshot, SOC_VALID)) {
        return;
    }
    const uint32_t active = speed_dashboard_battery_active_segments(snapshot);
    const int32_t start_x = x + 11 * battery_width;
    const int32_t segment_y = y + battery_height;
    for (uint32_t index = 0U; index < active; ++index) {
        const int32_t left = start_x + (int32_t)(index * 4U * battery_width);
        const lv_point_t p0 = speed_dashboard_point(left + battery_width, segment_y);
        const lv_point_t p1 = speed_dashboard_point(left + 4 * battery_width, segment_y);
        const lv_point_t p2 = speed_dashboard_point(left + 3 * battery_width,
                                                     segment_y + 15 * battery_height);
        const lv_point_t p3 = speed_dashboard_point(left, segment_y + 15 * battery_height);
        speed_dashboard_draw_triangle(layer, p0, p1, p2, COLOR_WHITE);
        speed_dashboard_draw_triangle(layer, p0, p2, p3, COLOR_WHITE);
    }
}

#if ESP_BMS_FEATURE_GPS
static void speed_dashboard_draw_satellite(lv_layer_t *layer,
                                           const lv_area_t *coords,
                                           bool portrait,
                                           bool gps_fix_valid)
{
    const int32_t x = speed_dashboard_scaled_x(coords, portrait, portrait ? 35 : 145);
    const int32_t y = speed_dashboard_scaled_y(coords, portrait, portrait ? 29 : 8);
    const int32_t width = speed_dashboard_scaled_x(coords, portrait, 1) - coords->x1;
    const int32_t height = speed_dashboard_scaled_y(coords, portrait, 1) - coords->y1;
    const int32_t icon_width = width > 0 ? width : 1;
    const int32_t icon_height = height > 0 ? height : 1;
    const lv_point_t top = speed_dashboard_point(x + 7 * icon_width, y + 2 * icon_height);
    const lv_point_t right = speed_dashboard_point(x + 12 * icon_width, y + 7 * icon_height);
    const lv_point_t bottom = speed_dashboard_point(x + 7 * icon_width, y + 12 * icon_height);
    const lv_point_t left = speed_dashboard_point(x + 2 * icon_width, y + 7 * icon_height);
    speed_dashboard_draw_triangle(layer, top, right, bottom, COLOR_WHITE);
    speed_dashboard_draw_triangle(layer, top, bottom, left, COLOR_WHITE);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + icon_width, y + icon_height),
                              speed_dashboard_point(x + 5 * icon_width, y + 5 * icon_height),
                              COLOR_WHITE,
                              3,
                              false);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + 9 * icon_width, y + 9 * icon_height),
                              speed_dashboard_point(x + 14 * icon_width, y + 14 * icon_height),
                              COLOR_WHITE,
                              3,
                              false);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(x + 7 * icon_width, y + 12 * icon_height),
                              speed_dashboard_point(x + 3 * icon_width, y + 16 * icon_height),
                              COLOR_WHITE,
                              1,
                              false);
    speed_dashboard_draw_rect(layer,
                              (lv_area_t){ x + 15 * icon_width, y + icon_height,
                                           x + 20 * icon_width, y + 6 * icon_height },
                              gps_fix_valid ? COLOR_SPEED_GPS_OK : COLOR_WARN,
                              true,
                              LV_RADIUS_CIRCLE);
}
#endif

static void speed_dashboard_draw_static_gauge(lv_layer_t *layer,
                                              const lv_area_t *coords,
                                              bool portrait,
                                              const lv_point_t *outer,
                                              const lv_point_t *inner)
{
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        lv_point_t start = speed_dashboard_point(
            (outer[index].x + inner[index].x) / 2,
            (outer[index].y + inner[index].y) / 2);
        lv_point_t end = speed_dashboard_point(
            (outer[index + 1U].x + inner[index + 1U].x) / 2,
            (outer[index + 1U].y + inner[index + 1U].y) / 2);
        speed_dashboard_overlap_band_endpoints(index, &start, &end);
        speed_dashboard_draw_line(layer,
                                  start,
                                  end,
                                  speed_dashboard_segment_color(index, 0U, false),
                                  speed_dashboard_band_width(portrait,
                                                             outer[index],
                                                             inner[index],
                                                             outer[index + 1U],
                                                             inner[index + 1U]),
                                  false);
    }

    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SEGMENT_COUNT; ++index) {
        const int32_t border_width = index >= SPEED_DASHBOARD_DANGER_START ? 2 : 4;
        speed_dashboard_draw_line(layer,
                                  outer[index],
                                  outer[index + 1U],
                                  index >= SPEED_DASHBOARD_DANGER_START
                                      ? COLOR_SPEED_BAND_DANGER
                                      : COLOR_WHITE,
                                  border_width,
                                  false);
    }

    for (uint32_t index = 0U; index <= SPEED_DASHBOARD_SEGMENT_COUNT;
         index += SPEED_DASHBOARD_MINOR_TICK_STEP) {
        const bool major = index % SPEED_DASHBOARD_MAJOR_TICK_STEP == 0U ||
                           index == SPEED_DASHBOARD_SEGMENT_COUNT;
        const int32_t numerator = major ? 38 : 22;
        const lv_point_t tick_end = speed_dashboard_point(
            outer[index].x + ((inner[index].x - outer[index].x) * numerator / 100),
            outer[index].y + ((inner[index].y - outer[index].y) * numerator / 100));
        speed_dashboard_draw_line(layer,
                                  outer[index],
                                  tick_end,
                                  index >= SPEED_DASHBOARD_DANGER_START
                                      ? COLOR_SPEED_BAND_DANGER
                                      : COLOR_WHITE,
                                  major ? 2 : 1,
                                  false);
    }

    if (!portrait) {
        const uint32_t last = SPEED_DASHBOARD_SEGMENT_COUNT;
        const lv_area_t terminal = {
            .x1 = inner[last].x,
            .y1 = outer[last].y,
            .x2 = outer[last].x,
            .y2 = inner[last].y,
        };
        speed_dashboard_draw_rect(layer, terminal, COLOR_SPEED_BAND_DANGER, true, 0);
        speed_dashboard_draw_line(layer,
                                  speed_dashboard_point(outer[last].x, outer[last].y),
                                  speed_dashboard_point(outer[last].x, inner[last].y),
                                  COLOR_SPEED_BAND_DANGER,
                                  2,
                                  false);
    }

    const int32_t divider_y = speed_dashboard_scaled_y(coords, portrait, portrait ? 47 : 31);
    speed_dashboard_draw_line(layer,
                              speed_dashboard_point(speed_dashboard_scaled_x(coords, portrait, 8),
                                                    divider_y),
                              speed_dashboard_point(speed_dashboard_scaled_x(coords,
                                                                            portrait,
                                                                            portrait ? 232 : 312),
                                                    divider_y),
                              COLOR_SPEED_DIVIDER,
                              1,
                              false);
}

static void speed_dashboard_static_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    if (!speed_dashboard_s3_static_background_enabled()) {
        return;
    }

    const bool portrait = lv_area_get_width(&coords) < lv_area_get_height(&coords);
    lv_point_t outer[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    lv_point_t inner[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    speed_dashboard_geometry(portrait, &coords, outer, inner);
    speed_dashboard_draw_static_gauge(layer, &coords, portrait, outer, inner);
}

static void speed_dashboard_draw_event_cb(lv_event_t *event)
{
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
    const int64_t draw_started_us = esp_timer_get_time();
#endif
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const bool portrait = lv_area_get_width(&coords) < lv_area_get_height(&coords);
    const esp_bms_dashboard_snapshot_t *snapshot = &s_ui.last_snapshot;
    const bool speed_valid = SNAPSHOT_FLAG(snapshot, SPEED_VALID);
    const uint32_t active_segments = speed_dashboard_active_segments(snapshot);
    const bool static_background = speed_dashboard_static_background_enabled(&coords);

    lv_point_t outer[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    lv_point_t inner[SPEED_DASHBOARD_SEGMENT_COUNT + 1U];
    speed_dashboard_geometry(portrait, &coords, outer, inner);
    const uint32_t segment_count = static_background
                                       ? (speed_valid
                                              ? (active_segments < SPEED_DASHBOARD_DANGER_START
                                                     ? active_segments
                                                     : SPEED_DASHBOARD_DANGER_START)
                                              : 0U)
                                       : SPEED_DASHBOARD_SEGMENT_COUNT;
    for (uint32_t index = 0U; index < segment_count; ++index) {
        const lv_color_t color = speed_dashboard_segment_color(index,
                                                                active_segments,
                                                                speed_valid);
        lv_point_t start = speed_dashboard_point(
            (outer[index].x + inner[index].x) / 2,
            (outer[index].y + inner[index].y) / 2);
        lv_point_t end = speed_dashboard_point(
            (outer[index + 1U].x + inner[index + 1U].x) / 2,
            (outer[index + 1U].y + inner[index + 1U].y) / 2);
        speed_dashboard_overlap_band_endpoints(index, &start, &end);
        speed_dashboard_draw_line(layer,
                                  start,
                                  end,
                                  color,
                                  speed_dashboard_band_width(portrait,
                                                             outer[index],
                                                             inner[index],
                                                             outer[index + 1U],
                                                             inner[index + 1U]),
                                  false);
    }
    if (!static_background) {
        speed_dashboard_draw_static_gauge(layer, &coords, portrait, outer, inner);
    }
    const bool compact = lv_area_get_width(&coords) < 180 ||
                         lv_area_get_height(&coords) < 180;
    if (!compact) {
        speed_dashboard_draw_battery(layer, &coords, portrait, snapshot);
#if ESP_BMS_FEATURE_GPS
        speed_dashboard_draw_satellite(layer,
                                       &coords,
                                       portrait,
                                       SNAPSHOT_FLAG(snapshot, GPS_FIX_VALID));
#endif
    }
    const bool controller_online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    const bool controller_temp_visible = controller_online &&
                                         SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID);
    const bool motor_temp_visible = controller_online &&
                                    SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID);
    lv_draw_label_dsc_t prefix;
    lv_draw_label_dsc_init(&prefix);
    prefix.font = &settings_zh_13;
    prefix.color = COLOR_TEXT;
    prefix.opa = LV_OPA_COVER;
    prefix.text_static = 1;
    if (!compact && controller_temp_visible) {
        prefix.text = "控";
        const lv_area_t area = {
            .x1 = speed_dashboard_scaled_x(&coords, portrait, portrait ? 96 : 188),
            .y1 = speed_dashboard_scaled_y(&coords, portrait, portrait ? 32 : 11),
            .x2 = speed_dashboard_scaled_x(&coords, portrait, portrait ? 111 : 203),
            .y2 = speed_dashboard_scaled_y(&coords, portrait, portrait ? 46 : 25),
        };
        lv_draw_label(layer, &prefix, &area);
    }
    if (!compact && motor_temp_visible) {
        prefix.text = "电机";
        const lv_area_t area = {
            .x1 = speed_dashboard_scaled_x(&coords, portrait, portrait ? 170 : 250),
            .y1 = speed_dashboard_scaled_y(&coords, portrait, portrait ? 32 : 11),
            .x2 = speed_dashboard_scaled_x(&coords, portrait, portrait ? 201 : 281),
            .y2 = speed_dashboard_scaled_y(&coords, portrait, portrait ? 46 : 25),
        };
        lv_draw_label(layer, &prefix, &area);
    }
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
    const int64_t elapsed_us = esp_timer_get_time() - draw_started_us;
    const uint32_t bounded_elapsed_us = elapsed_us > UINT32_MAX
                                            ? UINT32_MAX
                                            : (uint32_t)elapsed_us;
    s_ui.speed_art_draw_count++;
    s_ui.speed_art_draw_elapsed_us += bounded_elapsed_us;
    if (bounded_elapsed_us > s_ui.speed_art_draw_max_us) {
        s_ui.speed_art_draw_max_us = bounded_elapsed_us;
    }
#endif
}

static void speed_dashboard_apply_layout(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t base_width = portrait ? 240 : 320;
    const int32_t base_height = portrait ? 320 : 240;
    const bool compact = s_ui.width < 180 || s_ui.height < 180;
#define SPEED_X(value) ((int32_t)((int64_t)(value) * s_ui.width / base_width))
#define SPEED_Y(value) ((int32_t)((int64_t)(value) * s_ui.height / base_height))
    const int32_t status_y = SPEED_Y(portrait ? 5 : 7);
    const int32_t status_height = SPEED_Y(20);
    const int32_t consumption_y = status_y + 1;
    const int32_t temperature_y = SPEED_Y(portrait ? 30 : 9);
    lv_obj_set_pos(s_ui.speed_art, 0, 0);
    lv_obj_set_size(s_ui.speed_art, s_ui.width, s_ui.height);
    speed_dashboard_static_background_apply();
    if (portrait) {
        lv_obj_set_pos(s_ui.speed, SPEED_X(14), SPEED_Y(58));
        lv_obj_set_size(s_ui.speed, SPEED_X(104), SPEED_Y(52));
        lv_obj_set_pos(s_ui.gps_speed_unit, SPEED_X(20), SPEED_Y(105));
        lv_obj_set_size(s_ui.gps_speed_unit, SPEED_X(76), SPEED_Y(26));
        lv_obj_set_pos(s_ui.speed_soc, SPEED_X(90), status_y);
        lv_obj_set_size(s_ui.speed_soc, SPEED_X(30), status_height);
        lv_obj_set_pos(s_ui.speed_consumption, SPEED_X(58), consumption_y);
        lv_obj_set_size(s_ui.speed_consumption, SPEED_X(174), status_height);
        lv_obj_set_pos(s_ui.speed_controller_temp, SPEED_X(98), temperature_y);
        lv_obj_set_size(s_ui.speed_controller_temp, SPEED_X(44), status_height);
        lv_obj_set_pos(s_ui.speed_motor_temp, SPEED_X(184), temperature_y);
        lv_obj_set_size(s_ui.speed_motor_temp, SPEED_X(48), status_height);
        lv_obj_set_pos(s_ui.speed_gear, SPEED_X(167), SPEED_Y(230));
        lv_obj_set_size(s_ui.speed_gear, SPEED_X(40), SPEED_Y(44));
        lv_obj_set_pos(s_ui.gps_detail, SPEED_X(112), SPEED_Y(278));
        lv_obj_set_size(s_ui.gps_detail, SPEED_X(120), SPEED_Y(34));
        static const int16_t positions[SPEED_DASHBOARD_SCALE_LABEL_COUNT][2] = {
            { 16, 264 }, { 56, 218 }, { 101, 160 }, { 139, 111 }, { 178, 67 }, { 207, 51 },
        };
        for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
            lv_obj_set_pos(s_ui.speed_scale_labels[index], SPEED_X(positions[index][0]), SPEED_Y(positions[index][1]));
            lv_obj_set_size(s_ui.speed_scale_labels[index], SPEED_X(34), SPEED_Y(18));
        }
    } else {
        lv_obj_set_pos(s_ui.speed, 0, SPEED_Y(56));
        lv_obj_set_size(s_ui.speed, SPEED_X(94), SPEED_Y(52));
        lv_obj_set_pos(s_ui.gps_speed_unit, SPEED_X(98), SPEED_Y(78));
        lv_obj_set_size(s_ui.gps_speed_unit, SPEED_X(68), SPEED_Y(26));
        lv_obj_set_pos(s_ui.speed_soc, SPEED_X(90), status_y);
        lv_obj_set_size(s_ui.speed_soc, SPEED_X(30), status_height);
        lv_obj_set_pos(s_ui.speed_consumption, SPEED_X(54), consumption_y);
        lv_obj_set_size(s_ui.speed_consumption, SPEED_X(86), status_height);
        lv_obj_set_pos(s_ui.speed_controller_temp, SPEED_X(190), temperature_y);
        lv_obj_set_size(s_ui.speed_controller_temp, SPEED_X(44), status_height);
        lv_obj_set_pos(s_ui.speed_motor_temp, SPEED_X(270), temperature_y);
        lv_obj_set_size(s_ui.speed_motor_temp, SPEED_X(42), status_height);
        lv_obj_set_pos(s_ui.speed_gear, SPEED_X(269), SPEED_Y(153));
        lv_obj_set_size(s_ui.speed_gear, SPEED_X(38), SPEED_Y(40));
        lv_obj_set_pos(s_ui.gps_detail, SPEED_X(196), SPEED_Y(195));
        lv_obj_set_size(s_ui.gps_detail, SPEED_X(120), SPEED_Y(34));
        static const int16_t positions[SPEED_DASHBOARD_SCALE_LABEL_COUNT][2] = {
            { 8, 168 }, { 53, 148 }, { 111, 124 }, { 174, 102 }, { 244, 84 }, { 286, 80 },
        };
        for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
            lv_obj_set_pos(s_ui.speed_scale_labels[index], SPEED_X(positions[index][0]), SPEED_Y(positions[index][1]));
            lv_obj_set_size(s_ui.speed_scale_labels[index], SPEED_X(34), SPEED_Y(18));
        }
    }
    lv_obj_set_style_text_font(s_ui.speed,
                               compact ? &lv_font_montserrat_28 : &lv_font_montserrat_48,
                               LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.gps_speed_unit,
                               compact ? &lv_font_montserrat_14 : &lv_font_montserrat_24,
                               LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.speed_soc, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.speed_consumption, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.speed_controller_temp, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.speed_motor_temp, &settings_zh_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.speed_controller_temp, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.speed_motor_temp, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    const int32_t gear_height = SPEED_Y(portrait ? 44 : 40);
    const int32_t gear_font_height = compact ? (int32_t)lv_font_montserrat_24.line_height :
                                               (int32_t)lv_font_montserrat_28.line_height;
    const int32_t gear_padding = (gear_height - gear_font_height) / 2;
    lv_obj_set_style_text_font(s_ui.speed_gear,
                               compact ? &lv_font_montserrat_24 : &lv_font_montserrat_28,
                               LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.speed_gear, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_ui.speed_gear, gear_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ui.speed_gear, gear_padding, LV_PART_MAIN);
#undef SPEED_X
#undef SPEED_Y
}

void set_gps_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot || !dashboard_page_content_ready(ESP_BMS_LVGL_PAGE_GPS)) {
        return;
    }

    char text[32];
    const bool compact = s_ui.width < 180 || s_ui.height < 180;
    const bool bms_online = SNAPSHOT_FLAG(snapshot, BMS_ONLINE);
    if (SNAPSHOT_FLAG(snapshot, SPEED_VALID)) {
        snprintf(text,
                 sizeof(text),
                 "%u",
                 (snapshot->speed_deci_units + 5U) / 10U);
    } else {
        snprintf(text, sizeof(text), "-");
    }
    gps_label_set(s_ui.speed, s_ui.gps_speed_buf, sizeof(s_ui.gps_speed_buf), text);
    gps_label_set(s_ui.gps_speed_unit,
                  s_ui.gps_speed_unit_buf,
                  sizeof(s_ui.gps_speed_unit_buf),
                  snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h");

    set_obj_hidden(s_ui.speed_soc, true);

    const char *consumption_unit = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                       ? "Wh/mi"
                                       : "Wh/km";
    if (snapshot->average_consumption_valid) {
        const int32_t deci = snapshot->average_consumption_deci_wh_per_distance;
        const int32_t rounded = deci >= 0 ? (deci + 5) / 10 : (deci - 5) / 10;
        snprintf(text, sizeof(text), "%ld%s", (long)rounded, consumption_unit);
    } else {
        snprintf(text, sizeof(text), "--%s", consumption_unit);
    }
    gps_label_set(s_ui.speed_consumption,
                  s_ui.speed_consumption_buf,
                  sizeof(s_ui.speed_consumption_buf),
                  text);
    set_obj_hidden(s_ui.speed_consumption, compact || !bms_online);

    const bool controller_online = SNAPSHOT_FLAG(snapshot, CONTROLLER_ONLINE);
    const bool controller_temp_visible = controller_online &&
                                         SNAPSHOT_FLAG(snapshot, CONTROLLER_TEMP_VALID);
    const bool motor_temp_visible = controller_online &&
                                    SNAPSHOT_FLAG(snapshot, MOTOR_TEMP_VALID);
    if (controller_temp_visible) {
        snprintf(text, sizeof(text), "%dC", snapshot->controller_temp_c);
        gps_label_set(s_ui.speed_controller_temp,
                      s_ui.speed_controller_temp_buf,
                      sizeof(s_ui.speed_controller_temp_buf),
                      text);
    }
    if (motor_temp_visible) {
        snprintf(text, sizeof(text), "%dC", snapshot->motor_temp_c);
        gps_label_set(s_ui.speed_motor_temp,
                      s_ui.speed_motor_temp_buf,
                      sizeof(s_ui.speed_motor_temp_buf),
                      text);
    }
    gps_label_set(s_ui.speed_gear,
                  s_ui.speed_gear_buf,
                  sizeof(s_ui.speed_gear_buf),
                  controller_gear_text(snapshot->controller_gear,
                                       controller_online,
                                       SNAPSHOT_FLAG(snapshot, CONTROLLER_GEAR_VALID)));
    set_obj_hidden(s_ui.speed_controller_temp, compact || !controller_temp_visible);
    set_obj_hidden(s_ui.speed_motor_temp, compact || !motor_temp_visible);
    set_obj_hidden(s_ui.speed_gear, false);

    if (snapshot->gps_local_time_valid) {
        snprintf(text, sizeof(text), "%02u:%02u",
                 snapshot->gps_local_hour,
                 snapshot->gps_local_minute);
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    gps_label_set(s_ui.gps_detail, s_ui.gps_uptime_buf, sizeof(s_ui.gps_uptime_buf), text);

    static const uint16_t metric_scale[SPEED_DASHBOARD_SCALE_LABEL_COUNT] = {
        0U, 40U, 80U, 120U, 160U, 180U,
    };
    static const uint16_t imperial_scale[SPEED_DASHBOARD_SCALE_LABEL_COUNT] = {
        0U, 30U, 60U, 90U, 110U, 120U,
    };
    const uint16_t *scale = snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                ? imperial_scale
                                : metric_scale;
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
        snprintf(text, sizeof(text), "%u", scale[index]);
        gps_label_set(s_ui.speed_scale_labels[index],
                      s_ui.speed_scale_buf[index],
                      sizeof(s_ui.speed_scale_buf[index]),
                      text);
        set_obj_hidden(s_ui.speed_scale_labels[index], compact);
    }
    const esp_bms_speed_dashboard_style_t style =
        speed_dashboard_style_from_snapshot(snapshot);
    if (style == ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE) {
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
        set_fireblade_dashboard(snapshot);
        return;
#endif
    }
    const uint32_t render_signature = speed_dashboard_render_signature(snapshot);
    if (s_ui.speed_art &&
        (!s_ui.speed_art_signature_valid || s_ui.speed_art_signature != render_signature)) {
        s_ui.speed_art_signature = render_signature;
        s_ui.speed_art_signature_valid = true;
        lv_obj_invalidate(s_ui.speed_art);
    }
}

void create_gps_dashboard(void)
{
    if (speed_dashboard_s3_static_background_enabled()) {
        lv_obj_t *static_layer = lv_obj_create(s_ui.gps_page);
        clear_style(static_layer);
        lv_obj_set_pos(static_layer, 0, 0);
        lv_obj_set_size(static_layer, s_ui.width, s_ui.height);
        lv_obj_set_style_bg_color(static_layer, COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(static_layer, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(static_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(static_layer,
                            speed_dashboard_static_draw_event_cb,
                            LV_EVENT_DRAW_MAIN,
                            NULL);
        s_ui.speed_static_background = static_layer;
        if (dashboard_static_cache_finalize(&s_ui.speed_static_cache,
                                            s_ui.gps_page,
                                            static_layer,
                                            "s1000rr")) {
            s_ui.speed_static_background = s_ui.speed_static_cache.image;
        }
    }

    s_ui.speed_art = lv_obj_create(s_ui.gps_page);
    clear_style(s_ui.speed_art);
    lv_obj_clear_flag(s_ui.speed_art, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.speed_art,
                        speed_dashboard_draw_event_cb,
                        LV_EVENT_DRAW_MAIN,
                        NULL);

    s_ui.speed = controller_dashboard_label(s_ui.gps_page,
                                            s_ui.gps_speed_buf,
                                            0,
                                            0,
                                            1,
                                            lv_font_montserrat_48.line_height,
                                            &lv_font_montserrat_48,
                                            COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.speed, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_ui.gps_speed_unit = controller_dashboard_label(s_ui.gps_page,
                                                     s_ui.gps_speed_unit_buf,
                                                     0,
                                                     0,
                                                     1,
                                                     lv_font_montserrat_24.line_height,
                                                     &lv_font_montserrat_24,
                                                     COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.gps_speed_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    s_ui.speed_soc = controller_dashboard_label(s_ui.gps_page,
                                                s_ui.speed_soc_buf,
                                                0, 0, 1,
                                                settings_zh_10.line_height,
                                                &settings_zh_10,
                                                COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.speed_soc, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    set_obj_hidden(s_ui.speed_soc, true);
    s_ui.speed_consumption = controller_dashboard_label(s_ui.gps_page,
                                                        s_ui.speed_consumption_buf,
                                                        0, 0, 1,
                                                        settings_zh_10.line_height,
                                                        &settings_zh_10,
                                                        COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_consumption, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_consumption, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    set_obj_hidden(s_ui.speed_consumption, true);
    s_ui.speed_controller_temp = controller_dashboard_label(s_ui.gps_page,
                                                            s_ui.speed_controller_temp_buf,
                                                            0, 0, 1,
                                                            settings_zh_10.line_height,
                                                            &settings_zh_10,
                                                            COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_controller_temp, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_controller_temp, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.speed_motor_temp = controller_dashboard_label(s_ui.gps_page,
                                                       s_ui.speed_motor_temp_buf,
                                                       0, 0, 1,
                                                       settings_zh_10.line_height,
                                                       &settings_zh_10,
                                                       COLOR_TEXT);
    lv_label_set_long_mode(s_ui.speed_motor_temp, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_ui.speed_motor_temp, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_ui.speed_gear = controller_dashboard_label(s_ui.gps_page,
                                                 s_ui.speed_gear_buf,
                                                 0, 0, 1,
                                                 lv_font_montserrat_28.line_height,
                                                 &lv_font_montserrat_28,
                                                 COLOR_SPEED_BAND_BLUE);
    lv_obj_set_style_border_width(s_ui.speed_gear, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.speed_gear, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.speed_gear, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.speed_gear, COLOR_DASHBOARD_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.speed_gear, LV_OPA_70, LV_PART_MAIN);

    s_ui.gps_detail = controller_dashboard_label(s_ui.gps_page,
                                                 s_ui.gps_uptime_buf,
                                                 0, 0, 1,
                                                 lv_font_montserrat_24.line_height,
                                                 &lv_font_montserrat_24,
                                                 COLOR_TEXT);
    lv_obj_set_style_text_align(s_ui.gps_detail, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    for (uint32_t index = 0U; index < SPEED_DASHBOARD_SCALE_LABEL_COUNT; ++index) {
        s_ui.speed_scale_labels[index] = controller_dashboard_label(
            s_ui.gps_page,
            s_ui.speed_scale_buf[index],
            0, 0, 1,
            lv_font_montserrat_14.line_height,
            &lv_font_montserrat_14,
            index + 1U == SPEED_DASHBOARD_SCALE_LABEL_COUNT
                ? COLOR_SPEED_BAND_DANGER
                : COLOR_TEXT);
    }
    speed_dashboard_apply_layout();
}


void speed_page_sync(const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!snapshot || !s_ui.gps_page) {
        return;
    }

    const bool renderable = ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER;
    const bool changed = s_ui.speed_page_renderable != renderable;
    esp_bms_lvgl_page_t retained_page = s_ui.page;

    s_ui.speed_page_renderable = renderable;
    if (!renderable &&
        (retained_page == ESP_BMS_LVGL_PAGE_CONTROLLER ||
         retained_page == ESP_BMS_LVGL_PAGE_GPS)) {
        retained_page = ESP_BMS_LVGL_PAGE_BATTERY;
    }

    if (changed && s_ui.pages) {
        lv_obj_set_x(s_ui.gps_page, page_target_scroll_x(ESP_BMS_LVGL_PAGE_GPS));
        if (s_ui.cast_page) {
            lv_obj_set_x(s_ui.cast_page, page_target_scroll_x(ESP_BMS_LVGL_PAGE_CAST));
        }
        if (s_ui.music_page) {
            lv_obj_set_x(s_ui.music_page, page_target_scroll_x(ESP_BMS_LVGL_PAGE_MUSIC));
        }
        if (s_ui.page_transition_gps) {
            lv_obj_set_x(s_ui.page_transition_gps,
                         page_target_scroll_x(ESP_BMS_LVGL_PAGE_GPS));
        }
        if (s_ui.page_transition_cast) {
            lv_obj_set_x(s_ui.page_transition_cast,
                         page_target_scroll_x(ESP_BMS_LVGL_PAGE_CAST));
        }
        if (s_ui.page_transition_music) {
            lv_obj_set_x(s_ui.page_transition_music,
                         page_target_scroll_x(ESP_BMS_LVGL_PAGE_MUSIC));
        }
        move_to_page(retained_page, false);
        lv_obj_invalidate(s_ui.pages);
    }
}

