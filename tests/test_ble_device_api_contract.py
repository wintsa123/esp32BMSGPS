import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"


def function_body(name):
    source = RUNTIME.read_text(encoding="utf-8")
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


class BleDeviceApiContractTest(unittest.TestCase):
    def test_ble_dispatch_whitelists_only_profile_config_and_status(self):
        body = function_body("runtime_ble_api_response")
        for path in ("/api/status", "/api/config", "/api/settings/manifest"):
            self.assertIn(path, body)
        self.assertNotIn("/api/cast", body)
        self.assertNotIn("/api/ota", body)
        self.assertNotIn("/api/history", body)
        self.assertIn('error = "path not allowed"', body)

    def test_http_and_ble_share_serializers_and_config_validation(self):
        source = RUNTIME.read_text(encoding="utf-8")
        for helper in (
            "runtime_status_json",
            "runtime_config_json",
            "runtime_settings_manifest_json",
            "runtime_apply_config_json",
        ):
            self.assertGreaterEqual(source.count(f"{helper}("), 3)

    def test_device_service_uuid_is_advertised(self):
        advertise = function_body("runtime_bluetooth_start_advertising_now")
        self.assertIn("fields.uuids128 = &BLE_API_SERVICE_UUID", advertise)
        self.assertIn("ble_gap_adv_rsp_set_fields", advertise)


if __name__ == "__main__":
    unittest.main()
