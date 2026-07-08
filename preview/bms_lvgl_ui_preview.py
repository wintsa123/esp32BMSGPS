# LVGL MicroPython preview for components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c.
import lvgl as lv


COLOR_BG = 0x080A0E
COLOR_PANEL = 0x121820
COLOR_PANEL_ALT = 0x162029
COLOR_SOC = 0x0568DE
COLOR_WHITE = 0xFFFFFF
COLOR_TEXT = 0xE8F1FF
COLOR_MUTED = 0xA9B4C8
COLOR_ACCENT = 0x74D6B5
COLOR_WARN = 0xFFC857
COLOR_BAD = 0xFF6B6B
SOC_WAVE_PERIOD = 32
SOC_WAVE_W = 192
SOC_WAVE_H = 18
BT_SYMBOL = "\ue728"
HOTSPOT_SYMBOL = "\ue62b"

QUICK_ITEMS = (
    ("bt", BT_SYMBOL, False),
    ("hotspot", HOTSPOT_SYMBOL, True),
    ("wifi", lv.SYMBOL.WIFI, False),
    ("rotate", lv.SYMBOL.LOOP, False),
    ("settings", lv.SYMBOL.SETTINGS, False),
)

SYMBOL_METRICS = {
    lv.SYMBOL.SETTINGS: (27, 5, 24, 24, 24, 0, -3),
    lv.SYMBOL.LOOP: (27, 5, 30, 31, 19, -1, -1),
    lv.SYMBOL.WIFI: (27, 5, 30, 30, 23, 0, -2),
    lv.SYMBOL.BLUETOOTH: (27, 5, 21, 19, 24, 1, -3),
    lv.SYMBOL.EDIT: (16, 3, 14, 15, 15, -1, -2),
}

BT_GLYPH_BITMAP = (
    0x00, 0xE0, 0x00, 0x78, 0x00, 0x3E, 0x00, 0x1F,
    0x8C, 0x0F, 0xE7, 0x07, 0x7B, 0xC3, 0x9E, 0xF1,
    0xC7, 0x3C, 0xE7, 0x8F, 0x77, 0x81, 0xFF, 0x80,
    0x7F, 0x80, 0x1F, 0x80, 0x07, 0x80, 0x07, 0xE0,
    0x0F, 0xF8, 0x0F, 0xFE, 0x0F, 0x77, 0x8F, 0x39,
    0xEF, 0x1C, 0x7F, 0x0E, 0x7F, 0x07, 0x7B, 0x03,
    0xF8, 0x01, 0xF8, 0x00, 0xF8, 0x00, 0x78, 0x00,
    0x38, 0x00,
)
BT_GLYPH_W = 17
BT_GLYPH_H = 27
BT_GLYPH_ADV_W = 30
BT_GLYPH_OFS_X = 7
BT_GLYPH_OFS_Y = -2
BT_FONT_LINE_H = 27
BT_FONT_BASE_LINE = 2
HOTSPOT_GLYPH_BITMAP = (
    0x10, 0x00, 0x00, 0x03, 0x80, 0x00, 0x1C, 0x30,
    0x00, 0x00, 0xC7, 0x18, 0x01, 0x8E, 0x63, 0x80,
    0x1C, 0x6E, 0x30, 0xF0, 0xC7, 0xE7, 0x1F, 0x8E,
    0x7C, 0x71, 0xF8, 0xE3, 0xE7, 0x1F, 0x8E, 0x7E,
    0x30, 0xF0, 0xC7, 0x63, 0x8F, 0x1C, 0x67, 0x18,
    0xF1, 0x8E, 0x38, 0x1F, 0x81, 0xC3, 0x81, 0xF8,
    0x1C, 0x00, 0x19, 0x80, 0x80, 0x03, 0x9C, 0x00,
    0x00, 0x3F, 0xC0, 0x00, 0x03, 0xFC, 0x00, 0x00,
    0x70, 0xE0, 0x00, 0x06, 0x06, 0x00, 0x00, 0x7F,
    0xE0, 0x00, 0x0F, 0xFF, 0x00, 0x00, 0xC0, 0x30,
    0x00, 0x1C, 0x03, 0x80, 0x01, 0x80, 0x38, 0x00,
    0x08, 0x01, 0x00,
)
HOTSPOT_GLYPH_W = 28
HOTSPOT_GLYPH_H = 26
HOTSPOT_GLYPH_ADV_W = 28
HOTSPOT_GLYPH_OFS_X = 0
HOTSPOT_GLYPH_OFS_Y = -2
HOTSPOT_FONT_LINE_H = 26
HOTSPOT_FONT_BASE_LINE = 2

