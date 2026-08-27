/*
 * UI 模块: bms_dash
 * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。
 */
#include "esp_bms_lvgl_ui_internal.h"

LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_0_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_1_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_2_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
LV_DRAW_BUF_DEFINE_STATIC(s_dashboard_cell_key_3_draw_buf,
                          DASHBOARD_CELL_KEY_BITMAP_W,
                          DASHBOARD_CELL_KEY_BITMAP_H,
                          LV_COLOR_FORMAT_ARGB8888);
static uint8_t s_dashboard_cell_key_draw_buf_initialized_flags;
static int32_t s_dashboard_battery_inner_w;
static const uint8_t DASHBOARD_CELL_STAT_KEY_BITMAPS[DASHBOARD_CELL_STAT_COUNT]
                                                   [DASHBOARD_CELL_KEY_BITMAP_BYTES] = {
    {
        0x3f, 0xf0, 0x08, 0x03, 0x01, 0x1f, 0xfe, 0x3f, 0xf0, 0x00, 0x03, 0x01,
        0x07, 0xf8, 0x3f, 0xf0, 0x40, 0x80, 0x00, 0x07, 0xf8, 0x7f, 0xf8, 0x00,
        0x02, 0x20, 0x0f, 0xfe, 0x3f, 0xf8, 0x80, 0x62, 0x29, 0x0b, 0xf6, 0x3e,
        0xb0, 0xa1, 0x62, 0x26, 0x0a, 0x16, 0x7e, 0xf0, 0xbf, 0x60, 0x30, 0x88,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0x00, 0x03, 0xff, 0x02, 0x00, 0x30, 0x10, 0x43, 0xc3, 0xff,
        0x05, 0xd0, 0x30, 0x10, 0x91, 0x03, 0xff, 0x19, 0x10, 0x00, 0x03, 0x9f,
        0xe7, 0xff, 0x89, 0x10, 0x22, 0x00, 0x91, 0x03, 0xff, 0x89, 0x08, 0x22,
        0x90, 0x90, 0x83, 0xeb, 0x09, 0xe9, 0x22, 0x60, 0xb8, 0x57, 0xef, 0x08,
        0x06, 0x03, 0x08, 0x9f, 0x80, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0x41, 0x83, 0xff, 0x82, 0x10, 0x20, 0x01, 0xff, 0xe2, 0x18,
        0x00, 0xc0, 0x21, 0x80, 0xff, 0xc2, 0x18, 0x00, 0xc0, 0x2f, 0xf8, 0x0c,
        0x02, 0x18, 0x1f, 0xfe, 0x21, 0xa0, 0x60, 0x04, 0x1b, 0x05, 0xfc, 0x41,
        0x90, 0x42, 0x04, 0x18, 0x0c, 0x20, 0x5f, 0xf8, 0x82, 0x00, 0x00, 0x17,
        0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    {
        0x00, 0x00, 0xc4, 0x07, 0xff, 0x8c, 0x40, 0x03, 0x00, 0xcc, 0x01, 0x33,
        0x0c, 0xfe, 0x13, 0x21, 0xf8, 0x21, 0x32, 0x0d, 0x02, 0x0b, 0x40, 0xcf,
        0x20, 0x30, 0x0c, 0x02, 0x7f, 0xf8, 0xe0, 0x20, 0x30, 0x0e, 0x1a, 0x03,
        0x01, 0x8e, 0x20, 0x30, 0x00, 0x82, 0x03, 0x00, 0x00, 0x60, 0x30, 0x00,
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
};

lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 4, LV_PART_MAIN);
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(obj, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return obj;
}

lv_obj_t *dashboard_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t w,
                                 int32_t h,
                                 lv_color_t color,
                                 lv_color_t border_color)
{
    lv_obj_t *obj = panel(parent, x, y, w, h, color);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_post(obj, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    return obj;
}

lv_obj_t *dashboard_viewport(lv_obj_t *parent, bool portrait)
{
    const int32_t layout_w = portrait ? 240 : 320;
    const int32_t layout_h = portrait ? 320 : 240;
    lv_obj_t *viewport = lv_obj_create(parent);
    clear_style(viewport);
    lv_obj_set_pos(viewport, 0, 0);
    lv_obj_set_size(viewport, layout_w, layout_h);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return viewport;
}

lv_obj_t *dashboard_separator(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *line = lv_obj_create(parent);
    clear_style(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, 1);
    lv_obj_set_style_bg_color(line, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    return line;
}

void dashboard_battery_icon(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h)
{
    lv_obj_t *body = lv_obj_create(parent);
    clear_style(body);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, w, h);
    lv_obj_set_style_radius(body, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(body, COLOR_DASHBOARD_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(body, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_post(body, true, LV_PART_MAIN);

    s_ui.soc_battery_level = lv_obj_create(body);
    clear_style(s_ui.soc_battery_level);
    lv_obj_set_pos(s_ui.soc_battery_level, 2, 2);
    lv_obj_set_size(s_ui.soc_battery_level, w - 4, h - 4);
    s_dashboard_battery_inner_w = w - 4;
    lv_obj_set_style_radius(s_ui.soc_battery_level, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.soc_battery_level, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.soc_battery_level, LV_OPA_COVER, LV_PART_MAIN);

    for (int32_t index = 1; index < 5; ++index) {
        lv_obj_t *divider = dashboard_separator(body, (w * index) / 5, 2, 1);
        lv_obj_set_height(divider, h - 4);
    }

    lv_obj_t *terminal = lv_obj_create(parent);
    clear_style(terminal);
    lv_obj_set_pos(terminal, x + w, y + ((h - 8) / 2));
    lv_obj_set_size(terminal, 4, 8);
    lv_obj_set_style_radius(terminal, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(terminal, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, LV_PART_MAIN);
}


void update_dashboard_battery_icon(uint8_t soc_percent, bool valid, bool charging)
{
    if (!s_ui.soc_battery_level) {
        return;
    }

    const int32_t inner_w = s_dashboard_battery_inner_w;
    const uint8_t soc = valid ? (soc_percent > 100U ? 100U : soc_percent) : 0U;
    lv_obj_set_width(s_ui.soc_battery_level, (inner_w * (int32_t)soc) / 100);
    lv_obj_set_style_bg_color(s_ui.soc_battery_level,
                              dashboard_soc_fill_color(soc, valid, charging),
                              LV_PART_MAIN);
}

void dashboard_thermometer_icon(lv_obj_t *parent, int32_t center_x, int32_t y)
{
    lv_obj_t *stem = lv_obj_create(parent);
    clear_style(stem);
    lv_obj_set_pos(stem, center_x - 1, y);
    lv_obj_set_size(stem, 3, 10);
    lv_obj_set_style_radius(stem, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(stem, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *bulb = lv_obj_create(parent);
    clear_style(bulb);
    lv_obj_set_pos(bulb, center_x - 3, y + 7);
    lv_obj_set_size(bulb, 7, 7);
    lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bulb, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, LV_PART_MAIN);
}

static bms_native_layout_t bms_native_layout(void)
{
    const int32_t width = s_ui.width;
    const int32_t height = s_ui.height;
    const int32_t margin = width / 60;
    const int32_t gap = margin;
    const int32_t content_w = width - (margin * 2);
    const int32_t left_w = (content_w * 28) / 100;
    const int32_t rows_h = height - (margin * 2) - (gap * 3);
    const int32_t electrical_h = rows_h / 4;
    const int32_t temp_h = rows_h / 5;
    const int32_t cell_h = (rows_h * 3) / 16;
    const int32_t temp_y = margin + electrical_h + gap;
    const int32_t cell_y = temp_y + temp_h + gap;
    const int32_t safety_y = cell_y + cell_h + gap;
    bms_native_layout_t layout = {
        .margin = margin,
        .gap = gap,
        .content_w = content_w,
        .top_y = margin,
        .left_w = left_w,
        .right_x = margin + left_w + gap,
        .right_w = content_w - left_w - gap,
        .electrical_h = electrical_h,
        .temp_y = temp_y,
        .temp_h = temp_h,
        .cell_y = cell_y,
        .cell_h = cell_h,
        .safety_y = safety_y,
        .safety_h = height - safety_y - margin,
    };
    return layout;
}

lv_obj_t *dashboard_native_layer(lv_obj_t *parent,
                                        int32_t x,
                                        int32_t y,
                                        int32_t width,
                                        int32_t height)
{
    lv_obj_t *layer = lv_obj_create(parent);
    clear_style(layer);
    lv_obj_set_pos(layer, x, y);
    lv_obj_set_size(layer, width, height);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}

static void bms_native_static_label(lv_obj_t *parent,
                                    const char *text,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    int32_t height,
                                    const lv_font_t *font,
                                    lv_color_t color,
                                    lv_text_align_t align)
{
    lv_obj_t *obj = label(parent, x, y, width, height, font);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, align, LV_PART_MAIN);
}

static void bms_native_safety_icon(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *circle = lv_obj_create(parent);
    clear_style(circle);
    lv_obj_set_pos(circle, x, y);
    lv_obj_set_size(circle, 12, 12);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(circle, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(circle, COLOR_MUTED, LV_PART_MAIN);
    lv_obj_set_style_border_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_obj_t *bms_native_safety_check(lv_obj_t *parent, int32_t x, int32_t y)
{
    static const lv_point_precise_t check_points[] = {
        { 2, 6 }, { 5, 9 }, { 10, 3 },
    };
    lv_obj_t *check = lv_line_create(parent);
    lv_line_set_points(check, check_points, ARRAY_SIZE(check_points));
    lv_obj_set_pos(check, x, y);
    lv_obj_set_size(check, 12, 12);
    lv_obj_set_style_line_width(check, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(check, COLOR_STATUS_OK, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(check, true, LV_PART_MAIN);
    return check;
}

void create_native_bms_dashboard(void)
{
    const bms_native_layout_t layout = bms_native_layout();
    /* 左侧 SOC 面板从顶部一直拉长到底部，右侧列依次排 electrical/temp/cell/safety */
    const int32_t left_h = s_ui.height - layout.top_y - layout.margin;
    const int32_t soc_ring_size = LV_MIN(layout.left_w - 28, left_h - 180);
    s_ui.native_bms_dashboard = true;

    lv_obj_t *static_layer = dashboard_native_layer(s_ui.battery_page,
                                                     0,
                                                     0,
                                                     s_ui.width,
                                                     s_ui.height);
    lv_obj_set_style_bg_color(static_layer, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(static_layer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *soc_panel = dashboard_panel(static_layer,
                                          layout.margin,
                                          layout.top_y,
                                          layout.left_w,
                                          left_h,
                                          COLOR_DASHBOARD_BG,
                                          COLOR_DASHBOARD_SOC_BORDER);
    lv_obj_t *electrical_panel = dashboard_panel(static_layer,
                                                  layout.right_x,
                                                  layout.top_y,
                                                  layout.right_w,
                                                  layout.electrical_h,
                                           COLOR_DASHBOARD_PANEL,
                                           COLOR_DASHBOARD_BORDER);
    lv_obj_t *temp_panel = dashboard_panel(static_layer,
                                           layout.right_x,
                                           layout.temp_y,
                                           layout.right_w,
                                           layout.temp_h,
                                           COLOR_DASHBOARD_PANEL,
                                           COLOR_DASHBOARD_BORDER);
    lv_obj_t *cell_panel = dashboard_panel(static_layer,
                                           layout.right_x,
                                           layout.cell_y,
                                           layout.right_w,
                                           layout.cell_h,
                                           COLOR_DASHBOARD_PANEL,
                                           COLOR_DASHBOARD_BORDER);
    lv_obj_t *safety_panel = dashboard_panel(static_layer,
                                             layout.right_x,
                                             layout.safety_y,
                                             layout.right_w,
                                             layout.safety_h,
                                             COLOR_DASHBOARD_PANEL,
                                             COLOR_DASHBOARD_BORDER);

    bms_native_static_label(soc_panel,
                            ui_t("电池SOC", "SOC"),
                            10,
                            8,
                            layout.left_w - 20,
                            14,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    dashboard_separator(soc_panel, 10, left_h - 144, layout.left_w - 20);
    bms_native_static_label(soc_panel,
                            ui_t("电池容量", "Capacity"),
                            10,
                            left_h - 140,
                            layout.left_w - 20,
                            12,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    dashboard_separator(soc_panel, 10, left_h - 108, layout.left_w - 20);
    bms_native_static_label(soc_panel,
                            ui_t("剩余里程", "Range"),
                            10,
                            left_h - 104,
                            layout.left_w - 20,
                            12,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    dashboard_separator(soc_panel, 10, left_h - 72, layout.left_w - 20);
    bms_native_static_label(soc_panel,
                            ui_t("使用天数", "Days"),
                            10,
                            left_h - 68,
                            layout.left_w - 20,
                            12,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    dashboard_separator(soc_panel, 10, left_h - 36, layout.left_w - 20);
    bms_native_static_label(soc_panel,
                            ui_t("循环容量", "Cycle cap."),
                            10,
                            left_h - 32,
                            layout.left_w - 20,
                            12,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    bms_native_static_label(electrical_panel,
                            ui_t("实时电气状态", "Live electrical"),
                            12,
                            7,
                            layout.right_w - 24,
                            14,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    lv_obj_t *electrical_separator =
        dashboard_separator(electrical_panel, layout.right_w / 2, 22, 1);
    lv_obj_set_height(electrical_separator, layout.electrical_h - 30);
    const int32_t electrical_value_y =
        layout.top_y + 22 + (layout.electrical_h - 22 - 32) / 2;

    static const char *const temp_keys[] = {
        "温度1", "温度2", "温度3", "温度4", "MOS温度", "均衡温度",
    };
    static const char *const temp_keys_en[] = {
        "T1", "T2", "T3", "T4", "MOS", "BAL",
    };
    const int32_t temp_col_w = layout.right_w / (int32_t)ESP_BMS_BMS_TEMP_MAX_COUNT;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        const int32_t col_x = (int32_t)index * temp_col_w;
        bms_native_static_label(temp_panel,
                                ui_t(temp_keys[index], temp_keys_en[index]),
                                col_x,
                                8,
                                temp_col_w,
                                14,
                                &settings_zh_10,
                                COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER);
    }

    static const char *const cell_keys[] = { "高", "低", "压差", "平均" };
    static const char *const cell_keys_en[] = { "Hi", "Lo", "Delta", "Avg" };
    const int32_t cell_col_w = layout.right_w / (int32_t)DASHBOARD_CELL_STAT_COUNT;
    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        const int32_t col_x = (int32_t)index * cell_col_w;
        bms_native_static_label(cell_panel,
                                ui_t(cell_keys[index], cell_keys_en[index]),
                                col_x,
                                8,
                                cell_col_w,
                                14,
                                &settings_zh_10,
                                COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER);
        if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
            lv_obj_t *separator = dashboard_separator(cell_panel,
                                                       col_x + cell_col_w,
                                                       8,
                                                       1);
            lv_obj_set_height(separator, layout.cell_h - 16);
        }
    }

    bms_native_static_label(safety_panel,
                            ui_t("告警与保护", "Alarms"),
                            12,
                            7,
                            layout.right_w - 24,
                            14,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    const int32_t safety_col_w = layout.right_w / 2;
    const int32_t safety_row_h = (layout.safety_h - 25) / 4;
    const int32_t safety_text_h = (int32_t)settings_zh_10.line_height;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        const int32_t col_x = (int32_t)(index % 2U) * safety_col_w;
        const int32_t row_y = 24 + (int32_t)(index / 2U) * safety_row_h;
        const int32_t text_y = row_y + (safety_row_h - safety_text_h) / 2;
        bms_native_safety_icon(safety_panel,
                               col_x + 12,
                               row_y + (safety_row_h - 12) / 2);
        bms_native_static_label(safety_panel,
                                BMS_SAFETY_KEYS[index],
                                col_x + 28,
                                text_y,
                                safety_col_w - 74,
                                safety_text_h,
                                &settings_zh_10,
                                COLOR_WHITE,
                                LV_TEXT_ALIGN_LEFT);
    }

    (void)dashboard_static_cache_finalize(&s_ui.battery_static_cache,
                                           s_ui.battery_page,
                                           static_layer,
                                           "bms");

    lv_obj_t *dynamic_layer = dashboard_native_layer(s_ui.battery_page,
                                                      0,
                                                      0,
                                                      s_ui.width,
                                                      s_ui.height);
    /* SOC 环在标题与底部 list 之间垂直居中 */
    const int32_t soc_ring_y = layout.top_y + 25;
    s_ui.soc_arc = lv_arc_create(dynamic_layer);
    clear_style(s_ui.soc_arc);
    lv_obj_set_pos(s_ui.soc_arc,
                   layout.margin + (layout.left_w - soc_ring_size) / 2,
                   soc_ring_y);
    lv_obj_set_size(s_ui.soc_arc, soc_ring_size, soc_ring_size);
    lv_arc_set_range(s_ui.soc_arc, 0, 100);
    lv_arc_set_bg_angles(s_ui.soc_arc, 0, 270);
    lv_arc_set_rotation(s_ui.soc_arc, 135);
    lv_obj_set_style_arc_width(s_ui.soc_arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui.soc_arc, COLOR_DASHBOARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui.soc_arc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui.soc_arc, COLOR_SOC, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.soc_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_ui.soc_arc, LV_OBJ_FLAG_CLICKABLE);
    s_ui.soc = label(dynamic_layer,
                     layout.margin,
                     soc_ring_y + (soc_ring_size - 34) / 2,
                     layout.left_w,
                     34,
                     &lv_font_montserrat_28);
    lv_obj_t *range_group = dashboard_native_layer(
        dynamic_layer,
        layout.margin,
        layout.top_y + left_h - 91,
        layout.left_w,
        18);
    s_ui.remaining_range_value = label(range_group,
                                       0,
                                       0,
                                       LV_SIZE_CONTENT,
                                       18,
                                       &lv_font_montserrat_14);
    s_ui.remaining_range_unit = label(range_group,
                                      0,
                                      0,
                                      LV_SIZE_CONTENT,
                                      LV_SIZE_CONTENT,
                                      &settings_zh_10);
    lv_label_set_text_static(s_ui.remaining_range_unit, "km");
    s_ui.capacity = label(dynamic_layer,
                          layout.margin + 8,
                          layout.top_y + left_h - 127,
                          layout.left_w - 16,
                          18,
                          &lv_font_montserrat_14);
    s_ui.bms_running_time = label(dynamic_layer,
                                  layout.margin + 10,
                                  layout.top_y + left_h - 55,
                                  layout.left_w - 20,
                                  18,
                                  &settings_zh_13);
    s_ui.bms_cycle_capacity = label(dynamic_layer,
                                    layout.margin + 8,
                                    layout.top_y + left_h - 18,
                                    layout.left_w - 16,
                                    18,
                                    &lv_font_montserrat_14);

    const int32_t electrical_value_w = layout.right_w / 2;
#if LV_USE_FLEX
    const bool native_bms_480 = s_ui.width == 480 && s_ui.height == 320;
    const int32_t electrical_group_w =
        native_bms_480 ? electrical_value_w - 16 : electrical_value_w;
    const int32_t electrical_group_x = native_bms_480 ? 8 : 0;
    const int32_t electrical_group_y = native_bms_480 ? -2 : 0;
    lv_obj_t *voltage_group = dashboard_native_layer(dynamic_layer,
                                                      layout.right_x + electrical_group_x,
                                                      electrical_value_y + electrical_group_y,
                                                      electrical_group_w,
                                                      32);
    lv_obj_set_flex_flow(voltage_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(voltage_group,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(voltage_group, 2, LV_PART_MAIN);
    s_ui.pack_voltage = label(voltage_group,
                              0,
                              0,
                              LV_SIZE_CONTENT,
                              32,
                              &lv_font_montserrat_28);
    s_ui.pack_voltage_unit = label(voltage_group,
                                   0,
                                   0,
                                   LV_SIZE_CONTENT,
                                   LV_SIZE_CONTENT,
                                   &settings_zh_16);
    lv_label_set_text_static(s_ui.pack_voltage_unit, "V");

    lv_obj_t *current_group = dashboard_native_layer(dynamic_layer,
                                                      layout.right_x + electrical_value_w +
                                                          electrical_group_x,
                                                      electrical_value_y + electrical_group_y,
                                                      electrical_group_w,
                                                      32);
    lv_obj_set_flex_flow(current_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(current_group,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(current_group, 2, LV_PART_MAIN);
    s_ui.current = label(current_group, 0, 0, LV_SIZE_CONTENT, 32, &lv_font_montserrat_28);
    s_ui.current_unit = label(current_group,
                              0,
                              0,
                              LV_SIZE_CONTENT,
                              LV_SIZE_CONTENT,
                              &settings_zh_16);
    lv_label_set_text_static(s_ui.current_unit, "A");
#else
    s_ui.pack_voltage = label(dynamic_layer,
                              layout.right_x,
                              electrical_value_y,
                              electrical_value_w - 18,
                              32,
                              &lv_font_montserrat_28);
    s_ui.pack_voltage_unit = label(dynamic_layer,
                                   layout.right_x + electrical_value_w - 18,
                                   electrical_value_y + 13,
                                   16,
                                   16,
                                   &lv_font_montserrat_14);
    lv_label_set_text_static(s_ui.pack_voltage_unit, "V");
    s_ui.current = label(dynamic_layer,
                         layout.right_x + electrical_value_w,
                         electrical_value_y,
                         electrical_value_w - 18,
                         32,
                         &lv_font_montserrat_28);
    s_ui.current_unit = label(dynamic_layer,
                              layout.right_x + layout.right_w - 18,
                              electrical_value_y + 13,
                              16,
                              16,
                              &lv_font_montserrat_14);
    lv_label_set_text_static(s_ui.current_unit, "A");
#endif

    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        s_ui.cell_stat_values[index] = label(dynamic_layer,
                                             layout.right_x + (int32_t)index * cell_col_w,
                                             layout.cell_y + 30,
                                             cell_col_w,
                                             20,
                                             &lv_font_montserrat_14);
    }

    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        s_ui.temperature_values[index] = label(dynamic_layer,
                                               layout.right_x + (int32_t)index * temp_col_w,
                                               layout.temp_y + 31,
                                               temp_col_w,
                                               20,
                                               &lv_font_montserrat_14);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        const int32_t col_x = (int32_t)(index % 2U) * safety_col_w;
        const int32_t row_y = 24 + (int32_t)(index / 2U) * safety_row_h;
        const int32_t text_y = row_y + (safety_row_h - safety_text_h) / 2;
        s_ui.bms_safety_values[index] = label(dynamic_layer,
                                              layout.right_x + col_x + safety_col_w - 46,
                                              layout.safety_y + text_y,
                                              38,
                                              safety_text_h,
                                              &settings_zh_10);
        s_ui.bms_safety_checks[index] =
            bms_native_safety_check(dynamic_layer,
                                    layout.right_x + col_x + 12,
                                    layout.safety_y + row_y + (safety_row_h - 12) / 2);
    }

    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_running_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_cycle_capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.current, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.soc, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.capacity, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.bms_running_time, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.bms_cycle_capacity, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.pack_voltage, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.pack_voltage_unit, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_translate_y(s_ui.pack_voltage_unit, -2, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.current, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.current_unit, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_translate_y(s_ui.current_unit, -2, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_value, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_unit, COLOR_ACCENT, LV_PART_MAIN);
    const lv_color_t bms_value_color =
        s_ui.native_bms_dashboard ? COLOR_WHITE : COLOR_DASHBOARD_VALUE;
    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.cell_stat_values[index], bms_value_color,
                                    LV_PART_MAIN);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.temperature_values[index], bms_value_color,
                                    LV_PART_MAIN);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.bms_safety_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
}

void create_native_bms_portrait_dashboard(void)
{
    static const char *const cell_keys[] = { "高", "低", "压差", "均" };
    static const char *const cell_keys_en[] = { "Hi", "Lo", "Delta", "Avg" };
    static const char *const temp_keys[] = { "T1", "T2", "T3", "T4", "BAL", "MOS" };
    const int32_t margin = 8;
    const int32_t content_w = s_ui.width - (margin * 2);
    const int32_t soc_y = 8;
    const int32_t soc_h = 115;
    const int32_t electrical_y = soc_y + soc_h + margin;
    const int32_t electrical_h = 54;
    const int32_t top_panel_h = electrical_y + electrical_h - soc_y;
    const int32_t cell_y = 193;
    const int32_t cell_h = 54;
    const int32_t temp_y = 255;
    const int32_t temp_h = 52;
    const int32_t temp_row_pitch = 22;
    const int32_t metric_y = temp_y + temp_h + 8;
    const int32_t metric_h = 48;
    const int32_t safety_y = metric_y + metric_h + 8;
    const int32_t safety_h = s_ui.height - safety_y - margin;
    const int32_t safety_col_w = content_w / 2;
    const int32_t safety_row_h = (safety_h - 23) / 4;
    const int32_t safety_text_h = (int32_t)settings_zh_10.line_height;
    const int32_t soc_battery_x = margin + 18;
    const int32_t soc_battery_y = soc_y + 43;
    const int32_t soc_battery_w = content_w - 40;
    const int32_t soc_battery_h = 48;
    const int32_t soc_value_w = 88;
    s_ui.native_bms_dashboard = true;

    lv_obj_t *static_layer = dashboard_native_layer(s_ui.battery_page,
                                                     0,
                                                     0,
                                                     s_ui.width,
                                                     s_ui.height);
    lv_obj_set_style_bg_color(static_layer, COLOR_DASHBOARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(static_layer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *soc_panel = dashboard_panel(static_layer,
                                          margin,
                                          soc_y,
                                          content_w,
                                          top_panel_h,
                                          COLOR_DASHBOARD_BG,
                                          COLOR_DASHBOARD_SOC_BORDER);
    lv_obj_t *cell_panel = dashboard_panel(static_layer,
                                            margin,
                                            cell_y,
                                            content_w,
                                            cell_h,
                                            COLOR_DASHBOARD_PANEL,
                                            COLOR_DASHBOARD_BORDER);
    lv_obj_t *temp_panel = dashboard_panel(static_layer,
                                            margin,
                                            temp_y,
                                            content_w,
                                            temp_h,
                                            COLOR_DASHBOARD_PANEL,
                                            COLOR_DASHBOARD_BORDER);
    lv_obj_t *metric_panel = dashboard_panel(static_layer,
                                              margin,
                                              metric_y,
                                              content_w,
                                              metric_h,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);
    lv_obj_t *safety_panel = dashboard_panel(static_layer,
                                              margin,
                                              safety_y,
                                              content_w,
                                              safety_h,
                                              COLOR_DASHBOARD_PANEL,
                                              COLOR_DASHBOARD_BORDER);

    bms_native_static_label(soc_panel,
                            "BMS",
                            12,
                            8,
                            48,
                            14,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    dashboard_separator(soc_panel, 8, electrical_y - soc_y, content_w - 16);
    bms_native_static_label(soc_panel,
                            ui_t("电压", "Voltage"),
                            0,
                            electrical_y - soc_y + 7,
                            content_w / 2,
                            14,
                            &settings_zh_10,
                            COLOR_MUTED,
                            LV_TEXT_ALIGN_CENTER);
    bms_native_static_label(soc_panel,
                            ui_t("电流", "Current"),
                            content_w / 2,
                            electrical_y - soc_y + 7,
                            content_w / 2,
                            14,
                            &settings_zh_10,
                            COLOR_MUTED,
                            LV_TEXT_ALIGN_CENTER);
    lv_obj_t *electrical_separator =
        dashboard_separator(soc_panel, content_w / 2, electrical_y - soc_y + 8, 1);
    lv_obj_set_height(electrical_separator, electrical_h - 16);

    const int32_t cell_col_w = content_w / (int32_t)DASHBOARD_CELL_STAT_COUNT;
    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        const int32_t col_x = (int32_t)index * cell_col_w;
        bms_native_static_label(cell_panel,
                                ui_t(cell_keys[index], cell_keys_en[index]),
                                col_x,
                                7,
                                cell_col_w,
                                14,
                                &settings_zh_10,
                                COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER);
        if (index + 1U < DASHBOARD_CELL_STAT_COUNT) {
            lv_obj_t *separator = dashboard_separator(cell_panel, col_x + cell_col_w, 7, 1);
            lv_obj_set_height(separator, cell_h - 14);
        }
    }

    const int32_t temp_col_w = content_w / 3;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        const int32_t column = (int32_t)index % 3;
        const int32_t row = (int32_t)index / 3;
        bms_native_static_label(temp_panel,
                                temp_keys[index],
                                column * temp_col_w + 8,
                                7 + row * temp_row_pitch,
                                34,
                                14,
                                &settings_zh_10,
                                COLOR_MUTED,
                                LV_TEXT_ALIGN_LEFT);
        if (row > 0) {
            dashboard_separator(temp_panel,
                                column * temp_col_w + 8,
                                7 + row * temp_row_pitch - 3,
                                temp_col_w - 16);
        }
    }

    static const char *const metric_keys[] = { "容量", "剩余里程", "使用时长" };
    static const char *const metric_keys_en[] = { "Cap", "Range", "Runtime" };
    const int32_t metric_col_w = content_w / 3;
    for (uint8_t index = 0U; index < ARRAY_SIZE(metric_keys); ++index) {
        const int32_t col_x = (int32_t)index * metric_col_w;
        bms_native_static_label(metric_panel,
                                ui_t(metric_keys[index], metric_keys_en[index]),
                                col_x,
                                6,
                                metric_col_w,
                                12,
                                &settings_zh_10,
                                COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER);
        if (index + 1U < ARRAY_SIZE(metric_keys)) {
            lv_obj_t *separator = dashboard_separator(metric_panel, col_x + metric_col_w, 8, 1);
            lv_obj_set_height(separator, metric_h - 16);
        }
    }

    bms_native_static_label(safety_panel,
                            ui_t("告警与保护", "Alarms"),
                            12,
                            7,
                            content_w - 24,
                            14,
                            &settings_zh_10,
                            COLOR_DASHBOARD_TITLE,
                            LV_TEXT_ALIGN_LEFT);
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        const int32_t col_x = (int32_t)(index % 2U) * safety_col_w;
        const int32_t row_y = 23 + (int32_t)(index / 2U) * safety_row_h;
        const int32_t text_y = row_y + (safety_row_h - safety_text_h) / 2;
        bms_native_safety_icon(safety_panel,
                               col_x + 10,
                               row_y + (safety_row_h - 12) / 2);
        bms_native_static_label(safety_panel,
                                BMS_SAFETY_KEYS[index],
                                col_x + 26,
                                text_y,
                                safety_col_w - 72,
                                safety_text_h,
                                &settings_zh_10,
                                COLOR_WHITE,
                                LV_TEXT_ALIGN_LEFT);
    }

    (void)dashboard_static_cache_finalize(&s_ui.battery_static_cache,
                                           s_ui.battery_page,
                                           static_layer,
                                           "bms");

    lv_obj_t *dynamic_layer = dashboard_native_layer(s_ui.battery_page,
                                                      0,
                                                      0,
                                                      s_ui.width,
                                                      s_ui.height);
    s_ui.soc_arc = NULL;
    dashboard_battery_icon(dynamic_layer,
                           soc_battery_x,
                           soc_battery_y,
                           soc_battery_w,
                           soc_battery_h);
    s_ui.soc = label(dynamic_layer,
                     s_ui.width - margin - 12 - soc_value_w,
                     soc_y + 4,
                     soc_value_w,
                     32,
                     &lv_font_montserrat_28);
    s_ui.pack_voltage = label(dynamic_layer,
                              margin,
                              electrical_y + 25,
                              content_w / 2,
                              24,
                              &lv_font_montserrat_24);
    s_ui.current = label(dynamic_layer,
                         margin + (content_w / 2),
                         electrical_y + 25,
                         content_w / 2,
                         24,
                         &lv_font_montserrat_24);

    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        s_ui.cell_stat_values[index] = label(dynamic_layer,
                                             margin + (int32_t)index * cell_col_w,
                                             cell_y + 28,
                                             cell_col_w,
                                             18,
                                             &lv_font_montserrat_14);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        const int32_t column = (int32_t)index % 3;
        const int32_t row = (int32_t)index / 3;
        s_ui.temperature_values[index] = label(dynamic_layer,
                                               margin + column * temp_col_w + 40,
                                               temp_y + 7 + row * temp_row_pitch,
                                               temp_col_w - 48,
                                               14,
                                               &lv_font_montserrat_14);
    }
    s_ui.capacity = label(dynamic_layer,
                          margin,
                          metric_y + 24,
                          metric_col_w,
                          18,
                          &settings_zh_13);
    lv_obj_t *range_group = dashboard_native_layer(dynamic_layer,
                                                    margin + metric_col_w,
                                                    metric_y + 24,
                                                    metric_col_w,
                                                    18);
    s_ui.remaining_range_value = label(range_group,
                                       0,
                                       0,
                                       LV_SIZE_CONTENT,
                                       18,
                                       &lv_font_montserrat_14);
    s_ui.remaining_range_unit = label(range_group,
                                      0,
                                      0,
                                      LV_SIZE_CONTENT,
                                      LV_SIZE_CONTENT,
                                      &settings_zh_10);
    lv_label_set_text(s_ui.remaining_range_unit, "km");
    s_ui.bms_running_time = label(dynamic_layer,
                                  margin + metric_col_w * 2,
                                  metric_y + 24,
                                  metric_col_w,
                                  18,
                                  &settings_zh_13);
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        const int32_t col_x = (int32_t)(index % 2U) * safety_col_w;
        const int32_t row_y = 23 + (int32_t)(index / 2U) * safety_row_h;
        const int32_t text_y = row_y + (safety_row_h - safety_text_h) / 2;
        s_ui.bms_safety_values[index] = label(dynamic_layer,
                                              margin + col_x + safety_col_w - 42,
                                              safety_y + text_y,
                                              34,
                                              safety_text_h,
                                              &settings_zh_10);
        s_ui.bms_safety_checks[index] =
            bms_native_safety_check(dynamic_layer,
                                    margin + col_x + 10,
                                    safety_y + row_y + (safety_row_h - 12) / 2);
    }

    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.pack_voltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.current, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.capacity, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.remaining_range_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.bms_running_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.soc, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.pack_voltage, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.current, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.capacity, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_value, COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.remaining_range_unit, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.bms_running_time, COLOR_WHITE, LV_PART_MAIN);
    for (uint8_t index = 0U; index < DASHBOARD_CELL_STAT_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.cell_stat_values[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.cell_stat_values[index], COLOR_WHITE, LV_PART_MAIN);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.temperature_values[index], LV_TEXT_ALIGN_RIGHT,
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ui.temperature_values[index], COLOR_WHITE, LV_PART_MAIN);
    }
    for (uint8_t index = 0U; index < ESP_BMS_BMS_SAFETY_COUNT; ++index) {
        lv_obj_set_style_text_align(s_ui.bms_safety_values[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
}

static lv_draw_buf_t *dashboard_cell_key_draw_buf(uint8_t index)
{
    switch (index) {
    case 0:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 0U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_0_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 0U, true);
        }
        return &s_dashboard_cell_key_0_draw_buf;
    case 1:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 1U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_1_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 1U, true);
        }
        return &s_dashboard_cell_key_1_draw_buf;
    case 2:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 2U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_2_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 2U, true);
        }
        return &s_dashboard_cell_key_2_draw_buf;
    case 3:
    default:
        if (!ui_flag_get(s_dashboard_cell_key_draw_buf_initialized_flags, 3U)) {
            LV_DRAW_BUF_INIT_STATIC(s_dashboard_cell_key_3_draw_buf);
            ui_flag_set(&s_dashboard_cell_key_draw_buf_initialized_flags, 3U, true);
        }
        return &s_dashboard_cell_key_3_draw_buf;
    }
}

static void dashboard_cell_key_draw(lv_obj_t *canvas, uint8_t index)
{
    if (!canvas || index >= DASHBOARD_CELL_STAT_COUNT) {
        return;
    }

    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
    for (uint8_t y = 0; y < DASHBOARD_CELL_KEY_BITMAP_H; ++y) {
        for (uint8_t x = 0; x < DASHBOARD_CELL_KEY_BITMAP_W; ++x) {
            const uint16_t bit_index = ((uint16_t)y * DASHBOARD_CELL_KEY_BITMAP_W) + x;
            const uint8_t byte = DASHBOARD_CELL_STAT_KEY_BITMAPS[index][bit_index / 8U];
            const uint8_t mask = (uint8_t)(1U << (7U - (bit_index % 8U)));
            if ((byte & mask) != 0U) {
                lv_canvas_set_px(canvas, x, y, COLOR_TEXT, LV_OPA_COVER);
            }
        }
    }
}

lv_obj_t *dashboard_cell_key(lv_obj_t *parent, int32_t x, int32_t y, uint8_t index)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    clear_style(canvas);
    lv_canvas_set_draw_buf(canvas, dashboard_cell_key_draw_buf(index));
    lv_obj_set_pos(canvas, x, y);
    lv_obj_set_size(canvas, DASHBOARD_CELL_KEY_BITMAP_W, DASHBOARD_CELL_KEY_BITMAP_H);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, LV_PART_MAIN);
    dashboard_cell_key_draw(canvas, index);
    return canvas;
}

lv_color_t dashboard_soc_fill_color(uint8_t soc_percent, bool valid, bool charging)
{
    (void)charging;
    if (!valid) {
        return COLOR_PANEL_ALT;
    }
    if (soc_percent >= 60U) {
        return COLOR_STATUS_OK;
    }
    if (soc_percent >= 30U) {
        return COLOR_WARN;
    }
    return COLOR_BAD;
}
