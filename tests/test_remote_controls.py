"""Compile the real remote dispatcher with fake transport and display queue."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

class RemoteControlsTest(unittest.TestCase):
    def test_dispatch_and_manifest(self):
        source = (ROOT / 'components/esp_bms_idf_runtime/esp_bms_remote_controls.inc').read_text()
        actions = sorted(a for a in set(re.findall(r'ESP_BMS_LVGL_ACTION_\w+', source)) if '_EVENT_FLAG_' not in a)
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define ESP_OK 0
#define ESP_BMS_DISPLAY_SERVICE_COMMAND_REMOTE_ACTION 1
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_COMMITTED 1
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID 2
#define ESP_BMS_LVGL_ACTION_EVENT_FLAG_CONTROLLER_SETTING_VALID 4
'''
        harness += 'typedef enum {' + ','.join(actions) + '} esp_bms_lvgl_action_t;\n'
        harness += r'''
typedef struct { int action, flags, numeric_delta, controller_tire_rim_inch, controller_tire_width_mm, controller_tire_aspect_percent, controller_gear_ratio_centi; } esp_bms_lvgl_action_event_t;
typedef struct { int kind; union { esp_bms_lvgl_action_event_t remote_action; } data; } esp_bms_display_service_command_t;
typedef struct { struct { int speed_dashboard_style, boot_animation_style, preset_range_km; } snapshot; uint16_t controller_fallback_tire_rim_inch, controller_fallback_tire_width_mm, controller_fallback_tire_aspect_percent; struct { int fallback_gear_ratio_centi; } controller_state; } esp_bms_idf_runtime_t;
static const char *test_id; static uint32_t test_value; static bool valid_value = true, enabled = true; static int sends, result; static esp_bms_lvgl_action_event_t sent;
static bool runtime_action_feature_enabled(esp_bms_lvgl_action_t a) { (void)a; return enabled; }
static bool runtime_json_get_string(const char *b,const char *f,char *out,size_t n,bool *found) { (void)b;(void)f; *found=true; snprintf(out,n,"%s",test_id);return true; }
static bool runtime_json_get_u32(const char *b,const char *f,uint32_t *out,bool *found) { (void)b;(void)f; *found=valid_value; *out=test_value; return valid_value; }
static bool runtime_speed_dashboard_style_matches_policy(int v) {return v<2;}
static bool runtime_boot_animation_style_matches_policy(int v) {return v<2;}
static bool runtime_controller_tire_matches_policy(int r,int a,int w) {return r>=8&&r<=24&&a>=30&&a<=100&&a%5==0&&w>=50&&w<=200&&w%5==0;}
static int esp_bms_display_service_submit_command(const esp_bms_display_service_command_t *c, int timeout) { (void)timeout; sends++;sent=c->data.remote_action;return result; }
static bool runtime_json_append(char *out,size_t len,size_t *offset,const char *format,...) {va_list ap;va_start(ap,format);int n=vsnprintf(out+*offset,len-*offset,format,ap);va_end(ap);if(n<0||(size_t)n>=len-*offset)return false;*offset+=n;return true;}
'''
        harness += source
        harness += r'''
int main(void) {
 esp_bms_idf_runtime_t r={.controller_fallback_tire_rim_inch=12,.controller_fallback_tire_width_mm=100,.controller_fallback_tire_aspect_percent=80}; const char *error=NULL;
 test_id="ratio";test_value=250;assert(runtime_remote_control(&r,"",&error)==204);assert(sent.controller_gear_ratio_centi==250);assert(sends==1);
 test_value=1001;assert(runtime_remote_control(&r,"",&error)==400);assert(sends==1);
 test_value=49;assert(runtime_remote_control(&r,"",&error)==400);
 valid_value=false;assert(runtime_remote_control(&r,"",&error)==400);valid_value=true;
 test_id="tire_width";test_value=101;assert(runtime_remote_control(&r,"",&error)==400);
 test_value=120;assert(runtime_remote_control(&r,"",&error)==204);assert(sent.controller_tire_width_mm==120&&sent.controller_tire_rim_inch==12&&sent.controller_tire_aspect_percent==80);
 test_id="dashboard_style";test_value=2;assert(runtime_remote_control(&r,"",&error)==400);
 test_id="unknown";assert(runtime_remote_control(&r,"",&error)==400);
 test_id="restore";enabled=false;assert(runtime_remote_control(&r,"",&error)==400);enabled=true;
 result=1;assert(runtime_remote_control(&r,"",&error)==503);result=0;
 assert(runtime_remote_control(&r,"",&error)==204);
 const char *groups[]={"display","vehicle","connection","media","system"};
 for(int i=0;i<5;i++){char path[64],json[3072];snprintf(path,sizeof(path),"/api/controls?group=%s",groups[i]);assert(runtime_remote_controls_json(&r,path,json,sizeof(json)));assert(strlen(json)<3000);puts(json);}
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'check.c'; c.write_text(harness)
            exe = Path(tmp) / 'check'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            import json
            for line in result.stdout.splitlines():
                self.assertIn('controls', json.loads(line))

if __name__ == '__main__': unittest.main()
