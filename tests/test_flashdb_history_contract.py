import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FLASHDB = ROOT / "components/esp_bms_flashdb"
RUNTIME = ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"


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


class FlashDbHistoryContractTest(unittest.TestCase):
    def test_flashdb_is_tsdb_only_with_fixed_sample_size(self):
        cmake = (FLASHDB / "CMakeLists.txt").read_text(encoding="utf-8")
        header = (FLASHDB / "include/esp_bms_flashdb.h").read_text(encoding="utf-8")
        self.assertIn("src/fdb_tsdb.c", cmake)
        self.assertNotIn("src/fdb_kvdb.c", cmake)
        self.assertNotIn("src/fdb_file.c", cmake)
        self.assertIn("ESP_BMS_FLASHDB_SAMPLE_PAYLOAD_SIZE 32U", header)
        self.assertIn("ESP_BMS_FLASHDB_MAX_SAMPLES 18000U", header)
        self.assertIn("_Static_assert(sizeof(esp_bms_flashdb_sample_t)", header)

    def test_board_layouts_keep_faults_outside_history_slots(self):
        fal = (FLASHDB / "fal_inc/fal_cfg.h").read_text(encoding="utf-8")
        s3, classic = fal.split("#else", 1)
        self.assertEqual(len(re.findall(r'"history[0-2]"', s3)), 3)
        self.assertEqual(len(re.findall(r'"history[0-2]"', classic)), 1)
        self.assertIn('"faults"', s3)
        self.assertIn('"faults"', classic)

        source = (FLASHDB / "esp_bms_flashdb.c").read_text(encoding="utf-8")
        start = function_body(FLASHDB / "esp_bms_flashdb.c", "esp_bms_flashdb_start_session")
        self.assertIn("fdb_tsl_clean(&s_history[selected])", start)
        self.assertNotIn("s_faults", start)
        self.assertIn('init_db(&s_faults, "faults", sizeof(esp_bms_flashdb_fault_t), true)', source)

    def test_history_cursor_accepts_zero_and_stops_on_empty_page(self):
        samples = function_body(RUNTIME, "runtime_http_history_samples_handler")
        faults = function_body(RUNTIME, "runtime_http_history_faults_handler")
        for body in (samples, faults):
            self.assertIn('runtime_http_query_u64(req, "cursor", UINT64_MAX)', body)
            self.assertIn('\\"next_cursor\\":null', body)
        self.assertNotIn("runtime_persist_gps_track", RUNTIME.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
