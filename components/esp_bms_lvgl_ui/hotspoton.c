/*******************************************************************************
 * Size: 30 x 30 px
 * Bpp: 1
 * Source: base-station PNG alpha mask; generated as LVGL font glyph U+E729.
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef HOTSPOTON
#define HOTSPOTON 1
#endif

#if HOTSPOTON

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E729 */
    0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x60,
    0x38, 0x00, 0x00, 0x70, 0x78, 0x00, 0x00, 0x78,
    0x71, 0xc0, 0x0e, 0x38, 0xf3, 0xc0, 0x0f, 0x3c,
    0xe3, 0x87, 0x87, 0x1c, 0xe7, 0x8f, 0xc3, 0x9c,
    0xe7, 0x1f, 0xc3, 0x9c, 0xe7, 0x1f, 0xc3, 0x9c,
    0xe7, 0x8f, 0xc7, 0x9c, 0xe3, 0x87, 0x87, 0x1c,
    0xf3, 0xc7, 0x8f, 0x3c, 0x71, 0xcf, 0xce, 0x38,
    0x78, 0x0f, 0xc0, 0x78, 0x38, 0x0f, 0xc0, 0x70,
    0x18, 0x1c, 0xe0, 0x60, 0x00, 0x1c, 0xe0, 0x00,
    0x00, 0x3f, 0xf0, 0x00, 0x00, 0x3f, 0xf0, 0x00,
    0x00, 0x38, 0x70, 0x00, 0x00, 0x70, 0x38, 0x00,
    0x00, 0x7f, 0xf8, 0x00, 0x00, 0x7f, 0xf8, 0x00,
    0x00, 0xff, 0xfc, 0x00, 0x00, 0xe0, 0x1c, 0x00,
    0x01, 0xe0, 0x1c, 0x00, 0x01, 0xc0, 0x0e, 0x00,
    0x00, 0xc0, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 480, .box_w = 30, .box_h = 30, .ofs_x = 0, .ofs_y = 0}
};

static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 59177, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

#if LVGL_VERSION_MAJOR == 8
static lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};

extern const lv_font_t hotspoton;

#if LVGL_VERSION_MAJOR >= 8
const lv_font_t hotspoton = {
#else
lv_font_t hotspoton = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 30,
    .base_line = 0,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,
    .user_data = NULL,
};

#endif /* HOTSPOTON */
