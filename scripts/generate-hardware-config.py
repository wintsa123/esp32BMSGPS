#!/usr/bin/env python3
"""Generate the profile-owned LVGL hardware contract used by the firmware."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


KEY_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
VALUE_RE = re.compile(r"^[A-Za-z0-9,._:/-]*$")
PIN_RE = re.compile(r"^[0-9]{1,2}$")


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except OSError as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    for number, line in enumerate(lines, start=1):
        line = line.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: expected KEY=VALUE")
        key, value = line.split("=", 1)
        if not KEY_RE.fullmatch(key) or not VALUE_RE.fullmatch(value):
            raise ValueError(f"{path}:{number}: invalid KEY=VALUE entry")
        if key in values:
            raise ValueError(f"{path}:{number}: duplicate key {key}")
        values[key] = value
    return values


def require(values: dict[str, str], key: str, source: Path) -> str:
    value = values.get(key)
    if value is None or value == "":
        raise ValueError(f"{source}: missing {key}")
    return value


def integer(values: dict[str, str], key: str, source: Path) -> int:
    value = require(values, key, source)
    if not value.isdecimal():
        raise ValueError(f"{source}: {key} must be a decimal integer")
    return int(value, 10)


def gpio(profile: dict[str, str], role: str, required: bool) -> str:
    value = profile.get(f"GPIO_{role}")
    if value is None or value == "":
        if required:
            raise ValueError(f"firmware.env: missing required GPIO role {role}")
        return "GPIO_NUM_NC"
    if not PIN_RE.fullmatch(value):
        raise ValueError(f"firmware.env: GPIO_{role} must be a decimal GPIO number")
    return f"(gpio_num_t){value}"


def enum_value(value: str, mapping: dict[str, str], field: str, source: Path) -> str:
    try:
        return mapping[value]
    except KeyError as exc:
        raise ValueError(f"{source}: unsupported {field} {value}") from exc


def bool_value(values: dict[str, str], key: str, source: Path) -> str:
    value = require(values, key, source)
    if value == "0":
        return "false"
    if value == "1":
        return "true"
    raise ValueError(f"{source}: {key} must be 0 or 1")


def config_macro(profile: dict[str, str], board: dict[str, str], board_path: Path,
                 display: dict[str, str], display_path: Path,
                 touch: dict[str, str], touch_path: Path) -> str:
    panel = enum_value(require(display, "DRIVER", display_path), {
        "ST7789": "ESP_BMS_LVGL_PANEL_ST7789",
        "ST7796": "ESP_BMS_LVGL_PANEL_ST7796",
        "ILI9488": "ESP_BMS_LVGL_PANEL_ILI9488",
    }, "display driver", display_path)
    display_bus = enum_value(require(display, "BUS", display_path), {
        "SPI": "ESP_BMS_LVGL_DISPLAY_BUS_SPI",
        "I80": "ESP_BMS_LVGL_DISPLAY_BUS_I80",
    }, "display bus", display_path)
    touch_driver = enum_value(require(touch, "DRIVER", touch_path), {
        "NONE": "ESP_BMS_LVGL_TOUCH_NONE",
        "XPT2046": "ESP_BMS_LVGL_TOUCH_XPT2046",
        "FT5X06": "ESP_BMS_LVGL_TOUCH_FT5X06",
        "GT1151": "ESP_BMS_LVGL_TOUCH_GT1151",
    }, "touch driver", touch_path)
    rotation = enum_value(require(display, "ROTATION", display_path), {
        "PORTRAIT": "ESP_BMS_DISPLAY_ROTATION_PORTRAIT",
        "LANDSCAPE": "ESP_BMS_DISPLAY_ROTATION_LANDSCAPE",
        "INVERTED_PORTRAIT": "ESP_BMS_DISPLAY_ROTATION_INVERTED_PORTRAIT",
        "INVERTED_LANDSCAPE": "ESP_BMS_DISPLAY_ROTATION_INVERTED_LANDSCAPE",
    }, "display rotation", display_path)
    color_order = enum_value(require(display, "RGB_ORDER", display_path), {
        "RGB": "LCD_RGB_ELEMENT_ORDER_RGB",
        "BGR": "LCD_RGB_ELEMENT_ORDER_BGR",
    }, "RGB order", display_path)
    audio_backend = enum_value(require(board, "AUDIO_BACKEND", board_path), {
        "NONE": "ESP_BMS_PROFILE_AUDIO_BACKEND_NONE",
        "DAC": "ESP_BMS_PROFILE_AUDIO_BACKEND_DAC",
        "I2S": "ESP_BMS_PROFILE_AUDIO_BACKEND_I2S",
    }, "audio backend", board_path)
    audio_dac_channel = integer(board, "AUDIO_DAC_CHANNEL", board_path)
    audio_enable_level = integer(board, "AUDIO_ENABLE_ACTIVE_LEVEL", board_path)
    if audio_backend == "ESP_BMS_PROFILE_AUDIO_BACKEND_DAC" and audio_dac_channel not in (1, 2):
        raise ValueError(f"{board_path}: DAC audio requires AUDIO_DAC_CHANNEL 1 or 2")
    if audio_backend != "ESP_BMS_PROFILE_AUDIO_BACKEND_DAC" and audio_dac_channel != 0:
        raise ValueError(f"{board_path}: non-DAC audio requires AUDIO_DAC_CHANNEL 0")
    if audio_enable_level not in (0, 1):
        raise ValueError(f"{board_path}: AUDIO_ENABLE_ACTIVE_LEVEL must be 0 or 1")
    audio_dac_mask = {
        0: "0",
        1: "DAC_CHANNEL_MASK_CH1",
        2: "DAC_CHANNEL_MASK_CH2",
    }[audio_dac_channel]

    data_width = integer(display, "DATA_WIDTH", display_path)
    if display_bus == "ESP_BMS_LVGL_DISPLAY_BUS_SPI" and data_width != 0:
        raise ValueError(f"{display_path}: SPI display DATA_WIDTH must be 0")
    if display_bus == "ESP_BMS_LVGL_DISPLAY_BUS_I80" and data_width not in (8, 16):
        raise ValueError(f"{display_path}: I80 display DATA_WIDTH must be 8 or 16")
    data_pins = ["GPIO_NUM_NC"] * 16
    if display_bus == "ESP_BMS_LVGL_DISPLAY_BUS_I80":
        for index in range(data_width):
            data_pins[index] = gpio(profile, f"TFT_D{index}", True)

    touch_bus = require(touch, "BUS", touch_path)
    if touch_driver == "ESP_BMS_LVGL_TOUCH_XPT2046" and touch_bus != "SPI":
        raise ValueError(f"{touch_path}: XPT2046 requires SPI")
    if touch_driver in ("ESP_BMS_LVGL_TOUCH_FT5X06", "ESP_BMS_LVGL_TOUCH_GT1151") and touch_bus != "I2C":
        raise ValueError(f"{touch_path}: I2C touch driver requires I2C")

    is_spi_display = display_bus == "ESP_BMS_LVGL_DISPLAY_BUS_SPI"
    is_xpt2046 = touch_driver == "ESP_BMS_LVGL_TOUCH_XPT2046"
    is_i2c_touch = touch_driver in ("ESP_BMS_LVGL_TOUCH_FT5X06", "ESP_BMS_LVGL_TOUCH_GT1151")
    fields = {
        "display_bus": display_bus,
        "panel_driver": panel,
        "touch_driver": touch_driver,
        "pin_miso": gpio(profile, "TFT_MISO", False),
        "pin_mosi": gpio(profile, "TFT_MOSI", is_spi_display),
        "pin_sclk": gpio(profile, "TFT_SCLK", is_spi_display),
        "pin_cs": gpio(profile, "TFT_CS", True),
        "pin_dc": gpio(profile, "TFT_DC", True),
        "pin_reset": gpio(profile, "TFT_RESET", False),
        "pin_backlight": gpio(profile, "TFT_BACKLIGHT", False),
        "i80_data_pins": "{ " + ", ".join(data_pins) + " }",
        "pin_wr": gpio(profile, "TFT_WR", not is_spi_display),
        "i80_bus_width": str(data_width),
        "pin_touch_miso": gpio(profile, "TOUCH_MISO", is_xpt2046),
        "pin_touch_mosi": gpio(profile, "TOUCH_MOSI", is_xpt2046),
        "pin_touch_sclk": gpio(profile, "TOUCH_SCLK", is_xpt2046),
        "pin_touch_cs": gpio(profile, "TOUCH_CS", is_xpt2046),
        "pin_touch_irq": gpio(profile, "TOUCH_IRQ", False) if is_xpt2046 else gpio(profile, "TOUCH_INT", False),
        "pin_touch_reset": gpio(profile, "TOUCH_RESET", False),
        "pin_touch_sda": gpio(profile, "TOUCH_SDA", is_i2c_touch),
        "pin_touch_scl": gpio(profile, "TOUCH_SCL", is_i2c_touch),
        "backlight_on_level": str(integer(display, "BACKLIGHT_ON_LEVEL", display_path)),
        "pixel_clock_hz": str(integer(display, "PIXEL_CLOCK_HZ", display_path)),
        "physical_width": str(integer(display, "WIDTH", display_path)),
        "physical_height": str(integer(display, "HEIGHT", display_path)),
        "rotation": rotation,
        "rgb_element_order": color_order,
        "invert_color": bool_value(display, "INVERT_COLOR", display_path),
        "spi_mode": str(integer(display, "SPI_MODE", display_path)),
        "i80_swap_color_bytes": bool_value(display, "I80_SWAP_COLOR_BYTES", display_path),
        "i80_pclk_active_neg": bool_value(display, "I80_PCLK_ACTIVE_NEG", display_path),
        "i80_pclk_idle_low": bool_value(display, "I80_PCLK_IDLE_LOW", display_path),
        "touch_use_irq": bool_value(touch, "USE_IRQ", touch_path),
        "touch_swap_xy": bool_value(touch, "SWAP_XY", touch_path),
        "touch_mirror_x": bool_value(touch, "MIRROR_X", touch_path),
        "touch_mirror_y": bool_value(touch, "MIRROR_Y", touch_path),
        "touch_i2c_address": str(integer(touch, "I2C_ADDRESS", touch_path)),
        "touch_i2c_clock_hz": str(integer(touch, "I2C_CLOCK_HZ", touch_path)),
        "touch_i2c_control_phase_bytes": str(integer(touch, "I2C_CONTROL_PHASE_BYTES", touch_path)),
        "touch_i2c_dc_bit_offset": str(integer(touch, "I2C_DC_BIT_OFFSET", touch_path)),
        "touch_i2c_cmd_bits": str(integer(touch, "I2C_CMD_BITS", touch_path)),
        "touch_i2c_param_bits": str(integer(touch, "I2C_PARAM_BITS", touch_path)),
        "touch_i2c_disable_control_phase": bool_value(touch, "I2C_DISABLE_CONTROL_PHASE", touch_path),
        "touch_i2c_internal_pullup": bool_value(touch, "I2C_INTERNAL_PULLUP", touch_path),
        "touch_reset_level": str(integer(touch, "RESET_LEVEL", touch_path)),
        "touch_irq_level": str(integer(touch, "IRQ_LEVEL", touch_path)),
        "power_on_delay_ms": str(integer(display, "POWER_ON_DELAY_MS", display_path)),
    }
    lines = ["#pragma once", "", "/* Generated from firmware.env and the selected catalog records. */",
             f"#define ESP_BMS_PROFILE_GPS_RX {gpio(profile, 'GPS_RX', False)}",
             f"#define ESP_BMS_PROFILE_GPS_TX {gpio(profile, 'GPS_TX', False)}",
             f"#define ESP_BMS_PROFILE_GPS_PPS {gpio(profile, 'GPS_PPS', False)}",
             f"#define ESP_BMS_PROFILE_BATTERY_ADC {gpio(profile, 'BATTERY_ADC', False)}",
             "#define ESP_BMS_PROFILE_AUDIO_BACKEND_NONE 0",
             "#define ESP_BMS_PROFILE_AUDIO_BACKEND_DAC 1",
             "#define ESP_BMS_PROFILE_AUDIO_BACKEND_I2S 2",
             f"#define ESP_BMS_PROFILE_AUDIO_BACKEND {audio_backend}",
             f"#define ESP_BMS_PROFILE_AUDIO_DAC {gpio(profile, 'AUDIO_DAC', False)}",
             f"#define ESP_BMS_PROFILE_AUDIO_DAC_CHANNEL_MASK {audio_dac_mask}",
             f"#define ESP_BMS_PROFILE_AUDIO_ENABLE {gpio(profile, 'AUDIO_ENABLE', False) if audio_backend == 'ESP_BMS_PROFILE_AUDIO_BACKEND_DAC' else gpio(profile, 'AMP_SHDN', False)}",
             f"#define ESP_BMS_PROFILE_AUDIO_ENABLE_ACTIVE_LEVEL {audio_enable_level}",
             f"#define ESP_BMS_PROFILE_AUDIO_I2S_BCLK {gpio(profile, 'I2S_BCLK', False)}",
             f"#define ESP_BMS_PROFILE_AUDIO_I2S_LRCK {gpio(profile, 'I2S_LRCK', False)}",
             f"#define ESP_BMS_PROFILE_AUDIO_I2S_DATA {gpio(profile, 'I2S_DATA', False)}",
             "",
             "#define ESP_BMS_PROFILE_LVGL_CONFIG \\",
             "    ((esp_bms_lvgl_bridge_config_t){ \\"]
    items = list(fields.items())
    for index, (key, value) in enumerate(items):
        comma = "," if index < len(items) - 1 else ""
        lines.append(f"        .{key} = {value}{comma} \\")
    lines.append("    })")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--firmware-env", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        profile = read_env(args.firmware_env)
        board_id = require(profile, "BOARD", args.firmware_env)
        display_id = require(profile, "DISPLAY", args.firmware_env)
        input_id = require(profile, "INPUT", args.firmware_env)
        if display_id == "custom" or input_id == "custom":
            content = "#error \"Custom hardware needs a catalog driver record before it can be built\"\n"
        else:
            display_path = args.catalog / "display" / f"{display_id}.env"
            touch_path = args.catalog / "input" / f"{input_id}.env"
            board_path = args.catalog / "board" / f"{board_id}.env"
            content = config_macro(profile, read_env(board_path), board_path,
                                   read_env(display_path), display_path,
                                   read_env(touch_path), touch_path)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content, encoding="ascii", newline="\n")
    except ValueError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
