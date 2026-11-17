"""Compile the actual GPS status serializer and check valid, stale and bounded output."""
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def definition(source, name):
    start = re.search(r'static bool ' + name + r'\([^;]*?\)\s*\{', source, re.S).start()
    end = source.index('\n}\n', start) + 3
    return source[start:end]


class GpsStatusApiTest(unittest.TestCase):
    def test_gps_status_values_nulls_and_bounds(self):
        source = (ROOT / 'components/esp_bms_idf_runtime/esp_bms_idf_runtime.c').read_text()
        harness = '''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {
 bool gps_satellite_info_valid, gps_hdop_valid;
 uint8_t gps_satellites_visible, gps_satellites_used, gps_max_cn0, gps_average_cn0, gps_constellation_mask, gps_fix_dimension;
 uint16_t gps_hdop_centi;
} esp_bms_dashboard_snapshot_t;
'''
        harness += definition(source, 'runtime_json_write_u32_or_null')
        harness += definition(source, 'runtime_gps_status_json')
        harness += '''
int main(void) {
 esp_bms_dashboard_snapshot_t s={.gps_satellite_info_valid=true,.gps_hdop_valid=true,.gps_satellites_visible=18,.gps_satellites_used=9,.gps_max_cn0=45,.gps_average_cn0=32,.gps_constellation_mask=7,.gps_fix_dimension=3,.gps_hdop_centi=85};
 char out[384];assert(runtime_gps_status_json(&s,out,sizeof(out)));printf("{%s}\\n",out);
 s.gps_satellite_info_valid=false;assert(runtime_gps_status_json(&s,out,sizeof(out)));printf("{%s}\\n",out);
 s.gps_satellite_info_valid=true;s.gps_hdop_valid=false;assert(runtime_gps_status_json(&s,out,sizeof(out)));printf("{%s}\\n",out);
 memset(&s,255,sizeof(s));s.gps_satellite_info_valid=true;s.gps_hdop_valid=true;
 assert(runtime_gps_status_json(&s,out,sizeof(out)));assert(strlen(out)<sizeof(out));
 assert(!runtime_gps_status_json(&s,out,8));return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'gps.c', Path(tmp) / 'gps'
            c.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
            rows = [json.loads(line) for line in subprocess.check_output([str(exe)], text=True).splitlines()]
        self.assertEqual(rows[0]['gps_hdop_centi'], 85)
        self.assertEqual(rows[0]['gps_constellation_mask'], 7)
        self.assertEqual(rows[0]['gps_average_cn0'], 32)
        self.assertEqual(rows[0]['gps_satellites_used'], 9)
        self.assertFalse(rows[1]['gps_satellite_info_valid'])
        self.assertTrue(all(value is None for key, value in rows[1].items() if not key.endswith('_valid')))
        self.assertIsNone(rows[2]['gps_hdop_centi'])
        self.assertEqual(rows[2]['gps_satellites_visible'], 18)
        status = definition(source, 'runtime_status_json')
        self.assertIn('runtime_gps_status_json(&runtime->snapshot, gps, sizeof(gps))', status)
        self.assertIn('gps,', status)
        self.assertGreaterEqual(int(re.search(r'#define HTTP_JSON_MAX_LEN (\d+)', source)[1]), 4096)


if __name__ == '__main__':
    unittest.main()
