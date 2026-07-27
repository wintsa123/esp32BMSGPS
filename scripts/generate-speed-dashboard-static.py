#!/usr/bin/env python3
"""Generate the immutable 320x240 S1000RR speed-dashboard background."""

from pathlib import Path

from PIL import Image, ImageDraw


WIDTH = 320
HEIGHT = 240
SEGMENTS = 48
DANGER_START = SEGMENTS * 7 // 8
MINOR_TICK_STEP = SEGMENTS // 16
MAJOR_TICK_STEP = SEGMENTS // 4
BAND_OVERLAP = 4

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
IDLE = (0x27, 0x29, 0x2D)
DANGER = (0xC8, 0x24, 0x2F)
DIVIDER = (0x00, 0xC8, 0xF2)

ROOT = Path(__file__).resolve().parents[1]
RGB565_PATH = ROOT / "components/esp_bms_lvgl_ui/speed_dashboard_static_landscape.rgb565"
PREVIEW_PATH = ROOT / "preview/speed-dashboard-static-landscape.png"


def smooth_step(index: int) -> int:
    position = index * 1024 // SEGMENTS
    return position * position * (3072 - 2 * position) // 1048576


def geometry() -> tuple[list[tuple[int, int]], list[tuple[int, int]]]:
    outer = []
    inner = []
    for index in range(SEGMENTS + 1):
        smooth = smooth_step(index)
        outer.append((14 + 292 * index // SEGMENTS, 185 - 88 * smooth // 1024))
        inner.append((14 + 286 * index // SEGMENTS, 222 - 78 * smooth // 1024))
    return outer, inner


def overlap(index: int, start: tuple[int, int], end: tuple[int, int]) -> tuple[tuple[int, int], tuple[int, int]]:
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    span = max(abs(dx), abs(dy))
    if span == 0:
        return start, end
    step_x = dx * BAND_OVERLAP // span
    step_y = dy * BAND_OVERLAP // span
    if index > 0:
        start = (start[0] - step_x, start[1] - step_y)
    if index + 1 < SEGMENTS:
        end = (end[0] + step_x, end[1] + step_y)
    return start, end


def band_width(outer_start: tuple[int, int], inner_start: tuple[int, int],
               outer_end: tuple[int, int], inner_end: tuple[int, int]) -> int:
    start_width = abs(inner_start[1] - outer_start[1])
    end_width = abs(inner_end[1] - outer_end[1])
    return max(2, (start_width + end_width + 1) // 2)


def render() -> Image.Image:
    image = Image.new("RGB", (WIDTH, HEIGHT), BLACK)
    draw = ImageDraw.Draw(image)
    outer, inner = geometry()
    for index in range(SEGMENTS):
        start = ((outer[index][0] + inner[index][0]) // 2,
                 (outer[index][1] + inner[index][1]) // 2)
        end = ((outer[index + 1][0] + inner[index + 1][0]) // 2,
               (outer[index + 1][1] + inner[index + 1][1]) // 2)
        start, end = overlap(index, start, end)
        draw.line((start, end), fill=DANGER if index >= DANGER_START else IDLE,
                  width=band_width(outer[index], inner[index], outer[index + 1], inner[index + 1]))

    for index in range(SEGMENTS):
        color = DANGER if index >= DANGER_START else WHITE
        draw.line((outer[index], outer[index + 1]), fill=color,
                  width=2 if index >= DANGER_START else 4)

    for index in range(0, SEGMENTS + 1, MINOR_TICK_STEP):
        major = index % MAJOR_TICK_STEP == 0 or index == SEGMENTS
        amount = 38 if major else 22
        tick_end = (outer[index][0] + (inner[index][0] - outer[index][0]) * amount // 100,
                    outer[index][1] + (inner[index][1] - outer[index][1]) * amount // 100)
        draw.line((outer[index], tick_end), fill=DANGER if index >= DANGER_START else WHITE,
                  width=2 if major else 1)

    last = SEGMENTS
    draw.rectangle((inner[last][0], outer[last][1], outer[last][0], inner[last][1]), fill=DANGER)
    draw.line(((outer[last][0], outer[last][1]), (outer[last][0], inner[last][1])), fill=DANGER, width=2)
    draw.line(((8, 31), (312, 31)), fill=DIVIDER, width=1)
    return image


def to_rgb565_le(image: Image.Image) -> bytes:
    output = bytearray()
    for red, green, blue in image.getdata():
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output.extend((pixel & 0xFF, pixel >> 8))
    return bytes(output)


def main() -> None:
    image = render()
    data = to_rgb565_le(image)
    assert image.size == (WIDTH, HEIGHT)
    assert len(data) == WIDTH * HEIGHT * 2
    RGB565_PATH.write_bytes(data)
    image.save(PREVIEW_PATH)
    print(f"wrote {RGB565_PATH.relative_to(ROOT)} ({len(data)} bytes)")
    print(f"wrote {PREVIEW_PATH.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
