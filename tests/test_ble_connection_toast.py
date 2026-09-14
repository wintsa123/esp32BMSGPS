"""真实提示状态逻辑回归，隔离设备来源并验证超时定时器交接。"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "components/esp_bms_lvgl_ui"

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
enum { SETTINGS_BLE_SOURCE_BMS, SETTINGS_BLE_SOURCE_CONTROLLER };
enum { BMS_ONLINE, CONTROLLER_ONLINE };
typedef struct { bool flags[2];char bms_info_text[16]; } esp_bms_dashboard_snapshot_t;
typedef struct { int id; } lv_timer_t;
static struct { bool quick_connecting_toast_active;uint8_t quick_connecting_toast_source;
    lv_timer_t *quick_toast_timer; } s_ui;
static lv_timer_t old_timer={1},new_timer={2};
static bool timeout_running;
static unsigned shown;
static const char *message;
#define SNAPSHOT_FLAG(s,f) ((s)->flags[f])
#define ui_t(zh,en) (en)
#define ESP_LOGI(...) ((void)0)
static void quick_toast_show_text(const char *text) {
    if(timeout_running) assert(s_ui.quick_toast_timer==NULL);
    s_ui.quick_connecting_toast_active=false;
    s_ui.quick_toast_timer=&new_timer;
    message=text;shown++;
}
void quick_toast_update_connection(const esp_bms_dashboard_snapshot_t *previous,
    const esp_bms_dashboard_snapshot_t *snapshot,bool had_previous)
__UPDATE__
static void quick_toast_connecting_timeout_cb(lv_timer_t *timer)
__TIMEOUT__
int main(void) {
    esp_bms_dashboard_snapshot_t before={0}, after={0};
    s_ui.quick_connecting_toast_active=true;
    s_ui.quick_connecting_toast_source=SETTINGS_BLE_SOURCE_CONTROLLER;
    after.flags[BMS_ONLINE]=true;
    strcpy(after.bms_info_text,"BMS CONN FAIL");
    quick_toast_update_connection(&before,&after,true);
    assert(shown==0 && s_ui.quick_connecting_toast_active);
    after.flags[CONTROLLER_ONLINE]=true;
    quick_toast_update_connection(&before,&after,true);
    assert(shown==1 && strcmp(message,"Connected")==0);
    quick_toast_update_connection(&before,&after,true);
    assert(shown==1);
    s_ui.quick_connecting_toast_active=true;
    before=after;
    quick_toast_update_connection(&before,&after,true);
    assert(shown==1); /* 已有连接不能算本次跃迁成功。 */
    s_ui.quick_connecting_toast_source=SETTINGS_BLE_SOURCE_BMS;
    memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    after.flags[CONTROLLER_ONLINE]=true;
    strcpy(after.bms_info_text,"BMS CHR");
    quick_toast_update_connection(&before,&after,true);
    assert(shown==1);
    const char *errors[]={"BMS CONN FAIL","BMS CONN ERR","BMS NO CCCD","BMS TIMEOUT"};
    for(unsigned i=0;i<4;i++) {
        s_ui.quick_connecting_toast_active=true;
        strcpy(after.bms_info_text,errors[i]);
        quick_toast_update_connection(&before,&after,true);
        assert(shown==2+i && strcmp(message,"Connection failed")==0);
    }
    s_ui.quick_connecting_toast_active=true;
    after.flags[BMS_ONLINE]=true;
    quick_toast_update_connection(&before,&after,false);
    assert(shown==5);
    quick_toast_update_connection(&before,&after,true);
    assert(shown==6 && strcmp(message,"Connected")==0);
    s_ui.quick_connecting_toast_active=true;
    s_ui.quick_toast_timer=&old_timer;
    timeout_running=true;
    quick_toast_connecting_timeout_cb(&old_timer);
    assert(shown==7 && strcmp(message,"Connection failed")==0);
    assert(!s_ui.quick_connecting_toast_active && s_ui.quick_toast_timer==&new_timer);
    return 0;
}
"""


class BleConnectionToastTest(unittest.TestCase):
    def test_real_connection_result_and_timeout(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 C 编译器")
        source = UI / "ui_quick_panel.c"
        harness = HARNESS.replace("__UPDATE__", function_body(source, "quick_toast_update_connection"))
        harness = harness.replace("__TIMEOUT__", function_body(source, "quick_toast_connecting_timeout_cb"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            code = path / "toast.c"
            executable = path / "toast.exe"
            code.write_text(harness, encoding="utf-8")
            result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(code), "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_source_capture_and_snapshot_order(self):
        body = function_body(UI / "ui_quick_panel.c", "quick_toast_show_connecting")
        self.assertIn("s_ui.quick_connecting_toast_source = (uint8_t)source;", body)
        self.assertNotIn("settings_ble_source", body)
        apply = function_body(UI / "esp_bms_lvgl_ui.c", "apply_dashboard_snapshot")
        self.assertLess(apply.index("quick_toast_update_connection("), apply.index("memcpy(&s_ui.last_snapshot"))
        self.assertIn("quick_toast_show_connecting(source);", function_body(UI / "ui_settings_system.c", "settings_bms_bind_confirm_accept_event_cb"))

    def test_chinese_result_glyphs_present(self):
        font = (UI / "settings_zh_16.c").read_text(encoding="utf-8")
        sparse = re.search(r"unicode_list_1\[\]\s*=\s*\{(.*?)\};", font, re.S).group(1)
        offsets = {int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", sparse)}
        cmap = re.search(r"\.range_start = (\d+),[^{}]+\.unicode_list = unicode_list_1", font).group(1)
        codepoints = {int(cmap) + offset for offset in offsets}
        self.assertEqual({character for character in "连接成功失败" if ord(character) not in codepoints}, set())


if __name__ == "__main__":
    unittest.main()
