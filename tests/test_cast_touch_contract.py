import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_H = ROOT / "components/esp_bms_idf_runtime/include/esp_bms_cast_protocol.h"
PROTOCOL_C = ROOT / "components/esp_bms_idf_runtime/esp_bms_cast_protocol.c"
RUNTIME = ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
BRIDGE_H = ROOT / "components/esp_bms_lvgl_bridge/include/esp_bms_lvgl_bridge.h"
BRIDGE_C = ROOT / "components/esp_bms_lvgl_bridge/esp_bms_lvgl_bridge.c"
DISPLAY_H = ROOT / "components/esp_bms_display_service/include/esp_bms_display_service.h"
DISPLAY_C = ROOT / "components/esp_bms_display_service/esp_bms_display_service.c"
KOTLIN_PROTOCOL = ROOT / "android-cast/app/src/main/java/com/fuckingbms/cast/CastProtocol.kt"


def define(text, name):
    match = re.search(rf"^#define\s+{name}\s+(\S+)", text, re.M)
    if not match:
        raise AssertionError(f"missing define: {name}")
    # Strip the C integer suffixes (4U, 7UL) so values compare against plain numbers.
    return re.sub(r"[UuLl]+$", "", match.group(1))


def function_body(source, name):
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"missing function: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {name}")


