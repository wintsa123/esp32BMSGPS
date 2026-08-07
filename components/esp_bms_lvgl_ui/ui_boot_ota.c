/*
 * UI 模块: boot_ota
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

bool boot_animation_style_is_available(uint8_t style)
{
    if (style == (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE ||
        style == (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR) {
        return true;
    }
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    return style == (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE;
#else
    return false;
#endif
}

bool boot_animation_style_is_gauge(uint8_t style)
{
    return style == (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR ||
           style == (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE;
}

static lv_obj_t *boot_line(lv_obj_t *parent,
                           int32_t x,
                           int32_t y,
                           int32_t w,
                           int32_t h,
                           lv_color_t color,
                           lv_opa_t opacity)
{
    lv_obj_t *line = lv_obj_create(parent);
    clear_style(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_set_style_bg_color(line, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, opacity, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

static void boot_gauge_brand_spin_anim_cb(void *obj, int32_t angle)
{
    (void)obj;
    const int32_t phase = angle % 3600;
    const int32_t half_turn = phase % 1800;
    const int32_t edge = half_turn <= 900 ? half_turn : 1800 - half_turn;
    const int32_t scale_x = 256 - ((edge * 196) / 900);
    const int32_t scale_y = 256 - ((edge * 16) / 900);
    const int32_t perspective = (phase < 1800 ? 1 : -1) * ((edge * 16) / 900);
    const int32_t center_x = s_ui.width / 2;
    const int32_t center_y = s_ui.height / 2;

    for (uint32_t index = 0U; index < BOOT_BRAND_PART_COUNT; ++index) {
        const boot_brand_part_t *part = &s_ui.boot_brand_parts[index];
        if (!part->obj) {
            continue;
        }
        const int32_t width = (part->width * scale_x) / 256;
        const int32_t height = (part->height * scale_y) / 256;
        const int32_t part_center_x = part->x + part->width / 2;
        const int32_t part_center_y = part->y + part->height / 2;
        const int32_t x = center_x + ((part_center_x - center_x) * scale_x) / 256 - width / 2 +
                          ((part_center_y - center_y) * perspective) / 64;
        const int32_t y = center_y + ((part_center_y - center_y) * scale_y) / 256 - height / 2;
        lv_obj_set_pos(part->obj, x, y);
        lv_obj_set_size(part->obj, width > 0 ? width : 1, height > 0 ? height : 1);
    }
}

static void boot_gauge_brand_set_hidden(bool hidden)
{
    for (uint32_t index = 0U; index < BOOT_BRAND_PART_COUNT; ++index) {
        set_obj_hidden(s_ui.boot_brand_parts[index].obj, hidden);
    }
}

static void boot_gauge_brand_hide(void)
{
    if (!s_ui.boot_brand_mark) {
        return;
    }
    lv_anim_delete(s_ui.boot_brand_mark, boot_gauge_brand_spin_anim_cb);
    boot_gauge_brand_spin_anim_cb(s_ui.boot_brand_mark, 0);
    boot_gauge_brand_set_hidden(true);
}

static void boot_overlay_delete(void)
{
    boot_gauge_brand_hide();
    if (s_ui.boot_overlay) {
        lv_obj_delete(s_ui.boot_overlay);
    }
    s_ui.boot_overlay = NULL;
    s_ui.boot_status = NULL;
    s_ui.boot_progress = NULL;
    s_ui.boot_scan_line = NULL;
    s_ui.boot_brand_mark = NULL;
    s_ui.boot_rr_mark = NULL;
    memset(s_ui.boot_brand_parts, 0, sizeof(s_ui.boot_brand_parts));
    memset(s_ui.boot_charge_segments, 0, sizeof(s_ui.boot_charge_segments));
}

static lv_obj_t *boot_overlay_create(bool opaque)
{
    boot_overlay_delete();
    lv_obj_t *overlay = lv_obj_create(s_ui.root);
    clear_style(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(overlay, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, opaque ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(overlay);
    s_ui.boot_overlay = overlay;
    return overlay;
}

static void boot_corner_marks(lv_obj_t *parent)
{
    const int32_t margin = 10;
    const int32_t length = 22;
    const int32_t bottom = s_ui.height - margin - 2;
    const int32_t right = s_ui.width - margin - 2;

    (void)boot_line(parent, margin, margin, length, 2, COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, margin, margin, 2, length, COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, right - length, margin, length, 2,
                    COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, right, margin, 2, length, COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, margin, bottom, length, 2, COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, margin, bottom - length, 2, length,
                    COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, right - length, bottom, length, 2,
                    COLOR_BOOT_CYAN, LV_OPA_COVER);
    (void)boot_line(parent, right, bottom - length, 2, length,
                    COLOR_BOOT_CYAN, LV_OPA_COVER);
}

static void boot_charge_create(void)
{
    const bool portrait = s_ui.width < s_ui.height;
    lv_obj_t *overlay = boot_overlay_create(true);

    for (int32_t x = 32; x < s_ui.width; x += 32) {
        (void)boot_line(overlay, x, 28, 1, s_ui.height - 56,
                        COLOR_BOOT_GRID, LV_OPA_COVER);
    }
    for (int32_t y = 40; y < s_ui.height - 24; y += 28) {
        (void)boot_line(overlay, 16, y, s_ui.width - 32, 1,
                        COLOR_BOOT_GRID, LV_OPA_COVER);
    }
    boot_corner_marks(overlay);

    lv_obj_t *system_title = label(overlay,
                                   18,
                                   12,
                                   s_ui.width - 36,
                                   18,
                                   &lv_font_montserrat_14);
    lv_label_set_text(system_title, "POWER CORE // BOOT");
    lv_obj_set_style_text_color(system_title, COLOR_BOOT_CYAN, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(system_title, 1, LV_PART_MAIN);

    const int32_t frame_w = portrait ? s_ui.width - 56 : s_ui.width - 104;
    const int32_t frame_h = portrait ? 64 : 58;
    const int32_t frame_x = (s_ui.width - frame_w) / 2 - 3;
    const int32_t frame_y = portrait ? 108 : 72;
    lv_obj_t *battery = lv_obj_create(overlay);
    clear_style(battery);
    lv_obj_set_pos(battery, frame_x, frame_y);
    lv_obj_set_size(battery, frame_w, frame_h);
    lv_obj_set_style_bg_color(battery, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(battery, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(battery, COLOR_BOOT_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_opa(battery, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(battery, 4, LV_PART_MAIN);
    lv_obj_clear_flag(battery, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    (void)boot_line(overlay,
                    frame_x + frame_w + 4,
                    frame_y + (frame_h - 20) / 2,
                    7,
                    20,
                    COLOR_BOOT_CYAN,
                    LV_OPA_COVER);

    const int32_t gap = 3;
    const int32_t inner_w = frame_w - 16;
    const int32_t segment_w =
        (inner_w - ((int32_t)BOOT_CHARGE_SEGMENT_COUNT - 1) * gap) /
        (int32_t)BOOT_CHARGE_SEGMENT_COUNT;
    for (uint32_t index = 0U; index < BOOT_CHARGE_SEGMENT_COUNT; ++index) {
        lv_obj_t *segment = lv_obj_create(battery);
        clear_style(segment);
        lv_obj_set_pos(segment,
                       6 + (int32_t)index * (segment_w + gap),
                       7);
        lv_obj_set_size(segment, segment_w, frame_h - 18);
        lv_obj_set_style_bg_color(segment, COLOR_BOOT_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(segment, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(segment, COLOR_BOOT_BLUE, LV_PART_MAIN);
        lv_obj_set_style_border_opa(segment, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_radius(segment, 2, LV_PART_MAIN);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_ui.boot_charge_segments[index] = segment;
    }

    s_ui.boot_progress = label(overlay,
                               0,
                               frame_y + frame_h + (portrait ? 22 : 14),
                               s_ui.width,
                               lv_font_montserrat_48.line_height + 4,
                               &lv_font_montserrat_48);
    snprintf(s_ui.boot_progress_buf, sizeof(s_ui.boot_progress_buf), "0%%");
    lv_label_set_text_static(s_ui.boot_progress, s_ui.boot_progress_buf);
    lv_obj_set_style_text_align(s_ui.boot_progress, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.boot_progress, COLOR_BOOT_CYAN, LV_PART_MAIN);

    s_ui.boot_status = label(overlay,
                             20,
                             s_ui.height - 48,
                             s_ui.width - 40,
                             24,
                             &lv_font_montserrat_14);
    snprintf(s_ui.boot_status_buf, sizeof(s_ui.boot_status_buf), "POWER ON");
    lv_label_set_text_static(s_ui.boot_status, s_ui.boot_status_buf);
    lv_obj_set_style_text_align(s_ui.boot_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.boot_status, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_ui.boot_status, 2, LV_PART_MAIN);

    s_ui.boot_scan_line = boot_line(overlay,
                                    14,
                                    32,
                                    s_ui.width - 28,
                                    2,
                                    COLOR_BOOT_CYAN,
                                    LV_OPA_50);
    lv_obj_move_foreground(s_ui.boot_scan_line);
}

static bool boot_gauge_brand_part_register(lv_obj_t *obj,
                                           int32_t x,
                                           int32_t y,
                                           int32_t width,
                                           int32_t height)
{
    for (uint32_t index = 0U; index < BOOT_BRAND_PART_COUNT; ++index) {
        boot_brand_part_t *part = &s_ui.boot_brand_parts[index];
        if (part->obj) {
            continue;
        }
        part->obj = obj;
        part->x = (int16_t)x;
        part->y = (int16_t)y;
        part->width = (int16_t)width;
        part->height = (int16_t)height;
        return true;
    }
    return false;
}

static lv_obj_t *boot_gauge_brand_part_create(lv_obj_t *parent,
                                               int32_t x,
                                               int32_t y,
                                               int32_t width,
                                               int32_t height,
                                               lv_color_t color,
                                               int32_t radius)
{
    lv_obj_t *part = boot_line(parent, x, y, width, height, color, LV_OPA_COVER);
    if (!boot_gauge_brand_part_register(part, x, y, width, height)) {
        lv_obj_delete(part);
        return NULL;
    }
    lv_obj_set_style_radius(part, radius, LV_PART_MAIN);
    return part;
}

static lv_obj_t *boot_gauge_brand_label_create(lv_obj_t *parent,
                                                int32_t x,
                                                int32_t y,
                                                int32_t width,
                                                int32_t height,
                                                const char *text,
                                                const lv_font_t *font,
                                                lv_color_t color)
{
    lv_obj_t *part = label(parent, x, y, width, height, font);
    if (!boot_gauge_brand_part_register(part, x, y, width, height)) {
        lv_obj_delete(part);
        return NULL;
    }
    lv_label_set_text_static(part, text);
    lv_obj_set_style_text_color(part, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(part, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return part;
}

static lv_obj_t *boot_gauge_bmw_badge_create(lv_obj_t *parent, int32_t size)
{
    const int32_t x = (s_ui.width - size) / 2;
    const int32_t y = (s_ui.height - size) / 2;
    const int32_t face = size - 38;
    const int32_t inner = face + 6;
    const int32_t tile = face / 2;
    const int32_t face_x = x + (size - face) / 2;
    const int32_t face_y = y + (size - face) / 2;
    const int32_t label_h = (int32_t)lv_font_montserrat_14.line_height;
    lv_obj_t *mark = boot_gauge_brand_part_create(parent,
                                                   x,
                                                   y,
                                                   size,
                                                   size,
                                                   COLOR_DASHBOARD_BG,
                                                   LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(mark, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(mark, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_set_style_border_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
    (void)boot_gauge_brand_part_create(parent,
                                       x + (size - inner) / 2,
                                       y + (size - inner) / 2,
                                       inner,
                                       inner,
                                       COLOR_MUTED,
                                       LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent,
                                       face_x - 2,
                                       face_y - 2,
                                       face + 4,
                                       face + 4,
                                       COLOR_WHITE,
                                       LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, face_x, face_y, tile, tile,
                                       COLOR_BOOT_BLUE, 0);
    (void)boot_gauge_brand_part_create(parent, face_x + tile, face_y + tile, tile, tile,
                                       COLOR_BOOT_BLUE, 0);
    (void)boot_gauge_brand_part_create(parent,
                                       face_x + tile - 1,
                                       face_y,
                                       2,
                                       face,
                                       COLOR_MUTED,
                                       0);
    (void)boot_gauge_brand_part_create(parent,
                                       face_x,
                                       face_y + tile - 1,
                                       face,
                                       2,
                                       COLOR_MUTED,
                                       0);
    (void)boot_gauge_brand_label_create(parent,
                                        x + 8,
                                        y + size / 3 - label_h / 2,
                                        16,
                                        label_h,
                                        "B",
                                        &lv_font_montserrat_14,
                                        COLOR_WHITE);
    (void)boot_gauge_brand_label_create(parent,
                                        x + size / 2 - 8,
                                        y + 2,
                                        16,
                                        label_h,
                                        "M",
                                        &lv_font_montserrat_14,
                                        COLOR_WHITE);
    (void)boot_gauge_brand_label_create(parent,
                                        x + size - 24,
                                        y + size / 3 - label_h / 2,
                                        16,
                                        label_h,
                                        "W",
                                        &lv_font_montserrat_14,
                                        COLOR_WHITE);
    return mark;
}

static lv_obj_t *boot_gauge_honda_badge_create(lv_obj_t *parent,
                                                int32_t width,
                                                int32_t height)
{
    const int32_t x = (s_ui.width - width) / 2;
    const int32_t y = (s_ui.height - height) / 2;
    const int32_t base_y = y + height - 18;
    lv_obj_t *mark = boot_gauge_brand_part_create(parent, x + 8, base_y,
                                                   width - 16, 8,
                                                   COLOR_FIREBLADE_RED, LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, x + 15, base_y - 6, width / 3, 7,
                                        COLOR_FIREBLADE_RED, LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, x + width / 4, base_y - 12, width / 2, 7,
                                        COLOR_FIREBLADE_RED, LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, x + width / 2 - 4, base_y - 18,
                                        width / 2 - 8, 7, COLOR_FIREBLADE_RED,
                                        LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, x + (width * 2) / 3 - 8, base_y - 24,
                                        width / 3, 7, COLOR_FIREBLADE_RED, LV_RADIUS_CIRCLE);
    (void)boot_gauge_brand_part_create(parent, x + width - width / 3, base_y - 30,
                                        width / 5, 7, COLOR_FIREBLADE_RED, LV_RADIUS_CIRCLE);
    return mark;
}

static lv_obj_t *boot_gauge_rr_mark_create(lv_obj_t *parent)
{
    const int32_t width = s_ui.width < 280 ? 108 : 132;
    const int32_t height = (int32_t)lv_font_montserrat_48.line_height + 4;
    lv_obj_t *mark = label(parent,
                           (s_ui.width - width) / 2,
                           (s_ui.height - height) / 2,
                           width,
                           height,
                           &lv_font_montserrat_48);
    lv_label_set_text_static(mark, "RR");
    lv_obj_set_style_text_align(mark, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(mark, COLOR_BOOT_BLUE, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(mark, 3, LV_PART_MAIN);
    set_obj_hidden(mark, true);
    return mark;
}

static void boot_gauge_hud_create(void)
{
    lv_obj_t *overlay = boot_overlay_create(false);
    const bool honda = s_ui.boot_animation_style ==
                       (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE;
    const int32_t mark_w = honda ? (s_ui.width < 280 ? 132 : 160)
                                 : (s_ui.width < 280 ? 96 : 112);
    const int32_t mark_h = honda ? 92 : mark_w;
    lv_obj_t *mark = honda ? boot_gauge_honda_badge_create(overlay, mark_w, mark_h)
                            : boot_gauge_bmw_badge_create(overlay, mark_w);
    if (!mark) {
        return;
    }
    s_ui.boot_brand_mark = mark;
    if (!honda) {
        s_ui.boot_rr_mark = boot_gauge_rr_mark_create(overlay);
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mark);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_duration(&anim, 900);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_exec_cb(&anim, boot_gauge_brand_spin_anim_cb);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

esp_bms_speed_dashboard_style_t boot_gauge_dashboard_style(void)
{
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    if (s_ui.boot_animation_style ==
        (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE) {
        return ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE;
    }
#endif
    return ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR;
}

static uint32_t boot_gauge_sweep_progress(uint8_t progress_percent)
{
    if (progress_percent <= BOOT_GAUGE_BRAND_INTRO_PERCENT) {
        return 0U;
    }
    return ((uint32_t)(progress_percent - BOOT_GAUGE_BRAND_INTRO_PERCENT) * 100U) /
           (100U - BOOT_GAUGE_BRAND_INTRO_PERCENT);
}

uint16_t boot_gauge_demo_speed(uint8_t progress_percent)
{
    const uint16_t maximum = s_ui.boot_speed_unit == ESP_BMS_SPEED_UNIT_MPH
                                 ? 1200U
                                 : 1800U;
    const uint32_t sweep_progress = boot_gauge_sweep_progress(progress_percent);
    const uint32_t phase = sweep_progress <= 50U
                               ? sweep_progress * 2U
                               : (100U - sweep_progress) * 2U;
    return (uint16_t)(((uint32_t)maximum * phase) / 100U);
}

static void boot_gauge_dashboard_set_visible(bool visible)
{
    const bool fireblade = s_ui.boot_dashboard_style ==
                           ESP_BMS_SPEED_DASHBOARD_STYLE_HONDA_FIREBLADE;
    set_obj_hidden(s_ui.speed_art, fireblade || !visible);
    set_obj_hidden(s_ui.fireblade_page, !fireblade || !visible);
}

static void boot_gauge_apply(uint8_t progress_percent)
{
    esp_bms_dashboard_snapshot_t demo = s_ui.last_snapshot;
    const bool intro = progress_percent < BOOT_GAUGE_BRAND_INTRO_PERCENT;
    const bool rr_intro = s_ui.boot_animation_style ==
                              (uint8_t)ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR &&
                          progress_percent >= BOOT_GAUGE_BMW_RR_PERCENT && intro;
    const bool brand_intro = intro && !rr_intro;

    demo.speed_unit = s_ui.boot_speed_unit;
    demo.speed_dashboard_style = s_ui.boot_dashboard_style;
    demo.speed_deci_units = boot_gauge_demo_speed(progress_percent);
    demo.gps_module_state = (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE;
    esp_bms_dashboard_snapshot_flag_set(
        &demo, ESP_BMS_DASHBOARD_FLAG_CONTROLLER_PAGE_ENABLED, false);
    esp_bms_dashboard_snapshot_flag_set(
        &demo, ESP_BMS_DASHBOARD_FLAG_GPS_FIX_VALID, true);
    esp_bms_dashboard_snapshot_flag_set(
        &demo, ESP_BMS_DASHBOARD_FLAG_SPEED_VALID, true);
    s_ui.last_snapshot = demo;
    speed_dashboard_style_apply(&s_ui.last_snapshot);
    set_gps_dashboard(&s_ui.last_snapshot);
    lv_obj_move_foreground(s_ui.boot_overlay);
    lv_obj_set_style_bg_opa(s_ui.boot_overlay,
                            intro ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    boot_gauge_dashboard_set_visible(!intro);
    set_obj_hidden(s_ui.boot_rr_mark, !rr_intro);
    if (brand_intro) {
        boot_gauge_brand_set_hidden(false);
    } else {
        boot_gauge_brand_hide();
    }
}

esp_err_t esp_bms_lvgl_ui_boot_start(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    ESP_RETURN_ON_ERROR(rebuild_screen_if_needed(snapshot), TAG,
                        "rebuild boot UI failed");

    const uint8_t configured_style = snapshot->boot_animation_style;
    s_ui.boot_animation_style = boot_animation_style_is_available(configured_style)
                                    ? configured_style
                                    : (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    s_ui.boot_dashboard_style = boot_gauge_dashboard_style();
    s_ui.boot_speed_unit = snapshot->speed_unit;
    s_ui.boot_active = true;
    apply_dashboard_snapshot(snapshot);
    show_dashboard_view();

    if (boot_animation_style_is_gauge(s_ui.boot_animation_style)) {
        move_to_page(ESP_BMS_LVGL_PAGE_GPS, false);
        boot_gauge_hud_create();
    } else {
        move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
        boot_charge_create();
    }
    return esp_bms_lvgl_ui_boot_update(0U, "POWER ON");
}

esp_err_t esp_bms_lvgl_ui_boot_update(uint8_t progress_percent, const char *status_text)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    ESP_RETURN_ON_FALSE(s_ui.boot_active && s_ui.boot_overlay,
                        ESP_ERR_INVALID_STATE, TAG, "boot animation is not active");

    const uint8_t progress = progress_percent > 100U ? 100U : progress_percent;
    if (boot_animation_style_is_gauge(s_ui.boot_animation_style)) {
        boot_gauge_apply(progress);
    } else {
        char progress_text[sizeof(s_ui.boot_progress_buf)] = { 0 };
        (void)snprintf(progress_text, sizeof(progress_text), "%u%%", (unsigned)progress);
        gps_label_set(s_ui.boot_progress,
                      s_ui.boot_progress_buf,
                      sizeof(s_ui.boot_progress_buf),
                      progress_text);
        gps_label_set(s_ui.boot_status,
                      s_ui.boot_status_buf,
                      sizeof(s_ui.boot_status_buf),
                      status_text && status_text[0] != '\0' ? status_text : "BOOT");
        const uint32_t filled = progress == 0U
                                    ? 0U
                                    : (((uint32_t)progress * BOOT_CHARGE_SEGMENT_COUNT) + 99U) /
                                          100U;
        for (uint32_t index = 0U; index < BOOT_CHARGE_SEGMENT_COUNT; ++index) {
            if (!s_ui.boot_charge_segments[index]) {
                continue;
            }
            const bool active = index < filled;
            lv_obj_set_style_bg_color(s_ui.boot_charge_segments[index],
                                      active ? COLOR_BOOT_CYAN : COLOR_BOOT_DIM,
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(s_ui.boot_charge_segments[index],
                                          active ? COLOR_BOOT_CYAN : COLOR_BOOT_BLUE,
                                          LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_ui.boot_charge_segments[index],
                                    active ? LV_OPA_COVER : LV_OPA_60,
                                    LV_PART_MAIN);
        }
        if (s_ui.boot_scan_line) {
            const int32_t scan_min_y = 32;
            const int32_t scan_range = s_ui.height - 66;
            lv_obj_set_y(s_ui.boot_scan_line,
                         scan_min_y + (scan_range * (int32_t)progress) / 100);
        }
    }
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_boot_finish(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");

    s_ui.boot_active = false;
    boot_overlay_delete();
    apply_dashboard_snapshot(snapshot);
    show_dashboard_view();
    move_to_page(ESP_BMS_LVGL_PAGE_BATTERY, false);
    return ESP_OK;
}

static void ota_overlay_delete(void)
{
    if (s_ui.ota_overlay) {
        lv_obj_delete(s_ui.ota_overlay);
    }
    s_ui.ota_overlay = NULL;
    s_ui.ota_status = NULL;
    s_ui.ota_percent = NULL;
    s_ui.ota_warning = NULL;
    memset(s_ui.ota_bar_segments, 0, sizeof(s_ui.ota_bar_segments));
    s_ui.ota_active = false;
    UI_SET_FLAG(OTA_ACTIVE, false);
}

static lv_obj_t *ota_overlay_create(void)
{
    ota_overlay_delete();
    lv_obj_t *overlay = lv_obj_create(s_ui.root);
    clear_style(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, s_ui.width, s_ui.height);
    lv_obj_set_style_bg_color(overlay, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(overlay);
    s_ui.ota_overlay = overlay;

    for (int32_t x = 32; x < s_ui.width; x += 32) {
        (void)boot_line(overlay, x, 28, 1, s_ui.height - 56,
                        COLOR_BOOT_GRID, LV_OPA_COVER);
    }
    for (int32_t y = 40; y < s_ui.height - 24; y += 28) {
        (void)boot_line(overlay, 16, y, s_ui.width - 32, 1,
                        COLOR_BOOT_GRID, LV_OPA_COVER);
    }
    boot_corner_marks(overlay);

    lv_obj_t *title = label(overlay, 0, 12, s_ui.width, 18, &lv_font_montserrat_14);
    lv_label_set_text_static(title, "FIRMWARE UPDATE");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, COLOR_BOOT_CYAN, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 1, LV_PART_MAIN);

    lv_obj_t *warning = label(overlay, 16, 32, s_ui.width - 32, 18, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(warning, COLOR_WARN, LV_PART_MAIN);
    (void)snprintf(s_ui.ota_warning_buf, sizeof(s_ui.ota_warning_buf), "DO NOT POWER OFF");
    lv_label_set_text_static(warning, s_ui.ota_warning_buf);
    s_ui.ota_warning = warning;

    lv_obj_t *percent = label(overlay,
                              0,
                              64,
                              s_ui.width,
                              lv_font_montserrat_48.line_height + 4,
                              &lv_font_montserrat_48);
    lv_obj_set_style_text_align(percent, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(percent, COLOR_BOOT_CYAN, LV_PART_MAIN);
    (void)snprintf(s_ui.ota_percent_buf, sizeof(s_ui.ota_percent_buf), "0%%");
    lv_label_set_text_static(percent, s_ui.ota_percent_buf);
    s_ui.ota_percent = percent;

    const int32_t bar_w = s_ui.width - 80;
    const int32_t bar_x = (s_ui.width - bar_w) / 2;
    const int32_t bar_y = 136;
    const int32_t bar_h = 12;
    const int32_t gap = 2;
    const int32_t segment_w =
        (bar_w - ((int32_t)OTA_BAR_SEGMENT_COUNT - 1) * gap) /
        (int32_t)OTA_BAR_SEGMENT_COUNT;
    for (uint32_t index = 0U; index < OTA_BAR_SEGMENT_COUNT; ++index) {
        lv_obj_t *segment = lv_obj_create(overlay);
        clear_style(segment);
        lv_obj_set_pos(segment, bar_x + (int32_t)index * (segment_w + gap), bar_y);
        lv_obj_set_size(segment, segment_w, bar_h);
        lv_obj_set_style_bg_color(segment, COLOR_BOOT_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(segment, 2, LV_PART_MAIN);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_ui.ota_bar_segments[index] = segment;
    }

    lv_obj_t *status = label(overlay,
                             16,
                             s_ui.height - 64,
                             s_ui.width - 32,
                             24,
                             &lv_font_montserrat_14);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(status, 1, LV_PART_MAIN);
    (void)snprintf(s_ui.ota_status_buf, sizeof(s_ui.ota_status_buf), "UPLOADING");
    lv_label_set_text_static(status, s_ui.ota_status_buf);
    s_ui.ota_status = status;

    s_ui.ota_active = true;
    UI_SET_FLAG(OTA_ACTIVE, true);
    return overlay;
}

esp_err_t esp_bms_lvgl_ui_ota_update(uint8_t progress_percent,
                                     const char *status_text,
                                     bool failed)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    if (!s_ui.ota_active || !s_ui.ota_overlay) {
        ota_overlay_create();
    }

    const uint8_t progress = progress_percent > 100U ? 100U : progress_percent;
    char percent_text[sizeof(s_ui.ota_percent_buf)] = { 0 };
    (void)snprintf(percent_text, sizeof(percent_text), "%u%%", (unsigned)progress);
    gps_label_set(s_ui.ota_percent,
                  s_ui.ota_percent_buf,
                  sizeof(s_ui.ota_percent_buf),
                  percent_text);
    const uint32_t filled = progress == 0U
                                ? 0U
                                : (((uint32_t)progress * OTA_BAR_SEGMENT_COUNT) + 99U) /
                                      100U;
    for (uint32_t index = 0U; index < OTA_BAR_SEGMENT_COUNT; ++index) {
        lv_obj_t *segment = s_ui.ota_bar_segments[index];
        if (!segment) {
            continue;
        }
        const bool active = index < filled;
        lv_obj_set_style_bg_color(segment,
                                  active ? COLOR_BOOT_CYAN : COLOR_BOOT_DIM,
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment,
                                active ? LV_OPA_COVER : LV_OPA_60,
                                LV_PART_MAIN);
    }
    if (status_text && status_text[0] != '\0') {
        gps_label_set(s_ui.ota_status,
                      s_ui.ota_status_buf,
                      sizeof(s_ui.ota_status_buf),
                      status_text);
    }
    if (s_ui.ota_warning) {
        if (failed) {
            label_set_text_if_changed(s_ui.ota_warning, "UPDATE FAILED");
            lv_obj_set_style_text_color(s_ui.ota_warning, COLOR_BAD, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.ota_status, COLOR_BAD, LV_PART_MAIN);
        } else {
            label_set_text_if_changed(s_ui.ota_warning, "DO NOT POWER OFF");
            lv_obj_set_style_text_color(s_ui.ota_warning, COLOR_WARN, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_ui.ota_status, COLOR_TEXT, LV_PART_MAIN);
        }
    }
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_ota_finish(void)
{
    ESP_RETURN_ON_FALSE(UI_FLAG(INITIALIZED), ESP_ERR_INVALID_STATE, TAG,
                        "UI is not initialized");
    ota_overlay_delete();
    /* OTA_FINISH 只在失败路径触发（成功路径直接 esp_restart 重启）。
       失败后回到热点共享页，方便用户重试，而不是停留在仪表首页。 */
    if (lv_obj_get_parent(s_ui.settings_page) != s_ui.root ||
        lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN)) {
        show_settings_view();
    }
    settings_show_detail(SETTINGS_DETAIL_HOTSPOT);
    return ESP_OK;
}

