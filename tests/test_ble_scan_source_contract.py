import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(path, name):
    source = path.read_text(encoding="utf-8")
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


class BleScanSourceContractTest(unittest.TestCase):
    def test_bms_scan_stores_named_and_unnamed_candidates_only_while_active(self):
        body = function_body(
            ROOT / "components/esp_bms_bms_ble/esp_bms_bms_ble.c",
            "bms_gap_event",
        )
        self.assertNotIn("name_has_chinese", body)
        self.assertRegex(
            body,
            r"if \(RUNTIME_FLAG\(runtime, BMS_SCAN_ACTIVE\)\)\s*\{\s*"
            r"esp_bms_idf_runtime_bms_scan_store_candidate\(runtime,\s*mac,\s*"
            r"has_name \? name : NULL,\s*rssi\);",
        )

    def test_controller_scan_stores_named_and_unnamed_candidates_only_while_active(self):
        body = function_body(
            ROOT / "components/esp_bms_controller_ble/esp_bms_controller_ble.c",
            "controller_scan_gap_event",
        )
        self.assertNotIn("name_has_chinese", body)
        self.assertRegex(
            body,
            r"if \(RUNTIME_FLAG\(runtime, CONTROLLER_SCAN_ACTIVE\)\)\s*\{\s*"
            r"controller_store_candidate\(runtime,\s*mac,\s*"
            r"has_name \? name : NULL,\s*rssi\);",
        )

    def test_runtime_reset_never_requests_discoverability_by_default(self):
        body = function_body(
            ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c",
            "runtime_reset_state",
        )
        self.assertIn(
            "RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);",
            body,
        )
        self.assertNotIn("ESP_BMS_FEATURE_BLE_MEDIA_HID", body)


if __name__ == "__main__":
    unittest.main()