class CastTouchContractTest(unittest.TestCase):
    def test_protocol_version_matches_between_firmware_and_app(self):
        header = PROTOCOL_H.read_text(encoding="utf-8")
        app = KOTLIN_PROTOCOL.read_text(encoding="utf-8")
        firmware_version = define(header, "ESP_BMS_CAST_PROTOCOL_VERSION")
        app_version = re.search(r"const val VERSION = (\d+)", app).group(1)
        self.assertEqual("4", firmware_version)
        self.assertEqual(firmware_version, app_version)

    def test_input_frame_layout_matches_between_firmware_and_app(self):
        header = PROTOCOL_H.read_text(encoding="utf-8")
        app = KOTLIN_PROTOCOL.read_text(encoding="utf-8")
        self.assertEqual("2", define(header, "ESP_BMS_CAST_TYPE_INPUT_TOUCH"))
        self.assertEqual("7", define(header, "ESP_BMS_CAST_INPUT_FRAME_BYTES"))
        self.assertEqual("0", define(header, "ESP_BMS_CAST_INPUT_PHASE_DOWN"))
        self.assertEqual("1", define(header, "ESP_BMS_CAST_INPUT_PHASE_MOVE"))
        self.assertEqual("2", define(header, "ESP_BMS_CAST_INPUT_PHASE_UP"))
        self.assertIn("const val INPUT_TOUCH: Byte = 2", app)
        self.assertIn("const val INPUT_FRAME_BYTES = 7", app)
        self.assertIn("const val INPUT_PHASE_DOWN = 0", app)
        self.assertIn("const val INPUT_PHASE_MOVE = 1", app)
        self.assertIn("const val INPUT_PHASE_UP = 2", app)

    def test_encoder_writes_big_endian_coordinates(self):
        body = function_body(
            PROTOCOL_C.read_text(encoding="utf-8"),
            "esp_bms_cast_protocol_encode_input",
        )
        self.assertIn("input[0] = ESP_BMS_CAST_TYPE_INPUT_TOUCH;", body)
        self.assertIn("input[1] = ESP_BMS_CAST_PROTOCOL_VERSION;", body)
        self.assertIn("input[3] = (uint8_t)(x >> 8U);", body)
        self.assertIn("input[4] = (uint8_t)x;", body)
        self.assertIn("input[5] = (uint8_t)(y >> 8U);", body)
        self.assertIn("input[6] = (uint8_t)y;", body)

    def test_bridge_exposes_calibrated_cast_touch_readback(self):
        self.assertIn(
            "esp_bms_lvgl_bridge_read_cast_touch",
            BRIDGE_H.read_text(encoding="utf-8"),
        )
        body = function_body(
            BRIDGE_C.read_text(encoding="utf-8"),
            "esp_bms_lvgl_bridge_read_cast_touch",
        )
        self.assertIn("esp_lcd_touch_read_data", body)
        self.assertIn("touch_calibration_apply", body)
        # The LVGL UI filter must not run here: it holds the first polled sample back.
        self.assertNotIn("touch_filter_apply", body)

    def test_display_service_forwards_cast_touch_readback(self):
        self.assertIn(
            "esp_bms_display_service_read_cast_touch",
            DISPLAY_H.read_text(encoding="utf-8"),
        )
        body = function_body(
            DISPLAY_C.read_text(encoding="utf-8"),
            "esp_bms_display_service_read_cast_touch",
        )
        self.assertIn("esp_bms_lvgl_bridge_read_cast_touch", body)

    def test_runtime_pushes_input_frames_asynchronously_under_the_socket_lock(self):
        source = RUNTIME.read_text(encoding="utf-8")
        body = function_body(source, "runtime_cast_send_input")
        self.assertIn("esp_bms_cast_protocol_encode_input", body)
        self.assertIn("httpd_ws_send_frame_async", body)
        self.assertIn("cast_tx_lock", body)
        self.assertIn("ESP_BMS_CAST_INPUT_PHASE_MOVE", function_body(source, "runtime_cast_touch_task"))

    def test_ack_and_input_frames_share_one_socket_lock(self):
        body = function_body(RUNTIME.read_text(encoding="utf-8"), "runtime_cast_send_ack")
        self.assertIn("cast_tx_lock", body)
        self.assertIn("httpd_ws_send_frame", body)

    def test_stop_cast_tears_down_the_touch_task(self):
        body = function_body(
            RUNTIME.read_text(encoding="utf-8"),
            "esp_bms_idf_runtime_stop_cast",
        )
        self.assertIn("runtime_cast_touch_stop(runtime);", body)

    def test_cast_connect_starts_the_touch_task(self):
        body = function_body(
            RUNTIME.read_text(encoding="utf-8"),
            "esp_bms_idf_runtime_http_cast_connected",
        )
        self.assertIn("runtime_cast_touch_start(runtime);", body)

    def test_app_decodes_device_input_frames(self):
        app = KOTLIN_PROTOCOL.read_text(encoding="utf-8")
        self.assertIn("fun inputEvent(packet: ByteArray): TouchEvent?", app)
        self.assertIn("data class TouchEvent", app)

    def test_exit_frame_layout_matches_between_firmware_and_app(self):
        header = PROTOCOL_H.read_text(encoding="utf-8")
        app = KOTLIN_PROTOCOL.read_text(encoding="utf-8")
        self.assertEqual("3", define(header, "ESP_BMS_CAST_TYPE_EXIT_CAST"))
        self.assertEqual("2", define(header, "ESP_BMS_CAST_EXIT_FRAME_BYTES"))
        self.assertIn("const val EXIT_CAST: Byte = 3", app)
        self.assertIn("const val EXIT_FRAME_BYTES = 2", app)
        self.assertIn("fun exitRequest(packet: ByteArray): Boolean", app)

    def test_panel_long_press_ends_the_cast_session(self):
        source = RUNTIME.read_text(encoding="utf-8")
        task = function_body(source, "runtime_cast_touch_task")
        self.assertIn("CAST_TOUCH_LONGPRESS_US", task)
        self.assertIn("runtime_cast_send_exit(runtime)", task)
        self.assertIn("cast_exit_requested", task)
        # Teardown must run from the runtime tick, never from the touch task itself.
        tick = function_body(source, "esp_bms_idf_runtime_tick")
        self.assertIn("cast_exit_requested", tick)
        self.assertIn("esp_bms_idf_runtime_stop_cast", tick)

    def test_bridge_overlays_touch_feedback_on_cast_frames(self):
        source = BRIDGE_C.read_text(encoding="utf-8")
        present = function_body(source, "esp_bms_lvgl_bridge_present_jpeg")
        self.assertIn("cast_overlay_touch", present)
        read_touch = function_body(source, "esp_bms_lvgl_bridge_read_cast_touch")
        self.assertIn("cast_touch_mark_pressed", read_touch)
        self.assertIn("cast_touch_mark_released", read_touch)
        overlay = function_body(source, "cast_overlay_touch")
        self.assertIn("CAST_TRAIL_RGB565", overlay)
        self.assertIn("CAST_LONGPRESS_RGB565", overlay)

    def test_bridge_waits_for_i80_dma_before_reusing_the_draw_buffer(self):
        source = BRIDGE_C.read_text(encoding="utf-8")
        draw = function_body(source, "i80_draw_bitmap_swap")
        self.assertIn("s_i80_trans_done_sem", draw)
        self.assertIn("xSemaphoreTake", draw)
        self.assertIn("s_i80_trans_in_flight", draw)
        hook = function_body(source, "i80_color_trans_done")
        self.assertIn("xSemaphoreGiveFromISR", hook)
        # The adapter's own bookkeeping must survive the hook replacement.
        self.assertIn("esp_lv_adapter_display_notify_color_trans_done_from_isr", hook)
        init = function_body(source, "esp_bms_lvgl_bridge_init")
        self.assertIn("esp_lcd_panel_io_register_event_callbacks", init)
        self.assertIn("xSemaphoreCreateBinary", init)

    def test_app_letterboxes_instead_of_cropping(self):
        geometry = (
            ROOT / "android-cast/app/src/main/java/com/fuckingbms/cast/CastGeometry.kt"
        ).read_text(encoding="utf-8")
        self.assertIn("fun castLayoutFor(", geometry)
        service = (
            ROOT / "android-cast/app/src/main/java/com/fuckingbms/cast/CastService.kt"
        ).read_text(encoding="utf-8")
        self.assertIn("castLayoutFor(", service)
        self.assertNotIn("cropRectFor(", service)


if __name__ == "__main__":
    unittest.main()
