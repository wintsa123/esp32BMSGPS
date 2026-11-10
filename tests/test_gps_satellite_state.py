"""用真实 NMEA 解析及 GPS 状态函数验证坏句隔离、恢复和超时。"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


ROOT = Path(__file__).resolve().parents[1]
GPS = ROOT / "components/esp_bms_gps/esp_bms_gps.c"
STREAM = ROOT / "components/esp_bms_gps/esp_bms_gps_stream.c"

STUBS = r"""
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_gps_stream.h"
typedef int uart_port_t;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef struct {
    bool gps_fix_valid, satellite_valid, hdop_valid;
    uint8_t visible, used, maximum, average, mask, dimension;
    uint16_t hdop;
    unsigned satellite_publishes, fix_publishes;
} esp_bms_idf_runtime_t;
static bool esp_bms_idf_runtime_publish_gps_satellites(
    esp_bms_idf_runtime_t *r, uint8_t visible, uint8_t used, uint8_t maximum,
    uint8_t average, uint8_t mask, uint8_t dimension, uint16_t hdop,
    bool hdop_valid, bool valid) {
    bool changed = r->visible != visible || r->used != used || r->maximum != maximum ||
        r->average != average || r->mask != mask || r->dimension != dimension ||
        r->hdop != hdop || r->hdop_valid != hdop_valid || r->satellite_valid != valid;
    r->visible = visible; r->used = used; r->maximum = maximum; r->average = average;
    r->mask = mask; r->dimension = dimension; r->hdop = hdop;
    r->hdop_valid = hdop_valid; r->satellite_valid = valid; r->satellite_publishes++;
    return changed;
}
static void esp_bms_idf_runtime_publish_gps_datetime(
    esp_bms_idf_runtime_t *r, uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second, uint64_t epoch, bool valid) {
    (void)r; (void)year; (void)month; (void)day; (void)hour; (void)minute;
    (void)second; (void)epoch; (void)valid;
}
static bool esp_bms_idf_runtime_publish_gps_position(
    esp_bms_idf_runtime_t *r, bool valid, int32_t lat, int32_t lon) {
    (void)r; (void)valid; (void)lat; (void)lon; return false;
}
static bool esp_bms_idf_runtime_publish_gps_sample(
    esp_bms_idf_runtime_t *r, bool valid, uint32_t speed) {
    (void)speed; bool changed = r->gps_fix_valid != valid;
    r->gps_fix_valid = valid; r->fix_publishes++; return changed;
}
static bool esp_bms_idf_runtime_timeout_gps(esp_bms_idf_runtime_t *r) {
    bool changed = r->gps_fix_valid; r->gps_fix_valid = false; return changed;
}
static bool gps_poll_uart(esp_bms_idf_runtime_t *r) { (void)r; return false; }
static bool gps_send_casbin(uint8_t cls, uint8_t id, const uint8_t *data, size_t len) {
    (void)cls; (void)id; (void)data; (void)len; return true;
}
"""

CASES = r"""
static const char *rmc = "GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,101126,,,A";
static const char *gsa = "GNGSA,A,3,03,07,08,11,16,22,,,,,,,1.2,0.8,0.9,1";
static const char *gsv = "GPGSV,1,1,04,03,45,120,31,07,50,230,26,08,22,050,44,11,18,300,27";
static const char *new_gsa = "GNGSA,A,2,03,07,08,11,,,,,,,,,2.1,1.3,1.7,1";
static const char *new_gsv = "GPGSV,1,1,02,03,45,120,22,07,50,230,24";
static bool feed(esp_bms_idf_runtime_t *r, const char *payload, bool corrupt) {
    char line[ESP_BMS_GPS_STREAM_CAPACITY];
    uint8_t checksum = 0;
    for (const char *p = payload; *p; p++) checksum ^= (uint8_t)*p;
    if (corrupt) checksum ^= 1;
    int length = snprintf(line, sizeof(line), "$%s*%02X", payload, checksum);
    assert(length > 0 && (size_t)length < sizeof(line));
    return gps_apply_line(r, (const uint8_t *)line, (size_t)length);
}
static void reset(esp_bms_idf_runtime_t *r) {
    memset(&s_gps, 0, sizeof(s_gps)); memset(r, 0, sizeof(*r));
    s_gps.uart_ready = true;
}
static void test_empty_and_unknown_constellations(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsa, false);
    /* 实机 GPS3 后的空信号与空星座不能清除已收到的 GPS 数据。 */
    feed(&r, "GPGSV,1,1,03,14,12,165,,17,44,103,,19,64,059,,1", false);
    assert(r.satellite_valid && r.visible == 3 && r.mask == 1);
    feed(&r, "GPGSV,1,1,00,8", false);
    feed(&r, "GLGSV,1,1,00,1", false);
    feed(&r, "GAGSV,1,1,00,7", false);
    assert(r.satellite_valid && r.visible == 3 && r.mask == 1);
    for (unsigned system = 2; system <= 5; system++) {
        char payload[96];
        snprintf(payload, sizeof(payload), "GNGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9,%u", system);
        feed(&r, payload, false);
        assert(r.used == 6 && r.dimension == 3 && r.hdop == 80);
    }
    /* Galileo/QZSS 可以计数，但不能冒充当前三模掩码中的 GPS/北斗/GLONASS。 */
    feed(&r, "GAGSV,1,1,02,301,45,120,31,302,40,100,27,7", false);
    assert(r.visible == 5 && r.mask == 1);
    feed(&r, "GQGSV,1,1,01,193,45,120,31,8", false);
    assert(r.visible == 6 && r.mask == 1);
    feed(&r, "BDGSV,1,1,02,201,45,120,31,202,40,100,27,1", false);
    assert(r.visible == 8 && r.mask == 3);
    feed(&r, "GLGSV,1,1,01,65,45,120,31,1", false);
    assert(r.visible == 9 && r.mask == 7);
}
static void test_signals_do_not_duplicate_satellites(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsa, false);
    feed(&r, "GPGSV,1,1,03,01,45,120,30,02,40,100,40,03,30,090,50,1", false);
    feed(&r, "GPGSV,1,1,03,01,45,120,20,02,40,100,30,03,30,090,40,5", false);
    assert(r.visible == 3 && r.average == 40 && r.maximum == 50);
    feed(&r, "BDGSV,1,1,02,201,45,120,35,202,40,100,45,1", false);
    assert(r.visible == 5 && r.mask == 3 && r.average == 40);
    feed(&r, "GPGSV,1,1,05,01,45,120,20,02,40,100,30,03,30,090,40,5", false);
    assert(r.visible == 7 && r.average == 40);
    /* 少样本信号仍可提供最大 C/N0，平均值继续采用样本最多的信号组。 */
    feed(&r, "GPGSV,1,1,01,01,45,120,75,9", false);
    assert(r.visible == 7 && r.average == 40 && r.maximum == 75);
    feed(&r, "GPGSV,1,1,03,01,45,120,30,02,40,100,40,03,30,090,50,1", false);
    assert(r.visible == 7 && r.average == 40 && r.maximum == 75);
}
static void test_full_gsv_cache_rejects_unknown_continuation(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsa, false);
    assert(GPS_GSV_GROUP_COUNT == 16U);
    for (unsigned signal = 1; signal <= GPS_GSV_GROUP_COUNT; signal++) {
        char payload[96];
        snprintf(payload, sizeof(payload), "GPGSV,1,1,01,01,45,120,30,%u", signal);
        feed(&r, payload, false);
    }
    for (unsigned i = 0; i < GPS_GSV_GROUP_COUNT; i++) {
        assert(s_gps.gsv_groups[i].occupied && s_gps.gsv_groups[i].complete);
    }
    gps_gsv_group_t groups_before[GPS_GSV_GROUP_COUNT];
    unsigned char runtime_before[sizeof(r)];
    memcpy(groups_before, s_gps.gsv_groups, sizeof(groups_before));
    memcpy(runtime_before, &r, sizeof(r));
    /* 未知 key 的后续页不能淘汰已完成组，也不能触发发布。 */
    assert(!feed(&r, "GPGSV,2,2,05,05,45,120,90,99", false));
    assert(!memcmp(groups_before, s_gps.gsv_groups, sizeof(groups_before)));
    assert(!memcmp(runtime_before, &r, sizeof(r)));
}
static void test_gsv_complete_ordered_batch_only(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsa, false);
    feed(&r, "GPGSV,1,1,02,01,45,120,22,02,40,100,24,1", false);
    const char *first = "GPGSV,3,1,09,01,45,120,10,02,40,100,20,03,30,090,30,04,20,080,40,1";
    const char *second = "GPGSV,3,2,09,05,45,120,50,06,40,100,60,07,30,090,70,08,20,080,80,1";
    const char *third = "GPGSV,3,3,09,09,45,120,90,1";
    unsigned publishes = r.satellite_publishes;
    assert(!feed(&r, third, false));
    assert(!feed(&r, first, false));
    assert(!feed(&r, third, false));
    assert(!feed(&r, second, false));
    assert(r.satellite_valid && r.visible == 2 && r.average == 23);
    assert(r.satellite_publishes == publishes);
    assert(!feed(&r, first, false));
    assert(!feed(&r, second, false));
    assert(r.visible == 2 && r.satellite_publishes == publishes);
    assert(feed(&r, third, false));
    assert(r.visible == 9 && r.average == 50 && r.maximum == 90);
    assert(r.satellite_publishes == publishes + 1);
}
static void test_gsa_system_replacement_and_accumulation(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsv, false);
    feed(&r, gsa, false);
    feed(&r, new_gsa, false);
    assert(r.used == 4 && r.dimension == 2 && r.hdop == 130);
    feed(&r, "GNGSA,A,3,65,66,,,,,,,,,,,1.2,0.9,0.9,2", false);
    assert(r.used == 6 && r.dimension == 3 && r.hdop == 90);
    feed(&r, "GNGSA,A,2,301,302,,,,,,,,,,,1.2,0.4,0.9,3", false);
    assert(r.used == 8 && r.dimension == 3 && r.hdop == 90);
    feed(&r, "GNGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9,4", false);
    assert(r.used == 8 && r.dimension == 3 && r.hdop == 90);
    /* 相同 system 即使换 talker 也属于替换，不能重复累计。 */
    feed(&r, "GPGSA,A,3,03,07,,,,,,,,,,,1.2,1.2,0.9,1", false);
    assert(r.used == 6 && r.dimension == 3 && r.hdop == 90);
}
static void test_only_expired_group_is_removed(void) {
    esp_bms_idf_runtime_t r;
    reset(&r);
    feed(&r, gsa, false);
    feed(&r, gsv, false);
    esp_bms_gps_tick(&r, 6000);
    feed(&r, "GNGSA,A,2,65,66,,,,,,,,,,,1.2,1.3,0.9,2", false);
    feed(&r, "GLGSV,1,1,02,65,45,120,30,66,40,100,40,1", false);
    assert(r.used == 8 && r.visible == 6 && r.mask == 5 && r.dimension == 3);
    esp_bms_gps_tick(&r, 5999);
    assert(r.satellite_valid && r.visible == 6);
    esp_bms_gps_tick(&r, 1);
    assert(r.satellite_valid && r.visible == 2 && r.used == 2 && r.mask == 4);
    assert(r.dimension == 2 && r.hdop == 130 && r.average == 35);
    esp_bms_gps_tick(&r, 5999);
    assert(r.satellite_valid && r.visible == 2);
    esp_bms_gps_tick(&r, 1);
    assert(!r.satellite_valid && r.visible == 0 && r.used == 0 && r.mask == 0);
}
int main(void) {
    test_empty_and_unknown_constellations();
    test_signals_do_not_duplicate_satellites();
    test_full_gsv_cache_rejects_unknown_continuation();
    test_gsv_complete_ordered_batch_only();
    test_gsa_system_replacement_and_accumulation();
    test_only_expired_group_is_removed();
    esp_bms_idf_runtime_t runtime;
    reset(&runtime);
    /* 没有有效数据时，坏校验句不能制造卫星有效状态。 */
    assert(!feed(&runtime, gsa, true));
    assert(!feed(&runtime, gsv, true));
    assert(!runtime.satellite_valid && !runtime.gps_fix_valid);
    assert(runtime.satellite_publishes == 0 && runtime.fix_publishes == 0);
    assert(s_gps.satellite_parse_errors == 2);

    assert(feed(&runtime, rmc, false));
    feed(&runtime, gsa, false);
    assert(!runtime.satellite_valid);
    feed(&runtime, gsv, false);
    assert(runtime.satellite_valid && runtime.gps_fix_valid);
    assert(runtime.visible == 4 && runtime.used == 6 && runtime.dimension == 3);
    assert(runtime.hdop_valid && runtime.hdop == 80 && runtime.mask == 1);
    esp_bms_idf_runtime_t before = runtime;
    /* 坏 GSA/GSV 不清空上次数据，也不能触碰独立的 RMC 定位状态。 */
    for (unsigned i = 0; i < 10; i++) {
        assert(!feed(&runtime, gsa, true));
        assert(!feed(&runtime, gsv, true));
        assert(!memcmp(&before, &runtime, sizeof(runtime)));
    }
    assert(s_gps.satellite_parse_errors == 22);
    /* 校验正确但字段无效的卫星句也必须被隔离。 */
    assert(!feed(&runtime, "GNGSA,A,9,,,,,,,,,,,,,1.2,0.8,0.9,1", false));
    assert(!feed(&runtime, "GPGSV,0,0,00", false));
    assert(!memcmp(&before, &runtime, sizeof(runtime)));
    feed(&runtime, new_gsa, false);
    feed(&runtime, new_gsv, false);
    assert(runtime.satellite_valid && runtime.gps_fix_valid);
    assert(runtime.visible == 2 && runtime.used == 4 && runtime.dimension == 2);
    assert(runtime.hdop == 130 && runtime.average == 23);

    /* 低流量 GSA/GSV 每 9 秒一批，不能沿用 RMC 的 3 秒截止线。 */
    assert(GPS_SATELLITE_TIMEOUT_MS == 12000U && GPS_RMC_TIMEOUT_MS == 3000U);
    esp_bms_gps_tick(&runtime, 3000U);
    assert(runtime.satellite_valid && !runtime.gps_fix_valid);
    feed(&runtime, rmc, false);
    esp_bms_gps_tick(&runtime, 6000U);
    assert(runtime.satellite_valid && !runtime.gps_fix_valid);
    /* 第 9 秒到达的新一批续期，之后再按完整 12 秒计算卫星超时。 */
    feed(&runtime, gsa, false);
    feed(&runtime, gsv, false);
    feed(&runtime, rmc, false);
    assert(runtime.satellite_valid && runtime.gps_fix_valid);
    /* 用真实 tick 验证 11999/12000ms 截止线，RMC 独立刷新。 */
    esp_bms_gps_tick(&runtime, GPS_SATELLITE_TIMEOUT_MS - 1);
    assert(runtime.satellite_valid);
    feed(&runtime, rmc, false);
    assert(esp_bms_gps_tick(&runtime, 1));
    assert(!runtime.satellite_valid && runtime.gps_fix_valid);
    feed(&runtime, gsa, false);
    assert(!runtime.satellite_valid);
    feed(&runtime, gsv, false);
    assert(runtime.satellite_valid && runtime.gps_fix_valid);

    /* 另一种卫星报文持续到达，也不能掩盖 GSA 单独超时。 */
    esp_bms_gps_tick(&runtime, GPS_SATELLITE_TIMEOUT_MS - 1);
    feed(&runtime, gsv, false); feed(&runtime, rmc, false);
    assert(esp_bms_gps_tick(&runtime, 1));
    assert(!runtime.satellite_valid && runtime.gps_fix_valid);
    feed(&runtime, gsa, false);
    assert(runtime.satellite_valid);
    /* 不再收到 RMC 时，3 秒独立超时不能提前撤销仍新鲜的卫星信息。 */
    esp_bms_gps_tick(&runtime, GPS_RMC_TIMEOUT_MS);
    assert(!runtime.gps_fix_valid && runtime.satellite_valid);
    esp_bms_gps_tick(&runtime, GPS_SATELLITE_TIMEOUT_MS - GPS_RMC_TIMEOUT_MS);
    assert(!runtime.gps_fix_valid && !runtime.satellite_valid);
    return 0;
}
"""


def extract_function(source, name):
    signature = re.search(
        rf"^((?:static\s+)?[A-Za-z_][A-Za-z0-9_ *]*\b{name}\s*\([^;]*?\))\s*\{{",
        source, re.M | re.S,
    )
    if not signature:
        raise AssertionError(f"missing GPS function: {name}")
    return signature.group(1) + function_body(GPS, name)


class GpsSatelliteStateTest(unittest.TestCase):
    def test_real_parser_state_survives_bad_sentences_but_expires(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 gcc 或 clang")
        source = GPS.read_text(encoding="utf-8")
        # 直接使用当前状态类型，新增诊断字段无需复制到测试中。
        types = source[source.index("typedef enum {"):source.index("static esp_bms_gps_state_t s_gps;")]
        constants = "\n".join(re.findall(r"^#define GPS_[^\n]+", source, re.M))
        functions = "\n".join(extract_function(source, name) for name in (
            "gps_publish_satellite_groups", "gps_store_gsa", "gps_store_gsv",
            "gps_age_satellite_groups",
            "gps_line_is_satellite_sentence", "gps_invalidate_satellite_info",
            "gps_parse_two_digits", "gps_parse_utc_time", "gps_parse_coordinate_e7",
            "gps_parse_rmc", "gps_is_leap_year", "gps_days_in_month",
            "gps_utc_to_local_utc8", "gps_utc_epoch_s", "gps_apply_line", "esp_bms_gps_tick",
        ))
        harness = STUBS + constants + "\n" + types + "\nstatic esp_bms_gps_state_t s_gps;\n" + functions + CASES
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            c_path = path / "gps_state.c"
            executable = path / "gps_state.exe"
            c_path.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(STREAM.parent / "include"), str(c_path), str(STREAM),
                 "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
