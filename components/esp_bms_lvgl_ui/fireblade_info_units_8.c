/*******************************************************************************
 * Size: 8 px
 * Bpp: 4
 * Opts: --font managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf --size 8 --bpp 4 --format lvgl --symbols=Wh/kmph --no-kerning --no-compress --lv-include lvgl.h --lv-font-name fireblade_info_units_8 --output components/esp_bms_lvgl_ui/fireblade_info_units_8.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FIREBLADE_INFO_UNITS_8
#define FIREBLADE_INFO_UNITS_8 1
#endif

#if FIREBLADE_INFO_UNITS_8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002F "/" */
    0x0, 0xa, 0x0, 0x2, 0x80, 0x0, 0x82, 0x0,
    0xa, 0x0, 0x4, 0x60, 0x0, 0x91, 0x0, 0x19,
    0x0, 0x0,

    /* U+0057 "W" */
    0x94, 0x0, 0xf1, 0x3, 0x83, 0xa0, 0x69, 0x70,
    0x92, 0xc, 0xb, 0xb, 0xb, 0x0, 0x79, 0x80,
    0x89, 0x60, 0x1, 0xf2, 0x2, 0xf1, 0x0,

    /* U+0068 "h" */
    0x48, 0x0, 0x4, 0x80, 0x0, 0x4c, 0x9b, 0x44,
    0x90, 0x1b, 0x48, 0x0, 0xc4, 0x80, 0xc,

    /* U+006B "k" */
    0x48, 0x0, 0x4, 0x80, 0x0, 0x48, 0xa, 0x34,
    0x9c, 0x30, 0x4d, 0x6a, 0x4, 0x80, 0x77,

    /* U+006D "m" */
    0x4c, 0x9b, 0x89, 0xb4, 0x49, 0x3, 0xb0, 0xb,
    0x48, 0x2, 0xa0, 0xc, 0x48, 0x2, 0xa0, 0xc,

    /* U+0070 "p" */
    0x4c, 0xab, 0x50, 0x4a, 0x0, 0xc0, 0x4a, 0x0,
    0xc0, 0x4c, 0xaa, 0x50, 0x48, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 45, .box_w = 5, .box_h = 7, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 18, .adv_w = 144, .box_w = 9, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 41, .adv_w = 87, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 79, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 135, .box_w = 8, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 87, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x28, 0x39, 0x3c, 0x3e, 0x41
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 47, .range_length = 66, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 6, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
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
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t fireblade_info_units_8 = {
#else
lv_font_t fireblade_info_units_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 7,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FIREBLADE_INFO_UNITS_8*/

