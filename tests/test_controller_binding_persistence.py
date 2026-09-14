"""真实 C 回归：绑定地址和自动连接开关必须一起保存或回滚。"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG };
enum { HTTP_CONTROLLER_BIND_PENDING };
typedef struct {
    void *http_pending_lock;
    bool flags[1], controller_connection_enabled;
    char controller_bound_mac[18], http_pending_controller_bound_mac[18];
    uint16_t controller_conn_handle;
} esp_bms_idf_runtime_t;
#define RUNTIME_FLAG(r,f) ((r)->flags[f])
#define RUNTIME_SET_FLAG(r,f,v) ((r)->flags[f]=(v))
#define pdMS_TO_TICKS(v) (v)
#define pdTRUE 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static unsigned saves, starts, scans, projections;
static int save_result;
static bool saved_enabled;
static char saved_mac[18];
static int xSemaphoreTake(void *lock, int ticks) {(void)lock;(void)ticks;return pdTRUE;}
static void xSemaphoreGive(void *lock) {(void)lock;}
static void runtime_copy_snapshot_text(char *out, size_t size, const char *text)
{ assert(strlen(text)<size);strcpy(out,text); }
static int esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *r)
{ saves++;saved_enabled=r->controller_connection_enabled;strcpy(saved_mac,r->controller_bound_mac);return save_result; }
static void runtime_project_controller_snapshot(esp_bms_idf_runtime_t *r) {(void)r;projections++;}
static void esp_bms_idf_runtime_project_controller_snapshot(esp_bms_idf_runtime_t *r) {(void)r;}
static int controller_start_scan(esp_bms_idf_runtime_t *r) {(void)r;scans++;return ESP_OK;}
static esp_err_t controller_start_if_enabled(esp_bms_idf_runtime_t *runtime)
__START__
static int esp_bms_idf_runtime_start_controller_ble_if_enabled(esp_bms_idf_runtime_t *r)
{ starts++;return controller_start_if_enabled(r); }
static bool runtime_apply_pending_http_controller_bind(esp_bms_idf_runtime_t *runtime)
__BIND__
int main(void) {
    esp_bms_idf_runtime_t r={0};
    r.http_pending_lock=&r;
    r.controller_conn_handle=0xffff;
    r.controller_connection_enabled=true;
    assert(controller_start_if_enabled(&r)==ESP_OK && scans==0);
    r.controller_connection_enabled=false;
    strcpy(r.http_pending_controller_bound_mac,"12:34:56:78:9A:BC");
    RUNTIME_SET_FLAG(&r,HTTP_CONTROLLER_BIND_PENDING,true);
    assert(runtime_apply_pending_http_controller_bind(&r));
    assert(saves==1 && saved_enabled);
    assert(strcmp(saved_mac,"12:34:56:78:9A:BC")==0);
    assert(r.controller_connection_enabled && starts==1 && scans==1 && projections==1);
    assert(!RUNTIME_FLAG(&r,HTTP_CONTROLLER_BIND_PENDING));
    assert(!runtime_apply_pending_http_controller_bind(&r) && saves==1);
    /* 模拟保存后的重启输入，真实启动门控应重新扫描上次地址。 */
    esp_bms_idf_runtime_t reboot={0};
    reboot.controller_conn_handle=0xffff;
    reboot.controller_connection_enabled=saved_enabled;
    strcpy(reboot.controller_bound_mac,saved_mac);
    assert(controller_start_if_enabled(&reboot)==ESP_OK && scans==2);
    /* 从禁用状态改绑失败，地址与开关必须一起回滚。 */
    r.controller_connection_enabled=false;
    strcpy(r.http_pending_controller_bound_mac,"AB:CD:EF:01:23:45");
    RUNTIME_SET_FLAG(&r,HTTP_CONTROLLER_BIND_PENDING,true);
    save_result=77;
    assert(runtime_apply_pending_http_controller_bind(&r));
    assert(saves==2 && saved_enabled && strcmp(saved_mac,"AB:CD:EF:01:23:45")==0);
    assert(!r.controller_connection_enabled);
    assert(strcmp(r.controller_bound_mac,"12:34:56:78:9A:BC")==0);
    assert(starts==1 && scans==2 && projections==1);
    /* 原本启用的绑定失败时也必须恢复为启用。 */
    r.controller_connection_enabled=true;
    RUNTIME_SET_FLAG(&r,HTTP_CONTROLLER_BIND_PENDING,true);
    assert(runtime_apply_pending_http_controller_bind(&r));
    assert(r.controller_connection_enabled && starts==1);
    assert(strcmp(r.controller_bound_mac,"12:34:56:78:9A:BC")==0);
    return 0;
}
"""


class ControllerBindingPersistenceTest(unittest.TestCase):
    def test_real_c_binding_save_and_rollback(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 C 编译器")
        runtime = ROOT / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c"
        controller = ROOT / "components/esp_bms_controller_ble/esp_bms_controller_ble.c"
        harness = HARNESS.replace(
            "__BIND__", function_body(runtime, "runtime_apply_pending_http_controller_bind")
        ).replace("__START__", function_body(controller, "controller_start_if_enabled"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / "binding.c"
            executable = path / "binding.exe"
            source.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
