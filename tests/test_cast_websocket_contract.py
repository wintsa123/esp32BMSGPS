import re
import unittest
from pathlib import Path


RUNTIME = (
    Path(__file__).resolve().parents[1]
    / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
)


class CastWebSocketContractTest(unittest.TestCase):
    def test_frame_handler_does_not_gate_on_http_method(self):
        source = RUNTIME.read_text(encoding="utf-8")
        body = re.search(
            r"esp_bms_idf_runtime_http_cast_ws_handler\([^;]*?\)\s*\{(.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(body)
        handler = body.group(1)
        self.assertNotIn("if (req->method == HTTP_GET)", handler)
        self.assertIn("httpd_ws_recv_frame(req, &frame, 0)", handler)


if __name__ == "__main__":
    unittest.main()
