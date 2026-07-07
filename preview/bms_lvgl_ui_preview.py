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

QUICK_ITEMS = (
    ("bt", lv.SYMBOL.BLUETOOTH, False),
    ("hotspot", "", True),
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

BT_ICON_W = 26
BT_ICON_H = 32

_PT = getattr(lv, "point_precise_t", None)
if _PT is None:
    _PT = lv.point_t
_LINE_POINTS = []


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


def _line(parent, points, color, width=4):
    obj = lv.line(parent)
    _clean(obj)
    obj.set_pos(0, 0)
    obj.set_size(BT_ICON_W, BT_ICON_H)
    pts = [_PT({"x": int(x), "y": int(y)}) for x, y in points]
    _LINE_POINTS.append(pts)
    obj.set_points(pts, len(pts))
    obj.set_style_line_width(int(width), 0)
    obj.set_style_line_color(lv.color_hex(color), 0)
    obj.set_style_line_opa(lv.OPA.COVER, 0)
    obj.set_style_line_rounded(True, 0)
    try:
        obj.set_style_bg_opa(lv.OPA.TRANSP, 0)
    except AttributeError:
        pass
    return obj


def _bluetooth_icon(parent, w, h, active=False):
    color = COLOR_SOC if active else COLOR_WHITE
    content_w = w - 8
    content_h = h - 8
    root = lv.obj(parent)
    _clean(root)
    root.set_pos(int((content_w - BT_ICON_W) / 2), int((content_h - BT_ICON_H) / 2))
    root.set_size(BT_ICON_W, BT_ICON_H)
    root.set_style_bg_opa(lv.OPA.TRANSP, 0)
    _line(root, ((13, 3), (13, 29)), color)
    _line(root, ((5, 10), (13, 16), (21, 8), (13, 3)), color)
    _line(root, ((5, 22), (13, 16), (21, 24), (13, 29)), color)


def _panel_label(parent, x, y, w, h, color, text, align=None, text_color=COLOR_TEXT):
    box = _panel(parent, x, y, w, h, color)
    return _label(box, 4, 4, w - 8, h - 8, text, align, text_color)


def _hotspot_icon(parent, w, h, color=COLOR_TEXT):
    content_w = w - 8
    content_h = h - 8
    root = lv.obj(parent)
    _clean(root)
    root.set_pos(int((content_w - 44) / 2), int((content_h - 42) / 2))
    root.set_size(44, 42)
    root.set_style_bg_opa(lv.OPA.TRANSP, 0)
    _symbol_icon(root, 44, 22, lv.SYMBOL.WIFI, color, lv.font_montserrat_24)
    dot = lv.obj(root)
    _clean(dot)
    dot.set_pos(19, 20)
    dot.set_size(6, 6)
    dot.set_style_radius(3, 0)
    dot.set_style_bg_color(lv.color_hex(color), 0)
    dot.set_style_bg_opa(lv.OPA.COVER, 0)
    base = lv.obj(root)
    _clean(base)
    base.set_pos(4, 28)
    base.set_size(36, 12)
    base.set_style_radius(3, 0)
    base.set_style_bg_color(lv.color_hex(color), 0)
    base.set_style_bg_opa(lv.OPA.COVER, 0)


def _dashboard_text(sample):
    if sample:
        return {
            "soc": "72%\n35.20/50.00Ah",
            "pack": "PACK\n52.4V\n+12.3A",
            "bms": "BMS INFO\nBMS ON",
            "bms_color": COLOR_ACCENT,
            "cell": "MAX  3.456\nMIN  3.421\nDIFF 35mV\nAVG  3.440",
            "temp": "T1   T2   T3   T4   BAL  MOS\n25C  26C  --   --   28C  32C",
        }
    return {
        "soc": "--\n--/--Ah",
        "pack": "PACK\n--V\n--",
        "bms": "BMS INFO\nBMS OFF",
        "bms_color": COLOR_WARN,
        "cell": "MAX  --\nMIN  --\nDIFF --\nAVG  --",
        "temp": "T1   T2   T3   T4   BAL  MOS\n--   --   --   --   --   --",
    }


def _draw_dashboard(scr, w, h, sample=False):
    portrait = w < h
    text = _dashboard_text(sample)
    if portrait:
        content_w = w - 16
        _panel_label(scr, 8, 8, 108, 112, COLOR_SOC,
                     text["soc"], lv.TEXT_ALIGN.CENTER)
        _panel_label(scr, 124, 8, 108, 112, COLOR_PANEL,
                     text["pack"], lv.TEXT_ALIGN.CENTER)
        _panel_label(scr, 8, 128, 108, 120, COLOR_PANEL,
                     text["bms"], lv.TEXT_ALIGN.CENTER, text["bms_color"])
        _panel_label(scr, 124, 128, 108, 120, COLOR_PANEL,
                     text["cell"])
        _panel_label(scr, 8, 256, content_w, 56, COLOR_PANEL,
                     text["temp"],
                     lv.TEXT_ALIGN.CENTER)
    else:
        _panel_label(scr, 8, 8, 148, 84, COLOR_SOC,
                     text["soc"], lv.TEXT_ALIGN.CENTER)
        _panel_label(scr, 164, 8, 148, 84, COLOR_PANEL,
                     text["pack"], lv.TEXT_ALIGN.CENTER)
        _panel_label(scr, 8, 100, 148, 70, COLOR_PANEL,
                     text["bms"], lv.TEXT_ALIGN.CENTER, text["bms_color"])
        _panel_label(scr, 164, 100, 148, 70, COLOR_PANEL,
                     text["cell"])
        _panel_label(scr, 8, 178, 304, 54, COLOR_PANEL,
                     text["temp"],
                     lv.TEXT_ALIGN.CENTER)


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
    box = _panel(parent, x, y, w, h, COLOR_PANEL_ALT)
    _label(box, 8, 4, w - 16, 18, "%s %d%%" % (icon, value),
           lv.TEXT_ALIGN.LEFT, COLOR_ACCENT)
    if vertical:
        track_x = x + w - 20
        track_y = y + 24
        track_w = 8
        track_h = h - 34
        fill_h = max(4, (track_h * (value - value_min)) // max(1, 100 - value_min))
        _panel(parent, track_x, track_y, track_w, track_h, COLOR_PANEL).set_style_radius(4, 0)
        _panel(parent, track_x, track_y + track_h - fill_h, track_w, fill_h, COLOR_ACCENT).set_style_radius(4, 0)
        knob_x = track_x - 4
        knob_y = track_y + track_h - fill_h - 4
    else:
        track_x = x + 8
        track_y = y + h - 18
        track_w = w - 16
        track_h = 8
        _panel(parent, track_x, track_y, track_w, track_h, COLOR_PANEL).set_style_radius(4, 0)
        fill_w = max(4, (track_w * (value - value_min)) // max(1, 100 - value_min))
        _panel(parent, track_x, track_y, fill_w, track_h, COLOR_ACCENT).set_style_radius(4, 0)
        knob_x = track_x + fill_w - 8
        knob_y = track_y - 4
    knob = lv.obj(parent)
    _clean(knob)
    knob.set_pos(knob_x, knob_y)
    knob.set_size(16, 16)
    knob.set_style_radius(8, 0)
    knob.set_style_bg_color(lv.color_hex(COLOR_TEXT), 0)
    knob.set_style_bg_opa(lv.OPA.COVER, 0)


def _draw_quick_panel(scr, w, h, brightness=85, volume=65,
                      bt=False, hotspot=True, wifi=False, edit=False, vertical=False):
    portrait = w < h
    cols = len(QUICK_ITEMS)
    gap = 8
    pad = 16
    rows = 2
    tile_h = 56 if portrait else 64
    tile_w = (w - pad * 2 - (cols - 1) * gap) // cols
    grid_w = cols * tile_w + (cols - 1) * gap
    grid_h = rows * tile_h + (rows - 1) * gap
    left = (w - grid_w) // 2
    top = (h - grid_h) // 2
    control_w = (grid_w - gap) // 2

    _draw_level_tile(scr, left, top, control_w, tile_h,
                     lv.SYMBOL.EYE_OPEN, brightness, 10, vertical)
    _draw_level_tile(scr, left + control_w + gap, top, control_w, tile_h,
                     lv.SYMBOL.VOLUME_MID, volume, 0, vertical)

    edit_box = _panel(scr, w - 34, 8, 26, 24, COLOR_PANEL_ALT)
    edit_box.set_style_border_width(1 if edit else 0, 0)
    edit_box.set_style_border_color(lv.color_hex(COLOR_SOC), 0)
    edit_box.set_style_border_opa(lv.OPA.COVER, 0)
    _symbol_icon(edit_box, 18, 16, lv.SYMBOL.EDIT, COLOR_SOC if edit else COLOR_MUTED,
                 lv.font_montserrat_14, 16)

    active = {"bt": bt, "hotspot": hotspot, "wifi": wifi}
    for index, (name, icon, is_hotspot) in enumerate(QUICK_ITEMS):
        item_w = tile_w
        x = left + index * (tile_w + gap)
        y = top + tile_h + gap
        color = COLOR_SOC if active.get(name, False) else COLOR_TEXT
        box = _panel(scr, x, y, item_w, tile_h, COLOR_PANEL_ALT)
        if name == "bt":
            _bluetooth_icon(box, item_w, tile_h, active.get(name, False))
        elif is_hotspot:
            _hotspot_icon(box, item_w, tile_h, color)
        else:
            _symbol_icon(box, item_w - 8, tile_h - 8, icon, color, lv.font_montserrat_24)


def _draw_settings(scr, w, h):
    portrait = w < h
    action_w = 88 if portrait else 84
    setup_info_x = action_w + 16
    setup_info_w = w - setup_info_x - 8
    setup_qr_size = 128 if setup_info_w >= 128 else setup_info_w
    actions = (
        (8, 8, action_w, 28, "BACK"),
        (8, 44, action_w, 22, "SETUP AP"),
        (8, 70, action_w, 22, "BRIGHT"),
        (8, 96, action_w, 22, "ROTATE"),
        (8, 122, action_w, 22, "SPEED"),
        (8, 148, action_w, 22, "LANG"),
        (8, 174, action_w, 22, "BMS"),
        (8, 200, action_w, 22, "RESTORE"),
    )
    for x, y, aw, ah, text in actions:
        _panel_label(scr, x, y, aw, ah, COLOR_PANEL, text)

    _panel_label(scr, setup_info_x, 8, setup_info_w, 68, COLOR_PANEL_ALT,
                 "SETUP ON\nSSID fuckingBms_c2ce5c\nPW 12345678")

    qr = _panel(scr, setup_info_x, 84, setup_qr_size, setup_qr_size, 0xFFFFFF)
    qr.set_style_radius(0, 0)
    block = max(4, setup_qr_size // 16)
    for row in range(16):
        for col in range(16):
            edge = row in (0, 1, 14, 15) or col in (0, 1, 14, 15)
            finder = (row < 5 and col < 5) or (row < 5 and col > 10) or (row > 10 and col < 5)
            pattern = ((row * 7 + col * 5 + row * col) % 6) in (0, 1, 4)
            if edge or finder or pattern:
                dot = lv.obj(qr)
                _clean(dot)
                dot.set_pos(col * block, row * block)
                dot.set_size(block, block)
                dot.set_style_radius(0, 0)
                dot.set_style_bg_color(lv.color_hex(0x000000), 0)
                dot.set_style_bg_opa(lv.OPA.COVER, 0)


def build(scr, view="home", sample=False, brightness=85, volume=65,
          bt=False, hotspot=True, wifi=False, edit=False, vertical=False):
    try:
        w = scr.get_width()
        h = scr.get_height()
    except AttributeError:
        w = 240
        h = 320
    scr.set_style_bg_color(lv.color_hex(COLOR_BG), 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)
    if view == "quick":
        _draw_quick_panel(scr, w, h, brightness, volume, bt, hotspot, wifi, edit, vertical)
    elif view == "settings":
        scr.clean()
        scr.set_style_bg_color(lv.color_hex(COLOR_BG), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        _draw_settings(scr, w, h)
    else:
        _draw_dashboard(scr, w, h, sample)
        _draw_pull_handle(scr, w)
