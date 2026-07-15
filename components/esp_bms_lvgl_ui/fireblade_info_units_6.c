/*******************************************************************************
 * Size: 6 px
 * Bpp: 4
 * Opts: --font managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf --size 6 --bpp 4 --format lvgl --symbols=Wh/kmph --no-kerning --no-compress --lv-include lvgl.h --lv-font-name fireblade_info_units_6 --output components/esp_bms_lvgl_ui/fireblade_info_units_6.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FIREBLADE_INFO_UNITS_6
#define FIREBLADE_INFO_UNITS_6 1
#endif

#if FIREBLADE_INFO_UNITS_6

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002F "/" */
    0x0, 0x21, 0x0, 0x70, 0x0, 0x80, 0x4, 0x40,
    0x8, 0x0, 0x7, 0x0,

    /* U+0057 "W" */
    0x90, 0x1d, 0x2, 0x64, 0x56, 0x83, 0x80, 0x9,
    0x90, 0x88, 0x0, 0x86, 0x9, 0x50,

    /* U+0068 "h" */
    0x72, 0x0, 0x79, 0x94, 0x72, 0x8, 0x72, 0x9,

    /* U+006B "k" */
    0x72, 0x0, 0x72, 0x82, 0x7b, 0x70, 0x72, 0x74,

    /* U+006D "m" */
    0x79, 0x99, 0x87, 0x72, 0x28, 0x9, 0x72, 0x17,
    0x9,

    /* U+0070 "p" */
    0x79, 0x75, 0x72, 0x9, 0x79, 0x85, 0x72, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 34, .box_w = 4, .box_h = 6, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 12, .adv_w = 108, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 26, .adv_w = 65, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 34, .adv_w = 59, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 42, .adv_w = 101, .box_w = 6, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 51, .adv_w = 65, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = -1}
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
const lv_font_t fireblade_info_units_6 = {
#else
lv_font_t fireblade_info_units_6 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 6,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FIREBLADE_INFO_UNITS_6*/

