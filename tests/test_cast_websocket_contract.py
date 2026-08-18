import re
import unittest
from pathlib import Path


RUNTIME = (
    Path(__file__).resolve().parents[1]
    / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
)


class CastWebSocketContractTest(unittest.TestCase):
    def test_upgrade_get_returns_before_receiving_frames(self):
        source = RUNTIME.read_text(encoding="utf-8")
        body = re.search(
            r"esp_bms_idf_runtime_http_cast_ws_handler\([^;]*?\)\s*\{(.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(body)
        handler = body.group(1)
        upgrade = handler.index("if (req->method == HTTP_GET)")
        receive = handler.index("httpd_ws_recv_frame(req, &frame, 0)")
        self.assertLess(upgrade, receive)
        self.assertRegex(
            handler[upgrade:receive],
            r"if \(req->method == HTTP_GET\)\s*\{\s*return ESP_OK;\s*\}",
        )


if __name__ == "__main__":
    unittest.main()
