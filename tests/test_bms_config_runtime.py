"""Compile production configuration functions against an in-memory NVS boundary."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"


def c_function(source, name):
    match = re.search(rf"^(?:static )?(?:bool|int|esp_err_t) {name}\([^;]*?\)\s*\{{", source, re.M | re.S)
    if match is None:
        raise AssertionError(f"missing function {name}")
    # Ignore braces inside literals/comments while preserving original source positions.
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*|[{}]', source[match.end() - 1:])
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():match.end() + token.end() - 1]
    raise AssertionError(f"unterminated function {name}")


STUBS = r'''
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int esp_bms_idf_display_rotation_t;
typedef int esp_bms_speed_unit_t;
typedef int esp_bms_speed_source_t;
typedef int esp_bms_speed_dashboard_style_t;
typedef int esp_bms_idf_bms_type_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NVS_NOT_FOUND };
enum { LANGUAGE_ZH, HTTP_PENDING_LANGUAGE_ZH, HTTP_CONFIG_PENDING, HTTP_CONFIG_APPLIED };
enum { NVS_READONLY, NVS_READWRITE, ESP_BMS_SPEED_SOURCE_GPS=0,
       ESP_BMS_SPEED_SOURCE_CONTROLLER=1, ESP_BMS_BOOT_ANIMATION_CHARGE=0,
       ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER=1, ESP_BMS_CTRL_TYPE_AUTO=0,
       ESP_BMS_CTRL_TYPE_COUNT=16, ESP_BMS_IDF_BMS_TYPE_ANT=0 };
#define ESP_BMS_PRESET_RANGE_DEFAULT_KM 100U
#define ESP_BMS_REMAINING_RANGE_MAX_KM 9999U
#define CONTROLLER_RATIO_CENTI_DEFAULT 100U
#define ESP_RETURN_ON_FALSE(cond,rc,...) do {if(!(cond)) return rc;} while(0)
#define ESP_RETURN_ON_ERROR(expr,...) do {int rc=(expr);if(rc) return rc;} while(0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define RUNTIME_FLAG(r,f) ((r)->flags[f])
#define RUNTIME_SET_FLAG(r,f,v) ((r)->flags[f]=(v))
#define pdMS_TO_TICKS(v) (v)
#define pdTRUE 1
#define xSemaphoreTake(lock,ticks) ((lock)!=0)
#define xSemaphoreGive(lock) ((void)(lock))
#define runtime_brightness_matches_policy(v) ((v)<=100)
#define runtime_volume_matches_policy(v) ((v)<=100)
#define runtime_rotation_matches_policy(v) ((v)<=3)
#define runtime_speed_unit_matches_policy(v) ((v)<=1)
#define runtime_speed_source_matches_policy(v) ((v)<=1)
#define runtime_speed_dashboard_style_matches_policy(v) ((v)<=2)
#define runtime_boot_animation_style_matches_policy(v) ((v)<=2)
#define runtime_bms_type_matches_policy(v) ((v)<=7)
#define runtime_language_matches_policy(v) ((v)<=1)
#define runtime_controller_ratio_matches_policy(v) ((v)<=1000)
#define runtime_controller_tire_matches_policy(a,b,c) ((a)!=0 && (b)!=0 && (c)!=0)
#define esp_bms_display_service_default_speed_dashboard_style() 0
#define esp_fardriver_tire_circumference_mm(a,b,c,d) false
#define runtime_parse_rotation_config_text(a,b) false
#define runtime_parse_speed_unit_config_text(a,b) false
#define runtime_parse_speed_source_config_text(a,b) false
#define runtime_parse_bms_type_config_text(a,b) false
#define runtime_parse_controller_type_config_text(a,b) false

typedef struct esp_bms_idf_runtime esp_bms_idf_runtime_t;
struct driver { bool (*stop)(esp_bms_idf_runtime_t *); int (*start_if_bound)(esp_bms_idf_runtime_t *); };
struct esp_bms_idf_runtime {
    bool flags[4];
    int http_pending_lock;
    uint8_t brightness_percent, volume_percent, bms_type, bms_ant_channel, controller_type;
    uint8_t http_pending_brightness_percent, http_pending_volume_percent;
    uint8_t http_pending_bms_type, http_pending_bms_ant_channel, http_pending_controller_type;
    int display_rotation, display_rotation_default_version;
    int http_pending_display_rotation, http_pending_speed_unit, http_pending_speed_source;
    struct { int speed_unit,speed_source,speed_dashboard_style,boot_animation_style;
        bool language_zh; uint8_t bms_type,controller_type; uint16_t preset_range_km; } snapshot;
    char bluebabe_code[7], http_pending_bluebabe_code[7];
    char bms_bound_mac[18], controller_bound_mac[18], controller_bound_name[32];
    uint8_t controller_fallback_tire_rim_inch, controller_fallback_tire_aspect_percent;
    uint16_t controller_fallback_tire_width_mm;
    bool controller_connection_enabled, controller_page_enabled;
    struct { uint16_t fallback_gear_ratio_centi, fallback_wheel_circumference_mm; } controller_state;
    const struct driver *bms_ble_driver;
};
static unsigned stop_count,start_count,save_count;
static bool stop_bms(esp_bms_idf_runtime_t *r) {(void)r;stop_count++;return true;}
static int start_bms(esp_bms_idf_runtime_t *r) {(void)r;start_count++;return ESP_OK;}
static const struct driver driver={stop_bms,start_bms};
static bool runtime_set_brightness_percent(esp_bms_idf_runtime_t *r,uint8_t v) {r->brightness_percent=v;return true;}
static bool runtime_set_volume_percent(esp_bms_idf_runtime_t *r,uint8_t v) {r->volume_percent=v;return true;}
static void runtime_copy_snapshot_text(char *dst,size_t len,const char *src) {snprintf(dst,len,"%s",src);}
static bool runtime_select_bms_type(esp_bms_idf_runtime_t *r,int v) {r->bms_type=v;return true;}
static bool runtime_select_controller_type(esp_bms_idf_runtime_t *r,int v) {r->controller_type=v;return true;}
static void runtime_project_controller_snapshot(esp_bms_idf_runtime_t *r) {(void)r;}
static void runtime_update_snapshot_speed(esp_bms_idf_runtime_t *r) {(void)r;}
static void runtime_set_error(esp_bms_idf_runtime_t *r,const char *v) {(void)r;(void)v;}
static int runtime_init_nvs(esp_bms_idf_runtime_t *r) {(void)r;return ESP_OK;}

static struct { char key[20]; unsigned value; } store[40];
static size_t store_count;
static int nvs_open(const char *ns,int mode,int *handle) {(void)ns;(void)mode;*handle=1;return ESP_OK;}
static void nvs_close(int handle) {(void)handle;}
static int nvs_commit(int handle) {(void)handle;save_count++;return ESP_OK;}
static int nvs_set_u16(int handle,const char *key,uint16_t value) {
    (void)handle;size_t i=0;for(;i<store_count;i++) if(!strcmp(store[i].key,key)) break;
    if(i==store_count) {assert(store_count<40);snprintf(store[i].key,20,"%s",key);store_count++;}
    store[i].value=value;return ESP_OK;
}
static int nvs_set_u8(int handle,const char *key,uint8_t value) {return nvs_set_u16(handle,key,value);}
static int nvs_get_u16(int handle,const char *key,uint16_t *value) {
    (void)handle;for(size_t i=0;i<store_count;i++) if(!strcmp(store[i].key,key)) {*value=store[i].value;return ESP_OK;}
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_get_u8(int handle,const char *key,uint8_t *value) {
    uint16_t v;int rc=nvs_get_u16(handle,key,&v);if(rc==ESP_OK)*value=(uint8_t)v;return rc;
}
static int nvs_set_str(int handle,const char *key,const char *value) {(void)handle;(void)key;(void)value;return ESP_OK;}
static int nvs_get_str(int handle,const char *key,char *value,size_t *len) {
    (void)handle;(void)key;(void)value;(void)len;return ESP_ERR_NVS_NOT_FOUND;
}
int esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *runtime);
'''

CASES = r'''
int main(void) {
    esp_bms_idf_runtime_t r={.http_pending_lock=1,.brightness_percent=50,.volume_percent=25,.bms_ble_driver=&driver};
    strcpy(r.bms_bound_mac,"01:02:03:04:05:06");
    const char *message=NULL;
    for(unsigned value=0;value<=2;value++) {
        char json[40];snprintf(json,sizeof(json),"{\"ant_channel\":%u}",value);
        assert(runtime_apply_config_json(&r,json,&message)==204);
        assert(r.http_pending_bms_ant_channel==value);
        assert(esp_bms_idf_runtime_apply_pending_http_config(&r));
        assert(r.bms_ant_channel==value);
    }
    assert(stop_count==2 && start_count==2 && save_count==2);
    uint8_t persisted=99;
    assert(nvs_get_u8(1,NVS_ANT_CHANNEL_KEY,&persisted)==ESP_OK && persisted==2);
    const char *bad[]={"-1","3","256","1.5","1e1","\"1\"","true","null"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char json[48];snprintf(json,sizeof(json),"{\"ant_channel\":%s}",bad[i]);
        assert(runtime_apply_config_json(&r,json,&message)==400);
        assert(!RUNTIME_FLAG(&r,HTTP_CONFIG_PENDING) && r.bms_ant_channel==2);
    }
    assert(runtime_apply_config_json(&r,"{\"brightness\":70}",&message)==204);
    assert(r.http_pending_bms_ant_channel==2 && r.bms_ant_channel==2);
    assert(esp_bms_idf_runtime_apply_pending_http_config(&r));
    assert(r.bms_ant_channel==2 && r.brightness_percent==70 && start_count==2);
    /* Channel edits must not restart a different BMS implementation. */
    r.bms_type=1;
    assert(runtime_apply_config_json(&r,"{\"ant_channel\":1}",&message)==204);
    assert(esp_bms_idf_runtime_apply_pending_http_config(&r));
    assert(stop_count==2 && start_count==2 && r.bms_ant_channel==1);
    r.bms_type=0;r.bms_bound_mac[0]='\0';
    assert(runtime_apply_config_json(&r,"{\"ant_channel\":0}",&message)==204);
    assert(esp_bms_idf_runtime_apply_pending_http_config(&r));
    assert(stop_count==3 && start_count==2);
    /* Actual save/load roundtrip and missing-key migration. */
    r.bms_ant_channel=2;
    assert(esp_bms_idf_runtime_save_display_settings(&r)==ESP_OK);
    esp_bms_idf_runtime_t loaded={0};bool found=false;
    assert(esp_bms_idf_runtime_load_display_settings(&loaded,&found)==ESP_OK);
    assert(found && loaded.bms_ant_channel==2);
    for(size_t i=0;i<store_count;i++) if(!strcmp(store[i].key,NVS_ANT_CHANNEL_KEY)) {
        store[i]=store[--store_count];break;
    }
    loaded.bms_ant_channel=2;
    assert(esp_bms_idf_runtime_load_display_settings(&loaded,&found)==ESP_OK);
    assert(found && loaded.bms_ant_channel==0);
    nvs_set_u8(1,NVS_ANT_CHANNEL_KEY,255);
    assert(esp_bms_idf_runtime_load_display_settings(&loaded,&found)==ESP_OK);
    assert(loaded.bms_ant_channel==0);
    r.bms_ant_channel=3;
    assert(esp_bms_idf_runtime_save_display_settings(&r)==ESP_ERR_INVALID_STATE);
    /* A saturated queue must leave live settings untouched. */
    r.bms_ant_channel=1;r.http_pending_lock=0;
    assert(runtime_apply_config_json(&r,"{\"ant_channel\":2}",&message)==500);
    assert(r.bms_ant_channel==1 && !esp_bms_idf_runtime_apply_pending_http_config(&r));
    return 0;
}
'''


class BmsConfigRuntimeTest(unittest.TestCase):
    def test_ant_channel_parse_queue_apply_and_persist(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 C 编译器")
        source = SOURCE.read_text(encoding="utf-8")
        constants = "\n".join(re.findall(r'^#define (?:DISPLAY_NVS_\w+|CONTROLLER_NVS_\w+|NVS_ANT_CHANNEL_KEY|NVS_BLUEBABE_CODE_KEY|SETUP_AP_NVS_NAMESPACE) "[^"]+"$', source, re.M))
        functions = "\n".join(c_function(source, name) for name in (
            "runtime_json_find_field", "runtime_json_get_u8", "runtime_json_get_string",
            "runtime_nvs_get_optional_u8", "runtime_nvs_get_optional_u16", "runtime_nvs_get_optional_string",
            "esp_bms_idf_runtime_save_display_settings", "esp_bms_idf_runtime_load_display_settings",
            "runtime_set_pending_http_config", "runtime_apply_config_json",
            "esp_bms_idf_runtime_apply_pending_http_config",
        ))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            harness = path / "config.c"
            executable = path / "config"
            harness.write_text(constants + STUBS + functions + CASES, encoding="utf-8")
            result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