_PT = getattr(lv, "point_precise_t", None)
if _PT is None:
    _PT = lv.point_t
_LINE_POINTS = []
_CANVAS_BUFS = []

CN_KEY_BITMAPS = {
    "最高": (28, 16, (
        0x3F, 0xF0, 0x08, 0x03, 0x01, 0x1F, 0xFE, 0x3F, 0xF0, 0x00, 0x03, 0x01,
        0x07, 0xF8, 0x3F, 0xF0, 0x40, 0x80, 0x00, 0x07, 0xF8, 0x7F, 0xF8, 0x00,
        0x02, 0x20, 0x0F, 0xFE, 0x3F, 0xF8, 0x80, 0x62, 0x29, 0x0B, 0xF6, 0x3E,
        0xB0, 0xA1, 0x62, 0x26, 0x0A, 0x16, 0x7E, 0xF0, 0xBF, 0x60, 0x30, 0x88,
        0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    )),
    "最低": (28, 16, (
        0x00, 0x00, 0x00, 0x03, 0xFF, 0x02, 0x00, 0x30, 0x10, 0x43, 0xC3, 0xFF,
        0x05, 0xD0, 0x30, 0x10, 0x91, 0x03, 0xFF, 0x19, 0x10, 0x00, 0x03, 0x9F,
        0xE7, 0xFF, 0x89, 0x10, 0x22, 0x00, 0x91, 0x03, 0xFF, 0x89, 0x08, 0x22,
        0x90, 0x90, 0x83, 0xEB, 0x09, 0xE9, 0x22, 0x60, 0xB8, 0x57, 0xEF, 0x08,
        0x06, 0x03, 0x08, 0x9F, 0x80, 0x00, 0x00, 0x00,
    )),
    "压差": (28, 16, (
        0x00, 0x00, 0x41, 0x83, 0xFF, 0x82, 0x10, 0x20, 0x01, 0xFF, 0xE2, 0x18,
        0x00, 0xC0, 0x21, 0x80, 0xFF, 0xC2, 0x18, 0x00, 0xC0, 0x2F, 0xF8, 0x0C,
        0x02, 0x18, 0x1F, 0xFE, 0x21, 0xA0, 0x60, 0x04, 0x1B, 0x05, 0xFC, 0x41,
        0x90, 0x42, 0x04, 0x18, 0x0C, 0x20, 0x5F, 0xF8, 0x82, 0x00, 0x00, 0x17,
        0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    )),
    "平均": (28, 16, (
        0x00, 0x00, 0xC4, 0x07, 0xFF, 0x8C, 0x40, 0x03, 0x00, 0xCC, 0x01, 0x33,
        0x0C, 0xFE, 0x13, 0x21, 0xF8, 0x21, 0x32, 0x0D, 0x02, 0x0B, 0x40, 0xCF,
        0x20, 0x30, 0x0C, 0x02, 0x7F, 0xF8, 0xE0, 0x20, 0x30, 0x0E, 0x1A, 0x03,
        0x01, 0x8E, 0x20, 0x30, 0x00, 0x82, 0x03, 0x00, 0x00, 0x60, 0x30, 0x00,
        0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    )),
}


def _clean(obj):
    try:
        obj.remove_style_all()
    except AttributeError:
        pass
    try:
        obj.clear_flag(lv.obj.FLAG.SCROLLABLE)
    except AttributeError:
        pass


def _panel(parent, x, y, w, h, color):
    obj = lv.obj(parent)
    _clean(obj)
    obj.set_pos(int(x), int(y))
    obj.set_size(int(w), int(h))
    obj.set_style_radius(6, 0)
    obj.set_style_bg_color(lv.color_hex(color), 0)
    obj.set_style_bg_opa(lv.OPA.COVER, 0)
    obj.set_style_pad_all(4, 0)
    return obj


def _solid_rect(parent, x, y, w, h, color):
    obj = lv.obj(parent)
    _clean(obj)
    obj.set_pos(int(x), int(y))
    obj.set_size(int(w), int(h))
    obj.set_style_radius(0, 0)
    obj.set_style_bg_color(lv.color_hex(color), 0)
    obj.set_style_bg_opa(lv.OPA.COVER, 0)
    obj.set_style_pad_all(0, 0)
    return obj


