import lvgl as lv
import lvkit as K


BG = 0x000000
WHITE = 0xF7F9FC
BLUE_DARK = 0x005594
BLUE = 0x00B8F0
IDLE = 0x27292D
DANGER = 0xC8242F
DIVIDER = 0x00C8F2
GPS_OK = 0x43E336
WARN = 0xFFC857
BORDER = 0x3E4247
PANEL = 0x090C10
MONTSERRAT = "managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
SEGMENTS = 32
_POINT = getattr(lv, "point_precise_t", None) or lv.point_t
_LINE_POINTS = []


def clean(obj):
    obj.remove_style_all()
    clear = getattr(obj, "remove_flag", None) or getattr(obj, "clear_flag")
    clear(lv.obj.FLAG.SCROLLABLE)
    return obj


def font(size):
    builtin = getattr(lv, "font_montserrat_%d" % size, None)
    if builtin is not None:
        return builtin, 256
    if size == 48:
        return lv.font_montserrat_24, 512
    return lv.font_montserrat_14, max(1, size * 256 // 14)


def label(parent, text, x, y, w, h, size, color=WHITE, align=None):
    obj = clean(lv.label(parent))
    selected_font, scale = font(size)
    render_w = max(1, (w * 256 + scale - 1) // scale)
    render_h = max(1, (h * 256 + scale - 1) // scale)
    obj.set_pos(x, y)
    obj.set_size(render_w, render_h)
    obj.set_text(str(text))
    obj.set_long_mode(lv.label.LONG_MODE.CLIP)
    obj.set_style_text_color(lv.color_hex(color), 0)
    if selected_font is not None:
        obj.set_style_text_font(selected_font, 0)
    obj.set_style_text_align(align or lv.TEXT_ALIGN.CENTER, 0)
    if scale != 256:
        obj.set_style_transform_pivot_x(0, 0)
        obj.set_style_transform_pivot_y(0, 0)
        obj.set_style_transform_scale(scale, 0)
    return obj


def rect(parent, x, y, w, h, color, radius=0):
    obj = clean(lv.obj(parent))
    obj.set_pos(int(x), int(y))
    obj.set_size(int(w), int(h))
    obj.set_style_radius(int(radius), 0)
    obj.set_style_pad_all(0, 0)
    obj.set_style_border_width(0, 0)
    obj.set_style_bg_color(lv.color_hex(color), 0)
    obj.set_style_bg_opa(lv.OPA.COVER, 0)
    return obj


def dot(parent, cx, cy, radius, color):
    return rect(parent, cx - radius, cy - radius, radius * 2, radius * 2,
                color, radius * 2)


def line(parent, x1, y1, x2, y2, color, width=1, rounded=False):
    obj = clean(lv.line(parent))
    points = [_POINT({"x": int(x1), "y": int(y1)}),
              _POINT({"x": int(x2), "y": int(y2)})]
    _LINE_POINTS.append(points)
    obj.set_points(points, 2)
    obj.set_style_line_width(int(width), 0)
    obj.set_style_line_color(lv.color_hex(color), 0)
    obj.set_style_line_rounded(bool(rounded), 0)
    return obj


def smooth_step(index):
    position = index * 1024 // SEGMENTS
    return position * position * (3072 - 2 * position) // 1048576


def geometry(portrait):
    outer = []
    inner = []
    for index in range(SEGMENTS + 1):
        smooth = smooth_step(index)
        if portrait:
            outer.append((28 + 180 * smooth // 1024, 292 - 228 * index // SEGMENTS))
            inner.append((86 + 126 * smooth // 1024, 292 - 175 * index // SEGMENTS))
        else:
            outer.append((14 + 292 * index // SEGMENTS, 185 - 88 * smooth // 1024))
            inner.append((14 + 286 * index // SEGMENTS, 222 - 78 * smooth // 1024))
    return outer, inner


def mix(first, second, amount):
    amount = max(0, min(255, int(amount)))
    return tuple((first[index] * amount + second[index] * (255 - amount)) // 255
                 for index in range(3))


def rgb(value):
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


def packed(value):
    return (value[0] << 16) | (value[1] << 8) | value[2]


def segment_color(index, active, valid):
    if index >= 28:
        return DANGER
    if not valid or index >= active:
        return IDLE
    denominator = max(1, active - 1)
    progress = index * 255 // denominator
    if progress < 176:
        return packed(mix(rgb(BLUE), rgb(BLUE_DARK), progress * 255 // 176))
    return packed(mix(rgb(WHITE), rgb(BLUE), (progress - 176) * 255 // 79))


def battery(parent, portrait, soc):
    x = 8
    y = 6 if portrait else 7
    outline = clean(lv.obj(parent))
    outline.set_pos(x, y + 2)
    outline.set_size(8, 20)
    outline.set_style_bg_opa(lv.OPA.TRANSP, 0)
    outline.set_style_border_width(1, 0)
    outline.set_style_border_color(lv.color_hex(WHITE), 0)
    rect(parent, x + 2, y, 4, 2, WHITE)
    if soc is None:
        return
    active = (max(0, min(100, int(soc))) * 8 + 99) // 100
    for index in range(active):
        left = x + 12 + index * 6
        line(parent, left + 2, y + 5, left + 4, y + 11, WHITE, 5)


def satellite(parent, portrait, gps_fix):
    x = 26 if portrait else 136
    y = 29 if portrait else 8
    line(parent, x + 2, y + 2, x + 12, y + 12, WHITE, 4)
    line(parent, x + 1, y + 1, x + 5, y + 5, WHITE, 3)
    line(parent, x + 9, y + 9, x + 14, y + 14, WHITE, 3)
    line(parent, x + 7, y + 12, x + 3, y + 16, WHITE, 1)
    dot(parent, x + 18, y + 4, 3, GPS_OK if gps_fix else WARN)


def draw_band(parent, portrait, speed, valid, unit):
    outer, inner = geometry(portrait)
    maximum = 120 if unit == "mph" else 180
    active = 0 if not valid else min(SEGMENTS, (max(0, speed) * SEGMENTS + maximum - 1) // maximum)
    for index in range(SEGMENTS):
        center0 = ((outer[index][0] + inner[index][0]) // 2,
                   (outer[index][1] + inner[index][1]) // 2)
        center1 = ((outer[index + 1][0] + inner[index + 1][0]) // 2,
                   (outer[index + 1][1] + inner[index + 1][1]) // 2)
        start_width = (abs(inner[index][0] - outer[index][0]) if portrait
                       else abs(inner[index][1] - outer[index][1]))
        end_width = (abs(inner[index + 1][0] - outer[index + 1][0]) if portrait
                     else abs(inner[index + 1][1] - outer[index + 1][1]))
        width = max(2, (start_width + end_width + 1) // 2)
        dx = center1[0] - center0[0]
        dy = center1[1] - center0[1]
        span = max(abs(dx), abs(dy))
        if span:
            step_x = int(dx / span)
            step_y = int(dy / span)
            if index > 0:
                center0 = (center0[0] - step_x, center0[1] - step_y)
            if index + 1 < SEGMENTS:
                center1 = (center1[0] + step_x, center1[1] + step_y)
        line(parent, *center0, *center1,
             segment_color(index, active, valid), width)
    for index in range(SEGMENTS):
        line(parent, *outer[index], *outer[index + 1],
             DANGER if index >= 28 else WHITE, 2)
    for index in range(0, SEGMENTS + 1, 2):
        amount = 38 if index % 8 == 0 or index == SEGMENTS else 22
        end = (outer[index][0] + (inner[index][0] - outer[index][0]) * amount // 100,
               outer[index][1] + (inner[index][1] - outer[index][1]) * amount // 100)
        line(parent, *outer[index], *end,
             DANGER if index >= 28 else WHITE, 2 if amount == 38 else 1)


def build(scr, speed=88, valid=True, unit="km/h", gps_fix=True,
          bms_online=True,
          controller_online=True, controller_fields_valid=True,
          soc=87, consumption=28, consumption_valid=True,
          gear=3, controller_temp=42, motor_temp=58, local_time="19:56", **_state):
    scr.set_style_bg_color(lv.color_hex(BG), 0)
    scr.set_style_bg_opa(lv.OPA.COVER, 0)
    clear = getattr(scr, "remove_flag", None) or getattr(scr, "clear_flag")
    clear(lv.obj.FLAG.SCROLLABLE)
    scr.set_scrollbar_mode(lv.SCROLLBAR_MODE.OFF)
    w = scr.get_width()
    h = scr.get_height()
    portrait = w < h

    draw_band(scr, portrait, int(speed), bool(valid), unit)
    line(scr, 8, 50 if portrait else 34, w - 8, 50 if portrait else 34, DIVIDER, 1)
    if bms_online:
        battery(scr, portrait, soc)
    satellite(scr, portrait, gps_fix)

    speed_text = str(int(speed)) if valid else "-"
    consumption_text = ("%d %s" % (int(consumption), "Wh/mi" if unit == "mph" else "Wh/km")
                        if consumption_valid else "-- %s" % ("Wh/mi" if unit == "mph" else "Wh/km"))
    scale = [0, 30, 60, 90, 110, 120] if unit == "mph" else [0, 40, 80, 120, 160, 180]

    if portrait:
        label(scr, speed_text, 14, 58, 104, 52, 48, align=lv.TEXT_ALIGN.RIGHT)
        label(scr, unit, 20, 105, 76, 26, 24, align=lv.TEXT_ALIGN.LEFT)
        if bms_online:
            label(scr, consumption_text, 74, 7, 159, 18, 10, align=lv.TEXT_ALIGN.LEFT)
        scale_positions = [(16, 264), (56, 218), (101, 160), (139, 111), (178, 67), (207, 51)]
        gear_box = (167, 230, 40, 44)
        time_box = (112, 278, 120, 34)
        ctrl_box = (66, 29, 55, 18)
        motor_box = (124, 29, 70, 18)
    else:
        label(scr, speed_text, 0, 56, 94, 52, 48, align=lv.TEXT_ALIGN.RIGHT)
        label(scr, unit, 98, 78, 68, 26, 24, align=lv.TEXT_ALIGN.LEFT)
        if bms_online:
            label(scr, consumption_text, 74, 9, 58, 18, 10, align=lv.TEXT_ALIGN.LEFT)
        scale_positions = [(8, 168), (53, 148), (111, 124), (174, 102), (244, 84), (286, 80)]
        gear_box = (269, 153, 38, 40)
        time_box = (196, 195, 120, 34)
        ctrl_box = (164, 9, 36, 18)
        motor_box = (206, 9, 48, 18)

    for index, value in enumerate(scale):
        label(scr, value, scale_positions[index][0], scale_positions[index][1], 34, 18, 14,
              DANGER if index == len(scale) - 1 else WHITE)
    if controller_online and controller_fields_valid:
        label(scr, "C %dC" % int(controller_temp), *ctrl_box, 10,
              align=lv.TEXT_ALIGN.LEFT)
        label(scr, "MTR %dC" % int(motor_temp), *motor_box, 10,
              align=lv.TEXT_ALIGN.LEFT)

    gear_panel = clean(lv.obj(scr))
    gear_panel.set_pos(gear_box[0], gear_box[1])
    gear_panel.set_size(gear_box[2], gear_box[3])
    gear_panel.set_style_bg_color(lv.color_hex(PANEL), 0)
    gear_panel.set_style_bg_opa(lv.OPA.COVER, 0)
    gear_panel.set_style_border_width(1, 0)
    gear_panel.set_style_border_color(lv.color_hex(BORDER), 0)
    gear_panel.set_style_radius(4, 0)
    displayed_gear = gear if controller_online and controller_fields_valid else 1
    label(gear_panel, displayed_gear, 0, 0, gear_box[2], gear_box[3], 28, BLUE)

    label(scr, local_time if local_time else "--:--", *time_box, 24,
          align=lv.TEXT_ALIGN.RIGHT)
