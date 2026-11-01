"""编译真实遥测投影与 JSON 函数，回归增量帧合并和诊断输出。"""

import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body

ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / "components/esp_bms_bms_ble/esp_bms_bms_ble.c"
RUNTIME = ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
UI = ROOT / "components/esp_bms_lvgl_ui/include/esp_bms_lvgl_ui.h"

STUBS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_lvgl_ui.h"
#include "esp_bms_bluebabe_protocol.h"
#include "esp_bms_em02_protocol.h"
#include "esp_bms_ride_records.h"
enum { ESP_BMS_IDF_BMS_TYPE_ANT, ESP_BMS_IDF_BMS_TYPE_JK, ESP_BMS_IDF_BMS_TYPE_JBD,
       ESP_BMS_IDF_BMS_TYPE_DALY, ESP_BMS_IDF_BMS_TYPE_YANYANG,
       ESP_BMS_IDF_BMS_TYPE_BLUEBABE, ESP_BMS_IDF_BMS_TYPE_MINGTANG,
       ESP_BMS_IDF_BMS_TYPE_EM02 };
enum { BMS_SNAPSHOT_DIRTY, BMS_DEVICE_INFO_KNOWN };
enum { ANT_BLE_PROTOCOL_PROBE_OLD, ANT_BLE_PROTOCOL_OLD, ANT_BLE_PROTOCOL_NEW };
typedef struct {
    esp_bms_dashboard_snapshot_t snapshot;
    int64_t bms_telemetry_last_us;
    struct { bool anchor_valid; } trip_efficiency;
    uint8_t bms_type;
    bool runtime_flags[2];
    uint8_t bms_frame[ESP_BMS_EM02_STREAM_CAPACITY];
    uint16_t bms_frame_len;
    uint8_t bms_poll_index;
    void *capacity_estimate_lock;
    struct { bool ready; uint32_t estimate_mah; } capacity_estimate;
} esp_bms_idf_runtime_t;
#define RUNTIME_SET_FLAG(r,f,v) ((r)->runtime_flags[f]=(v))
#define RUNTIME_FLAG(r,f) ((r)->runtime_flags[f])
#define RUNTIME_SET_SNAPSHOT_FLAG(r,f,v) esp_bms_dashboard_snapshot_flag_set(&(r)->snapshot,ESP_BMS_DASHBOARD_FLAG_##f,v)
#define RUNTIME_SNAPSHOT_FLAG(r,f) esp_bms_dashboard_snapshot_flag_get(&(r)->snapshot,ESP_BMS_DASHBOARD_FLAG_##f)
static int64_t now_us=100;
static int64_t esp_timer_get_time(void) { return now_us; }
static bool esp_bms_idf_runtime_observe_bms_capacity(esp_bms_idf_runtime_t *r,bool n,uint32_t c,uint16_t s)
{(void)r;(void)n;(void)c;(void)s;return true;}
static bool esp_bms_idf_runtime_observe_bms_capacity_from_current(esp_bms_idf_runtime_t *r,bool n,int16_t c,uint16_t s)
{(void)r;(void)n;(void)c;(void)s;return true;}
static bool esp_bms_idf_runtime_record_bms_sample(esp_bms_idf_runtime_t *r,const esp_bms_ride_record_sample_t *s)
{(void)r;(void)s;return true;}
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static int xSemaphoreTake(void *lock,int timeout) {(void)lock;(void)timeout;return 0;}
static void xSemaphoreGive(void *lock) {(void)lock;}
static bool runtime_bms_supports_capacity_estimate(uint8_t type) {(void)type;return false;}
static bool runtime_capacity_estimate_identity_matches(esp_bms_idf_runtime_t *r) {(void)r;return false;}
static const char *runtime_speed_unit_config_text(int unit) {(void)unit;return "kmh";}
static const char *runtime_wifi_config_text(int state) {(void)state;return "off";}
static int esp_app_get_elf_sha256(char *out,size_t len) {snprintf(out,len,"abcdef");return 0;}
/* Other protocol branches must stay unreachable in this EM02 integration case. */
#define UNEXPECTED_FEED(...) (assert(!"unexpected protocol branch"), false)
#define esp_bms_yanyang_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_ant_protocol_old_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_jk_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_jbd_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_mingtang_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_daly_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
#define esp_bms_ant_protocol_feed(...) UNEXPECTED_FEED(__VA_ARGS__)
'''

CASES = r'''
static void finish_em02_frame(uint8_t *frame, size_t length) {
    uint16_t crc=0xFFFFU;
    for(size_t i=0;i<length-2U;i++) {
        crc^=frame[i];
        for(unsigned bit=0;bit<8;bit++)
            crc=(crc&1U)?(uint16_t)((crc>>1U)^0xA001U):(uint16_t)(crc>>1U);
    }
    frame[length-2U]=(uint8_t)crc;frame[length-1U]=(uint8_t)(crc>>8U);
}
static void em02_projection_cases(void) {
    uint8_t identity[183]={1,3,178}, realtime[113]={1,3,108};
    memcpy(identity+67,"LX-EM02",7);finish_em02_frame(identity,sizeof(identity));
    realtime[3]=2;
    realtime[25]=0xD4;realtime[26]=0x30; /* 12500mV, little endian */
    realtime[29]=0x2E;realtime[30]=0xFB;realtime[31]=0xFF;realtime[32]=0xFF;
    realtime[33]=0xAC;realtime[34]=0x0D;realtime[35]=0xB0;realtime[36]=0x0D;
    realtime[105]=0xF4;realtime[106]=1;realtime[109]=88;
    finish_em02_frame(realtime,sizeof(realtime));
    esp_bms_idf_runtime_t r={.bms_type=ESP_BMS_IDF_BMS_TYPE_EM02};
    now_us=300;
    assert(bms_frame_push(&r,realtime,sizeof(realtime)));
    assert(!RUNTIME_FLAG(&r,BMS_DEVICE_INFO_KNOWN));
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE));
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,PACK_VOLTAGE_VALID));
    assert(r.snapshot.pack_voltage_mv==0 && r.bms_telemetry_last_us==0);
    uint8_t combined[sizeof(identity)+sizeof(realtime)];
    memcpy(combined,identity,sizeof(identity));
    memcpy(combined+sizeof(identity),realtime,sizeof(realtime));
    now_us=400;
    assert(bms_frame_push(&r,combined,sizeof(combined)));
    assert(r.bms_frame_len==0 && RUNTIME_FLAG(&r,BMS_DEVICE_INFO_KNOWN));
    assert(RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE));
    assert(RUNTIME_SNAPSHOT_FLAG(&r,PACK_VOLTAGE_VALID));
    assert(r.snapshot.pack_voltage_mv==12500 && r.snapshot.current_deci_amps==-123);
    assert(r.snapshot.soc_percent==88 && r.bms_telemetry_last_us==400);
    for(unsigned page=0;page<2;page++) {
        uint8_t auxiliary[101]={1,3,0};
        auxiliary[2]=page?96:90;
        size_t length=(size_t)auxiliary[2]+5U;
        finish_em02_frame(auxiliary,length);now_us=500+page;
        assert(bms_frame_push(&r,auxiliary,length));
        assert(r.bms_frame_len==0 && RUNTIME_FLAG(&r,BMS_DEVICE_INFO_KNOWN));
        assert(RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE));
        assert(r.snapshot.pack_voltage_mv==12500 && r.snapshot.current_deci_amps==-123);
        assert(r.bms_telemetry_last_us==400);
    }
    memset(identity+67,0,26);memcpy(identity+67,"OTHER-BMS",9);
    finish_em02_frame(identity,sizeof(identity));now_us=600;
    assert(bms_frame_push(&r,identity,sizeof(identity)));
    assert(!RUNTIME_FLAG(&r,BMS_DEVICE_INFO_KNOWN));
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE));
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,PACK_VOLTAGE_VALID));
    assert(r.snapshot.pack_voltage_mv==0 && r.bms_telemetry_last_us==0);
    assert(strcmp(r.snapshot.bms_info_text,"BMS MODEL")==0);
    now_us=700;
    assert(bms_frame_push(&r,realtime,sizeof(realtime)));
    assert(r.bms_frame_len==0 && !RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE));
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,PACK_VOLTAGE_VALID));
    assert(r.snapshot.pack_voltage_mv==0 && r.bms_telemetry_last_us==0);
}
static void blue_page(esp_bms_idf_runtime_t *r, bool identity, bool zero) {
    uint8_t frame[153]={0}, stream[320]={0};
    size_t length=identity?153U:130U, used=0;
    frame[0]=0xFA;frame[1]=0x50;frame[2]=0xA0;frame[3]=0x28;frame[4]=0x15;
    frame[6]=(uint8_t)(length-10U);frame[13]=identity?2U:1U;frame[15]=(uint8_t)(length-19U);
    uint8_t *p=frame+16;
    if(identity) { p[50]=0xC3;p[51]=0x50; } /* 50000mAh */
    else { p[0]=0x14;p[1]=0xC8; p[3]=0x00;p[4]=0x04;p[5]=zero?0:0xD2;
           if(zero) { p[4]=0; } p[6]=zero?0:3;p[7]=zero?0:0x20;
           p[14]=25;p[82]=0x0D;p[83]=0x02;p[84]=0x0C;p[85]=0xE4;p[87]=30; }
    uint16_t crc=esp_bms_bluebabe_crc16(frame,length-3U);
    frame[length-3U]=(uint8_t)(crc>>8);frame[length-2U]=(uint8_t)crc;frame[length-1U]=0xE5;
    esp_bms_bms_telemetry_t t={0};
    assert(esp_bms_bluebabe_feed(stream,&used,sizeof(stream),frame,length,&t));
    assert(bms_apply_telemetry(r,&t));
}
int main(void) {
    esp_bms_idf_runtime_t r={.bms_type=ESP_BMS_IDF_BMS_TYPE_BLUEBABE};
    blue_page(&r,true,false);
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE) && r.bms_telemetry_last_us==0);
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,PACK_VOLTAGE_VALID));
    assert(RUNTIME_SNAPSHOT_FLAG(&r,TOTAL_CAPACITY_VALID) && r.snapshot.total_capacity_mah==50000);
    blue_page(&r,false,false);
    assert(RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE) && r.snapshot.pack_voltage_mv==53200);
    assert(RUNTIME_SNAPSHOT_FLAG(&r,CURRENT_VALID) && r.snapshot.current_deci_amps==123);
    assert(RUNTIME_SNAPSHOT_FLAG(&r,SOC_VALID) && r.snapshot.soc_percent==80);
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,AVERAGE_CELL_VALID));
    assert(r.snapshot.total_capacity_mah==50000 && r.bms_telemetry_last_us==100);
    now_us=200;blue_page(&r,true,false);
    assert(r.snapshot.pack_voltage_mv==53200 && r.snapshot.current_deci_amps==123);
    assert(r.bms_telemetry_last_us==100);
    assert(esp_bms_dashboard_snapshot_temperature_valid(&r.snapshot,0) && r.snapshot.bms_temperature_celsius[0]==25);
    blue_page(&r,false,true);
    assert(RUNTIME_SNAPSHOT_FLAG(&r,CURRENT_VALID) && r.snapshot.current_deci_amps==0);
    assert(RUNTIME_SNAPSHOT_FLAG(&r,SOC_VALID) && r.snapshot.soc_percent==0);
    assert(r.snapshot.total_capacity_mah==50000 && r.bms_telemetry_last_us==200);
    esp_bms_bms_telemetry_t t={.partial=true,.present_fields=ESP_BMS_BMS_FIELD_TEMPERATURES};
    assert(bms_apply_telemetry(&r,&t));
    assert(!esp_bms_dashboard_snapshot_temperature_valid(&r.snapshot,0));
    r.snapshot.bms_ant_diagnostics.valid=true;
    bms_clear_telemetry(&r);
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,BMS_ONLINE) && !r.snapshot.bms_ant_diagnostics.valid);
    assert(!RUNTIME_SNAPSHOT_FLAG(&r,TOTAL_CAPACITY_VALID));
    t=(esp_bms_bms_telemetry_t){.pack_voltage_mv=52000,.soc_percent=0,.current_deci_amps=0};
    t.ant_diagnostics.valid=true;t.ant_diagnostics.cell_count=32;
    t.ant_diagnostics.power_w=INT32_MIN;t.ant_diagnostics.balance_mask=UINT32_MAX;
    t.ant_diagnostics.charge_time_valid=true;t.ant_diagnostics.charge_time_seconds=UINT32_MAX;
    t.ant_diagnostics.last_charge_gap_valid=true;t.ant_diagnostics.last_charge_gap_seconds=UINT32_MAX;
    t.ant_diagnostics.remaining_charge_valid=true;t.ant_diagnostics.remaining_charge_minutes=UINT16_MAX;
    for(unsigned i=0;i<32;i++) t.ant_diagnostics.cell_voltage_mv[i]=UINT16_MAX;
    t.protection_mask=(UINT64_C(1)<<1)|(UINT64_C(1)<<7)|(UINT64_C(1)<<16);
    t.balancing_supported=true;t.balancing_active=true;r.bms_type=ESP_BMS_IDF_BMS_TYPE_ANT;
    assert(bms_apply_telemetry(&r,&t));
    assert(RUNTIME_SNAPSHOT_FLAG(&r,CURRENT_VALID) && RUNTIME_SNAPSHOT_FLAG(&r,SOC_VALID));
    assert(r.snapshot.bms_safety_active_mask==(ESP_BMS_BMS_SAFETY_CELL_OV|ESP_BMS_BMS_SAFETY_CELL_DELTA|ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT|ESP_BMS_BMS_SAFETY_BALANCING));
    assert(r.snapshot.bms_ant_diagnostics.power_w==INT32_MIN);
    assert(r.snapshot.bms_ant_diagnostics.cell_voltage_mv[31]==UINT16_MAX);
    r.snapshot.flags=UINT32_MAX;
    r.snapshot.pack_voltage_mv=UINT32_MAX;r.snapshot.total_capacity_mah=UINT32_MAX;
    r.snapshot.capacity_remaining_mah=UINT32_MAX;r.snapshot.local_battery_mv=UINT32_MAX;
    r.snapshot.bms_running_time_seconds=UINT32_MAX;r.snapshot.bms_running_time_valid=true;
    r.snapshot.bms_cycle_capacity_mah=UINT32_MAX;r.snapshot.bms_cycle_capacity_valid=true;
    r.snapshot.controller_power_w=INT32_MIN;r.snapshot.controller_temp_c=INT16_MIN;
    r.snapshot.motor_temp_c=INT16_MIN;r.snapshot.current_deci_amps=INT16_MIN;
    r.snapshot.soc_percent=UINT16_MAX;r.snapshot.max_cell_voltage_mv=UINT16_MAX;
    r.snapshot.min_cell_voltage_mv=UINT16_MAX;r.snapshot.delta_cell_voltage_mv=UINT16_MAX;
    r.snapshot.average_cell_voltage_mv=UINT16_MAX;r.snapshot.controller_rpm=UINT16_MAX;
    r.snapshot.controller_speed_deci_units=UINT16_MAX;r.snapshot.controller_gear=UINT8_MAX;
    memset(r.snapshot.firmware_version,'v',sizeof(r.snapshot.firmware_version)-1);
    for(unsigned i=0;i<ESP_BMS_BMS_TEMP_MAX_COUNT;i++) r.snapshot.bms_temperature_celsius[i]=INT16_MIN;
    for(unsigned i=0;i<ESP_BMS_BMS_CODE_MAX_COUNT;i++) {
        snprintf(r.snapshot.bms_protection_codes[i],ESP_BMS_BMS_CODE_TEXT_LEN,"P%02u",i);
        snprintf(r.snapshot.bms_warning_codes[i],ESP_BMS_BMS_CODE_TEXT_LEN,"W%02u",i);
    }
    r.snapshot.bms_protection_count=ESP_BMS_BMS_CODE_MAX_COUNT;
    r.snapshot.bms_warning_count=ESP_BMS_BMS_CODE_MAX_COUNT;
    char json[HTTP_JSON_MAX_LEN];assert(runtime_status_json(&r,json,sizeof(json)));puts(json);
    assert(!runtime_status_json(&r,json,32));
    t.ant_diagnostics.remaining_charge_valid=false;t.ant_diagnostics.charge_time_valid=false;
    assert(bms_apply_telemetry(&r,&t));
    assert(runtime_status_json(&r,json,sizeof(json)));puts(json);
    bms_clear_telemetry(&r);
    assert(runtime_status_json(&r,json,sizeof(json)));puts(json);
    em02_projection_cases();
    return 0;
}
'''


class BmsTelemetryProjectionTest(unittest.TestCase):
    def test_real_c_projection_and_json(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("需要 gcc 或 clang 执行 C 投影")
        signatures = {
            "bms_set_info": "static void bms_set_info(esp_bms_idf_runtime_t *runtime,const char *text)",
            "bms_append_code": "static void bms_append_code(char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],uint8_t *count,char prefix,uint8_t bit)",
            "bms_apply_fault_masks": "static void bms_apply_fault_masks(esp_bms_dashboard_snapshot_t *snapshot,uint64_t protection_mask,uint64_t warning_mask)",
            "bms_mask_has_any": "static bool bms_mask_has_any(uint64_t mask,uint64_t bits)",
            "bms_apply_safety_status": "static void bms_apply_safety_status(esp_bms_idf_runtime_t *runtime,const esp_bms_bms_telemetry_t *telemetry)",
            "bms_apply_telemetry": "static bool bms_apply_telemetry(esp_bms_idf_runtime_t *runtime,const esp_bms_bms_telemetry_t *telemetry)",
            "bms_clear_telemetry": "static void bms_clear_telemetry(esp_bms_idf_runtime_t *runtime)",
        }
        for protocol in ["yanyang", "jk", "jbd", "daly", "bluebabe", "mingtang", "em02"]:
            signatures[f"bms_is_{protocol}"] = (
                f"static bool bms_is_{protocol}(const esp_bms_idf_runtime_t *runtime)"
            )
        signatures["bms_frame_push"] = (
            "static bool bms_frame_push(esp_bms_idf_runtime_t *runtime,const uint8_t *chunk,size_t chunk_len)"
        )
        json_signatures = {
            "runtime_json_write_u32_or_null": "static bool runtime_json_write_u32_or_null(char *out,size_t out_len,bool valid,uint32_t value)",
            "runtime_json_write_i32_or_null": "static bool runtime_json_write_i32_or_null(char *out,size_t out_len,bool valid,int32_t value)",
            "runtime_json_write_bms_codes": "static bool runtime_json_write_bms_codes(char *out,size_t out_len,const char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],uint8_t count)",
            "runtime_json_write_bms_temperatures": "static bool runtime_json_write_bms_temperatures(char *out,size_t out_len,const esp_bms_dashboard_snapshot_t *snapshot)",
            "runtime_json_write_ant_diagnostics": "static bool runtime_json_write_ant_diagnostics(char *out,size_t out_len,const esp_bms_ant_snapshot_diagnostics_t *diag)",
            "runtime_status_json": "static bool runtime_status_json(esp_bms_idf_runtime_t *runtime,char *json,size_t json_len)",
        }
        functions = "\n".join(signature + function_body(path, name)
                              for path, mapping in [(BLE, signatures), (RUNTIME, json_signatures)]
                              for name, signature in mapping.items())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            features = ["LABEL", "CANVAS", "IMAGE", "QRCODE", "ROLLER"]
            (path / "lvgl.h").write_text("#pragma once\ntypedef struct lv_display_t lv_display_t;\n"
                + "#define LVGL_VERSION_MAJOR 9\n#define LVGL_VERSION_MINOR 5\n#define LVGL_VERSION_PATCH 0\n"
                + "".join(f"#define LV_USE_{name} 1\n" for name in features))
            (path / "sdkconfig.h").write_text("#define CONFIG_LVGL_VERSION_MAJOR 9\n#define CONFIG_LVGL_VERSION_MINOR 5\n#define CONFIG_LVGL_VERSION_PATCH 0\n")
            (path / "esp_err.h").write_text("typedef int esp_err_t;\n")
            harness = path / "projection.c"
            executable = path / "projection"
            size_assert = re.search(
                r'_Static_assert\(sizeof\(esp_bms_dashboard_snapshot_t\).*?\);',
                (UI.parent.parent / "esp_bms_lvgl_ui_internal.h").read_text(), re.S,
            ).group()
            json_limit = re.search(r"^#define HTTP_JSON_MAX_LEN \d+U$", RUNTIME.read_text(), re.M).group()
            harness.write_text(json_limit + "\n" + STUBS + size_assert + functions + CASES)
            includes = [path, UI.parent, ROOT / "components/esp_bms_lvgl_contract/include",
                        ROOT / "components/esp_bms_idf_runtime/include",
                        ROOT / "components/esp_bms_bms_ble/protocols/bluebabe",
                        ROOT / "components/esp_bms_bms_ble/protocols/em02"]
            result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                *[f"-I{item}" for item in includes], str(harness),
                str(ROOT / "components/esp_bms_bms_ble/protocols/bluebabe/esp_bms_bluebabe_protocol.c"),
                str(ROOT / "components/esp_bms_bms_ble/protocols/em02/esp_bms_em02_protocol.c"),
                "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            full, missing, cleared = map(json.loads, result.stdout.splitlines())
            diag = full["bms_ant_diagnostics"]
            self.assertEqual(len(diag["cell_voltage_mv"]), 32)
            self.assertEqual(diag["power_w"], -(2**31))
            self.assertEqual(diag["charge_time_seconds"], 2**32 - 1)
            self.assertIsNone(diag["remaining_discharge_minutes"])
            self.assertIsNone(missing["bms_ant_diagnostics"]["charge_time_seconds"])
            self.assertIsNone(cleared["bms_ant_diagnostics"])


if __name__ == "__main__":
    unittest.main()