def _label(parent, x, y, w, h, text, align=None, color=COLOR_TEXT, font=None):
    obj = lv.label(parent)
    _clean(obj)
    obj.set_pos(int(x), int(y))
    obj.set_size(int(w), int(h))
    obj.set_text(text)
    obj.set_style_text_color(lv.color_hex(color), 0)
    if font is not None:
        obj.set_style_text_font(font, 0)
    try:
        obj.set_long_mode(lv.label.LONG_MODE.CLIP)
    except AttributeError:
        try:
            obj.set_long_mode(lv.label.LONG.CLIP)
        except AttributeError:
            pass
    if align is not None:
        obj.set_style_text_align(align, 0)
    return obj


def _symbol_icon(parent, content_w, content_h, text, color=COLOR_TEXT, font=None, line_height=27):
    label_x = 0
    label_h = line_height
    label_y = (content_h - label_h) // 2
    metrics = SYMBOL_METRICS.get(text)
    if metrics is not None:
        label_h, base_line, adv_w, box_w, box_h, ofs_x, ofs_y = metrics
        glyph_center_x2 = content_w - adv_w + 2 * ofs_x + box_w
        glyph_top = label_h - base_line - box_h - ofs_y
        glyph_center_y2 = 2 * glyph_top + box_h
        label_x = int((content_w - glyph_center_x2) / 2)
        label_y = int((content_h - glyph_center_y2) / 2)
    obj = _label(parent, label_x, label_y, content_w, label_h,
                 text, lv.TEXT_ALIGN.CENTER, color, font)
    try:
        obj.set_style_bg_opa(lv.OPA.TRANSP, 0)
    except AttributeError:
        pass
    return obj


def _line(parent, points, color, width=4, canvas_w=44, canvas_h=42, opa=None):
    obj = lv.line(parent)
    _clean(obj)
    obj.set_pos(0, 0)
    obj.set_size(canvas_w, canvas_h)
    pts = [_PT({"x": int(x), "y": int(y)}) for x, y in points]
    _LINE_POINTS.append(pts)
    obj.set_points(pts, len(pts))
    obj.set_style_line_width(int(width), 0)
    obj.set_style_line_color(lv.color_hex(color), 0)
    if opa is None:
        opa = lv.OPA.COVER
    obj.set_style_line_opa(opa, 0)
    obj.set_style_line_rounded(True, 0)
    try:
        obj.set_style_bg_opa(lv.OPA.TRANSP, 0)
    except AttributeError:
        pass
    return obj


def _soc_wave(parent, x, y):
    points = (
        (0, 9), (4, 5), (8, 3), (12, 5), (16, 9),
        (20, 13), (24, 15), (28, 13), (32, 9),
        (36, 5), (40, 3), (44, 5), (48, 9),
        (52, 13), (56, 15), (60, 13), (64, 9),
        (68, 5), (72, 3), (76, 5), (80, 9),
        (84, 13), (88, 15), (92, 13), (96, 9),
        (100, 5), (104, 3), (108, 5), (112, 9),
        (116, 13), (120, 15), (124, 13), (128, 9),
        (132, 5), (136, 3), (140, 5), (144, 9),
        (148, 13), (152, 15), (156, 13), (160, 9),
        (164, 5), (168, 3), (172, 5), (176, 9),
        (180, 13), (184, 15), (188, 13), (192, 9),
    )
    obj = _line(parent, points, COLOR_WHITE, width=3, canvas_w=SOC_WAVE_W,
                canvas_h=SOC_WAVE_H, opa=153)
    obj.set_pos(int(x), int(y))
    return obj


def _canvas(parent, w, h):
    obj = lv.canvas(parent)
    _clean(obj)
    buf = bytearray(lv.canvas.buf_size(w, h, 32, 4))
    _CANVAS_BUFS.append(buf)
    obj.set_buffer(buf, w, h, lv.COLOR_FORMAT.ARGB8888)
    obj.set_size(w, h)
    obj.set_style_bg_opa(lv.OPA.TRANSP, 0)
    obj.fill_bg(lv.color_hex(0), lv.OPA.TRANSP)
    return obj


def _canvas_dot(canvas, canvas_w, canvas_h, cx, cy, radius, color):
    radius_sq = radius * radius
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy > radius_sq:
                continue
            x = cx + dx
            y = cy + dy
            if 0 <= x < canvas_w and 0 <= y < canvas_h:
                canvas.set_px(x, y, lv.color_hex(color), lv.OPA.COVER)


def _canvas_bitmap_1bpp(canvas, bitmap, bitmap_w, bitmap_h, x0, y0, color):
    for y in range(bitmap_h):
        for x in range(bitmap_w):
            bit_index = y * bitmap_w + x
            byte = bitmap[bit_index // 8]
            mask = 1 << (7 - (bit_index % 8))
            if byte & mask:
                canvas.set_px(x0 + x, y0 + y, lv.color_hex(color), lv.OPA.COVER)


def _cn_key(parent, x, y, text, color=COLOR_TEXT):
    bitmap_w, bitmap_h, bitmap = CN_KEY_BITMAPS[text]
    canvas = _canvas(parent, bitmap_w, bitmap_h)
    canvas.set_pos(int(x), int(y))
    _canvas_bitmap_1bpp(canvas, bitmap, bitmap_w, bitmap_h, 0, 0, color)
    return canvas


def _bluetooth_icon(parent, w, h, active=False, color=None):
    if color is None:
        color = COLOR_SOC if active else COLOR_TEXT
    content_w = w - 8
    content_h = h - 8
    glyph_top = BT_FONT_LINE_H - BT_FONT_BASE_LINE - BT_GLYPH_H - BT_GLYPH_OFS_Y
    glyph_center_y2 = 2 * glyph_top + BT_GLYPH_H
    label_y = int((content_h - glyph_center_y2) / 2)
    glyph_x = int((content_w - BT_GLYPH_ADV_W) / 2) + BT_GLYPH_OFS_X
    glyph_y = glyph_top

    canvas = _canvas(parent, content_w, BT_FONT_LINE_H)
    canvas.set_pos(0, label_y)
    _canvas_bitmap_1bpp(canvas,
                        BT_GLYPH_BITMAP,
                        BT_GLYPH_W,
                        BT_GLYPH_H,
                        glyph_x,
                        glyph_y,
                        color)


def _panel_label(parent, x, y, w, h, color, text, align=None, text_color=COLOR_TEXT):
    box = _panel(parent, x, y, w, h, color)
    return _label(box, 4, 4, w - 8, h - 8, text, align, text_color)


def _hotspot_icon(parent, w, h, color=COLOR_TEXT):
    content_w = w - 8
    content_h = h - 8
    glyph_top = HOTSPOT_FONT_LINE_H - HOTSPOT_FONT_BASE_LINE - HOTSPOT_GLYPH_H - HOTSPOT_GLYPH_OFS_Y
    glyph_center_y2 = 2 * glyph_top + HOTSPOT_GLYPH_H
    label_y = int((content_h - glyph_center_y2) / 2)
    glyph_x = int((content_w - HOTSPOT_GLYPH_ADV_W) / 2) + HOTSPOT_GLYPH_OFS_X
    glyph_y = glyph_top

    canvas = _canvas(parent, content_w, HOTSPOT_FONT_LINE_H)
    canvas.set_pos(0, label_y)
    _canvas_bitmap_1bpp(canvas,
                        HOTSPOT_GLYPH_BITMAP,
                        HOTSPOT_GLYPH_W,
                        HOTSPOT_GLYPH_H,
                        glyph_x,
                        glyph_y,
                        color)


def _quick_axis_value(value, default_vertical):
    if value is None:
        return default_vertical
    if isinstance(value, bool):
        return value
    text = str(value).lower()
    return text in ("vertical", "v", "column", "cols", "true", "1")


def _quick_orientation_value(portrait, common_value, portrait_value, landscape_value,
                             default_vertical):
    value = portrait_value if portrait else landscape_value
    if value is None:
        value = common_value
    return _quick_axis_value(value, default_vertical)


def _quick_default_layout(w, h, tools_vertical):
    portrait = w < h
    gap = 8
    layout = {"items": []}
    if tools_vertical:
        tool_w = 104 if portrait else 126
        tool_h = 56
        icon_w = 72 if portrait else 52
        icon_h = 42 if portrait else 32
        grid_w = tool_w + gap + icon_w
        grid_h = len(QUICK_ITEMS) * icon_h + (len(QUICK_ITEMS) - 1) * gap
        left = (w - grid_w) // 2
        top = (h - grid_h) // 2
        icon_x = left + tool_w + gap
        layout["brightness"] = [left, top, tool_w, tool_h]
        layout["volume"] = [left, top + tool_h + gap, tool_w, tool_h]
        for index in range(len(QUICK_ITEMS)):
            layout["items"].append([icon_x, top + index * (icon_h + gap), icon_w, icon_h])
        return layout

    cols = len(QUICK_ITEMS)
    rows = 2
    pad = 16
    tile_h = 56 if portrait else 64
    tile_w = (w - pad * 2 - (cols - 1) * gap) // cols
    grid_w = cols * tile_w + (cols - 1) * gap
    grid_h = rows * tile_h + (rows - 1) * gap
    left = (w - grid_w) // 2
    top = (h - grid_h) // 2
    control_w = (grid_w - gap) // 2
    layout["brightness"] = [left, top, control_w, tile_h]
    layout["volume"] = [left + control_w + gap, top, control_w, tile_h]
    for index in range(len(QUICK_ITEMS)):
        layout["items"].append([left + index * (tile_w + gap), top + tile_h + gap, tile_w, tile_h])
    return layout


def _quick_offset_for(name, offsets):
    if not offsets:
        return None
    try:
        return offsets.get(name)
    except AttributeError:
        return None


def _quick_apply_offset(rect, offset):
    if offset is None:
        return
    try:
        rect[0] += int(offset[0])
        rect[1] += int(offset[1])
    except (IndexError, TypeError, ValueError):
        pass


def _quick_orientation_offsets(portrait, common_offsets, portrait_offsets, landscape_offsets):
    offsets = {}
    if common_offsets:
        try:
            offsets.update(common_offsets)
        except (AttributeError, TypeError, ValueError):
            pass
    scoped = portrait_offsets if portrait else landscape_offsets
    if scoped:
        try:
            offsets.update(scoped)
        except (AttributeError, TypeError, ValueError):
            pass
    return offsets


def _quick_layout(w, h, tool_axis=None, portrait_tool_axis=None, landscape_tool_axis=None,
                  icon_offsets=None, portrait_icon_offsets=None, landscape_icon_offsets=None,
                  tool_offsets=None, portrait_tool_offsets=None, landscape_tool_offsets=None):
    portrait = w < h
    tools_vertical = _quick_orientation_value(portrait,
                                              tool_axis,
                                              portrait_tool_axis,
                                              landscape_tool_axis,
                                              portrait)
    layout = _quick_default_layout(w, h, tools_vertical)

    active_tool_offsets = _quick_orientation_offsets(portrait,
                                                    tool_offsets,
                                                    portrait_tool_offsets,
                                                    landscape_tool_offsets)
    _quick_apply_offset(layout["brightness"], _quick_offset_for("brightness", active_tool_offsets))
    _quick_apply_offset(layout["volume"], _quick_offset_for("volume", active_tool_offsets))

    active_icon_offsets = _quick_orientation_offsets(portrait,
                                                    icon_offsets,
                                                    portrait_icon_offsets,
                                                    landscape_icon_offsets)
    for index, (name, _icon, _is_hotspot) in enumerate(QUICK_ITEMS):
        _quick_apply_offset(layout["items"][index], _quick_offset_for(name, active_icon_offsets))
    return layout


def _dashboard_text(sample, soc_percent=72, charging=True, bms_mode="ok"):
    if sample:
        soc_percent = max(0, min(100, int(soc_percent)))
        if bms_mode == "warning":
            bms_text = "BMS INFO\nWARN OV"
            bms_color = COLOR_BAD
        elif bms_mode == "info":
            bms_text = "BMS INFO\nRX"
            bms_color = COLOR_WARN
        else:
            bms_text = "BMS INFO\nOK"
            bms_color = COLOR_ACCENT
        return {
            "soc": ("%d%%" % soc_percent, "35.20/50.00Ah"),
            "soc_percent": soc_percent,
            "soc_valid": True,
            "charging": charging,
            "pack": ("52.4V", "+12.3A"),
            "bms": bms_text,
            "bms_color": bms_color,
            "cell": ("3.456", "3.421", "35mV", "3.440"),
            "temp": ("25C", "26C", "--", "--", "28C", "32C"),
        }
    return {
        "soc": ("--", "--/--Ah"),
        "soc_percent": 0,
        "soc_valid": False,
        "charging": False,
        "pack": ("--", "--"),
        "bms": "BMS INFO\nBMS OFF",
        "bms_color": COLOR_WARN,
        "cell": ("--", "--", "--", "--"),
        "temp": ("--", "--", "--", "--", "--", "--"),
    }


def _soc_fill_color(soc_percent, valid, charging):
    if not valid:
        return COLOR_PANEL_ALT
    if charging or soc_percent >= 100:
        return COLOR_ACCENT
    if soc_percent <= 20:
        return COLOR_BAD
    return COLOR_SOC


def _draw_soc_tile(scr, x, y, w, h, soc_text, soc_percent, valid, charging, wave_offset=0):
    box = _panel(scr, x, y, w, h, COLOR_PANEL)
    try:
        box.set_style_clip_corner(True, 0)
    except AttributeError:
        pass
    soc_percent = max(0, min(100, int(soc_percent))) if valid else 0
    fill_color = _soc_fill_color(soc_percent, valid, charging)
    if w > h:
        fill_w = (w * soc_percent) // 100
        if fill_w > 0:
            fill = _solid_rect(box, 0, 0, fill_w, h, fill_color)
        else:
            fill = None
        if charging and fill_w >= 32:
            _soc_wave(fill, -SOC_WAVE_PERIOD + (int(wave_offset) % SOC_WAVE_PERIOD),
                      (h - SOC_WAVE_H) // 2)
        percent_y = 25
        ah_y = 54
    else:
        fill_h = (h * soc_percent) // 100
        if fill_h > 0:
            fill = _solid_rect(box, 0, h - fill_h, w, fill_h, fill_color)
        else:
            fill = None
        if charging and fill_h >= 24:
            span = fill_h + SOC_WAVE_H
            wave_y = fill_h - 2 - (int(wave_offset) % span)
            _soc_wave(fill, -SOC_WAVE_PERIOD // 2, wave_y)
        percent_y = 38
        ah_y = 70
    _label(box, 4, percent_y, w - 8, 30, soc_text[0],
           lv.TEXT_ALIGN.CENTER, COLOR_TEXT, lv.font_montserrat_24)
    _label(box, 4, ah_y, w - 8, 20, soc_text[1], lv.TEXT_ALIGN.CENTER)


def _draw_pack_tile(scr, x, y, w, h, pack_text):
    box = _panel(scr, x, y, w, h, COLOR_PANEL)
    if h >= 100:
        voltage_y = 26
        current_y = 62
    else:
        voltage_y = 15
        current_y = 47
    _label(box, 4, voltage_y, w - 8, 30, pack_text[0],
           lv.TEXT_ALIGN.CENTER, COLOR_TEXT, lv.font_montserrat_24)
    _label(box, 4, current_y, w - 8, 30, pack_text[1],
           lv.TEXT_ALIGN.CENTER, COLOR_TEXT, lv.font_montserrat_24)


def _draw_cell_stats_tile(scr, x, y, w, h, values):
    box = _panel(scr, x, y, w, h, COLOR_PANEL)
    keys = ("最高", "最低", "压差", "平均")
    if h >= 100:
        row_h = 20
        row_gap = 26
        row_top = 6
        value_x = 55
        value_w = 42
        key_x = 11
    else:
        row_h = 16
        row_gap = 16
        row_top = 2
        value_x = 86
        value_w = 42
        key_x = 20
    for index, key in enumerate(keys):
        row_y = row_top + index * row_gap
        _cn_key(box, key_x, row_y + ((row_h - 16) // 2), key)
        _label(box, value_x, row_y, value_w, row_h,
               values[index], lv.TEXT_ALIGN.RIGHT)


def _draw_temperature_tile(scr, x, y, w, h, values):
    box = _panel(scr, x, y, w, h, COLOR_PANEL)
    keys = ("T1", "T2", "T3", "T4", "BAL", "MOS")
    col_w = w // len(keys)
    left = 0 if w < 260 else (w - col_w * len(keys)) // 2
    title_y = 7 if h >= 56 else 6
    value_y = 31 if h >= 56 else 29
    for index, key in enumerate(keys):
        col_x = left + index * col_w
        _label(box, col_x, title_y, col_w, 18, key, lv.TEXT_ALIGN.CENTER)
        _label(box, col_x, value_y, col_w, 18, values[index], lv.TEXT_ALIGN.CENTER)


def _draw_dashboard(scr, w, h, sample=False, soc_percent=72, charging=True,
                    bms_mode="ok", wave_offset=0):
    portrait = w < h
    text = _dashboard_text(sample, soc_percent, charging, bms_mode)
    if portrait:
        content_w = w - 16
        _draw_soc_tile(scr, 8, 8, 108, 112,
                       text["soc"], text["soc_percent"], text["soc_valid"], text["charging"],
                       wave_offset)
        _draw_pack_tile(scr, 124, 8, 108, 112, text["pack"])
        _panel_label(scr, 8, 128, 108, 120, COLOR_PANEL,
                     text["bms"], lv.TEXT_ALIGN.CENTER, text["bms_color"])
        _draw_cell_stats_tile(scr, 124, 128, 108, 120, text["cell"])
        _draw_temperature_tile(scr, 8, 256, content_w, 56, text["temp"])
    else:
        _draw_soc_tile(scr, 8, 8, 148, 84,
                       text["soc"], text["soc_percent"], text["soc_valid"], text["charging"],
                       wave_offset)
        _draw_pack_tile(scr, 164, 8, 148, 84, text["pack"])
        _panel_label(scr, 8, 100, 148, 70, COLOR_PANEL,
                     text["bms"], lv.TEXT_ALIGN.CENTER, text["bms_color"])
        _draw_cell_stats_tile(scr, 164, 100, 148, 70, text["cell"])
        _draw_temperature_tile(scr, 8, 178, 304, 54, text["temp"])


def _draw_pull_handle(scr, w):
    pull_w = 116 if w < 320 else 140
    zone = lv.obj(scr)
    _clean(zone)
    zone.set_pos((w - pull_w) // 2, 0)
    zone.set_size(pull_w, 34)
    zone.set_style_bg_opa(lv.OPA.TRANSP, 0)
    handle = lv.obj(zone)
    _clean(handle)
    handle.set_pos((pull_w - 36) // 2, 3)
    handle.set_size(36, 4)
    handle.set_style_radius(2, 0)
    handle.set_style_bg_color(lv.color_hex(COLOR_MUTED), 0)
    handle.set_style_bg_opa(lv.OPA.COVER, 0)


def _draw_level_tile(parent, x, y, w, h, icon, value=85, value_min=0, vertical=False):
    _ = (value, value_min, vertical)
    box = _panel(parent, x, y, w, h, COLOR_PANEL_ALT)
    _symbol_icon(box, w - 8, h - 8, icon, COLOR_TEXT, lv.font_montserrat_24)


def _draw_quick_panel(scr, w, h, brightness=85, volume=65,
                      bt=False, hotspot=True, wifi=False, edit=False, vertical=False,
                      tool_axis=None, portrait_tool_axis=None, landscape_tool_axis=None,
                      icon_offsets=None, portrait_icon_offsets=None, landscape_icon_offsets=None,
                      tool_offsets=None, portrait_tool_offsets=None, landscape_tool_offsets=None,
                      pressed=None):
    layout = _quick_layout(w,
                           h,
                           tool_axis,
                           portrait_tool_axis,
                           landscape_tool_axis,
                           icon_offsets,
                           portrait_icon_offsets,
                           landscape_icon_offsets,
                           tool_offsets,
                           portrait_tool_offsets,
                           landscape_tool_offsets)

    bx, by, bw, bh = layout["brightness"]
    vx, vy, vw, vh = layout["volume"]
    _draw_level_tile(scr, bx, by, bw, bh,
                     lv.SYMBOL.EYE_OPEN, brightness, 10, vertical)
    _draw_level_tile(scr, vx, vy, vw, vh,
                     lv.SYMBOL.VOLUME_MID, volume, 0, vertical)

    edit_box = _panel(scr, w - 34, 8, 26, 24, COLOR_PANEL_ALT)
    edit_box.set_style_border_width(1 if edit else 0, 0)
    edit_box.set_style_border_color(lv.color_hex(COLOR_SOC), 0)
    edit_box.set_style_border_opa(lv.OPA.COVER, 0)
    _symbol_icon(edit_box, 18, 16, lv.SYMBOL.EDIT, COLOR_SOC if edit else COLOR_MUTED,
                 lv.font_montserrat_14, 16)

    active = {"bt": bt, "hotspot": hotspot, "wifi": wifi}
    for index, (name, icon, is_hotspot) in enumerate(QUICK_ITEMS):
        x, y, item_w, item_h = layout["items"][index]
        if pressed == name:
            x += 2
            y += 2
            item_w -= 4
            item_h -= 4
        color = COLOR_SOC if active.get(name, False) else COLOR_TEXT
        box = _panel(scr, x, y, item_w, item_h, COLOR_PANEL_ALT)
        if name == "bt":
            _bluetooth_icon(box, item_w, item_h, active.get(name, False))
        elif is_hotspot:
            _hotspot_icon(box, item_w, item_h, color)
        else:
            _symbol_icon(box, item_w - 8, item_h - 8, icon, color, lv.font_montserrat_24)


def _draw_settings(scr, w, h):
    portrait = w < h
    back = _panel(scr, 8, 8, 54, 24, COLOR_PANEL_ALT)
    back.set_style_radius(8, 0)
    _label(back, 6, 4, 42, 16, "BACK", lv.TEXT_ALIGN.CENTER, COLOR_ACCENT)
    _label(scr, 72, 10, w - 80, 20, "SETTINGS", None, COLOR_TEXT)

    charge = getattr(lv.SYMBOL, "CHARGE", "B")
    options = (
        ("wifi", lv.SYMBOL.WIFI, "WIFI", "STA NETWORK"),
        ("hotspot", HOTSPOT_SYMBOL, "HOTSPOT", "SETUP AP"),
        ("bt", BT_SYMBOL, "BT", "BLE RADIO"),
        ("symbol", charge, "BMS", "PROTECT BOARD"),
        ("symbol", lv.SYMBOL.SETTINGS, "SYSTEM", "DISPLAY"),
        ("symbol", "i", "ABOUT", "DEVICE"),
    )
    card_x = 8
    card_w = w - 16
    card_h = 50 if portrait else 44
    gap = 8
    first_y = 42
    for index, (kind, icon, title, subtitle) in enumerate(options):
        y = first_y + index * (card_h + gap)
        box = _panel(scr, card_x, y, card_w, card_h, COLOR_PANEL)
        box.set_style_radius(8, 0)
        box.set_style_border_width(1, 0)
        box.set_style_border_color(lv.color_hex(COLOR_PANEL_ALT), 0)
        box.set_style_border_opa(lv.OPA.COVER, 0)
        icon_box = lv.obj(box)
        _clean(icon_box)
        icon_box.set_pos(10, 7)
        icon_box.set_size(34, card_h - 14)
        icon_box.set_style_bg_opa(lv.OPA.TRANSP, 0)
        if kind == "bt":
            _bluetooth_icon(icon_box, 42, card_h - 6, color=COLOR_ACCENT)
        elif kind == "hotspot":
            _hotspot_icon(icon_box, 42, card_h - 6, COLOR_ACCENT)
        else:
            _symbol_icon(icon_box, 34, card_h - 14, icon, COLOR_ACCENT,
                         lv.font_montserrat_24)
        _label(box, 54, 7, card_w - 84, 18, title, None, COLOR_TEXT)
        _label(box, 54, 27, card_w - 84, 16, subtitle, None, COLOR_MUTED)
        if index < 4:
            _label(box, card_w - 22, (card_h - 16) // 2, 14, 16, ">",
                   lv.TEXT_ALIGN.CENTER, COLOR_MUTED)


def build(scr, view="home", sample=False, brightness=85, volume=65,
          bt=False, hotspot=True, wifi=False, edit=False, vertical=False,
          soc_percent=72, charging=True, bms_mode="ok", wave_offset=0,
          tool_axis=None, portrait_tool_axis=None, landscape_tool_axis=None,
          icon_offsets=None, portrait_icon_offsets=None, landscape_icon_offsets=None,
          tool_offsets=None, portrait_tool_offsets=None, landscape_tool_offsets=None,
          pressed=None):
    try:
        w = scr.get_width()
        h = scr.get_height()
    except AttributeError:
        w = 240
        h = 320
    scr.set_style_bg_color(lv.color_hex(COLOR_BG), 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)
    if view == "quick":
        _draw_quick_panel(scr,
                          w,
                          h,
                          brightness,
                          volume,
                          bt,
                          hotspot,
                          wifi,
                          edit,
                          vertical,
                          tool_axis,
                          portrait_tool_axis,
                          landscape_tool_axis,
                          icon_offsets,
                          portrait_icon_offsets,
                          landscape_icon_offsets,
                          tool_offsets,
                          portrait_tool_offsets,
                          landscape_tool_offsets,
                          pressed)
    elif view == "settings":
        scr.clean()
        scr.set_style_bg_color(lv.color_hex(COLOR_BG), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        _draw_settings(scr, w, h)
    else:
        _draw_dashboard(scr, w, h, sample, soc_percent, charging, bms_mode, wave_offset)
        _draw_pull_handle(scr, w)
