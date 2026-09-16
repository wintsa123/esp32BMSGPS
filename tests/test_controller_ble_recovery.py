"""编译真实控制器函数，验证首帧重试和扫描取消后的恢复。"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


SOURCE = Path(__file__).resolve().parents[1] / (
    "components/esp_bms_controller_ble/esp_bms_controller_ble.c"
)

STUBS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
enum { CONTROLLER_SUBSCRIBED, CONTROLLER_SCAN_ACTIVE, CONTROLLER_SCAN_REQUESTED,
       BMS_SCAN_REQUESTED, BLE_HOST_SYNCED };
enum { CONTROLLER_BLE_PHASE_SUBSCRIBING, CONTROLLER_BLE_PHASE_ONLINE,
       CONTROLLER_BLE_PHASE_BACKOFF, CONTROLLER_BLE_PHASE_CONNECTING };
enum { ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER=1, ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD };
typedef struct {
    bool flags[5];
    uint16_t controller_conn_handle;
    uint8_t controller_ble_phase, active_data_source;
    uint32_t controller_keepalive_elapsed_ms, controller_scan_revision;
    unsigned controller_scan_candidate_count;
    char controller_scan_candidates[8];
} esp_bms_idf_runtime_t;
struct ble_gatt_error { int status; };
struct ble_gatt_attr { int unused; };
struct ble_gap_disc_desc { struct { unsigned type; } addr; };
struct ble_gap_conn_params { int scan_itvl, scan_window, itvl_min, itvl_max,
    latency, supervision_timeout, min_ce_len, max_ce_len; };
static const struct ble_gap_conn_params CONTROLLER_CONN_PARAMS[2] = { 0 };
static unsigned s_controller_conn_param_index, s_controller_profile;
static unsigned s_controller_profile_start;
static unsigned s_controller_stream_step, s_controller_online_step;
#define CONTROLLER_PROFILE_NUS 1
#define CONTROLLER_PROFILE_FFE0 2
#define CONTROLLER_PROFILE_DISCOVERED 3
struct ble_gap_disc_params { int filter_duplicates, passive, filter_policy, limited; };
static struct { bool read_polling; } profile;
#define controller_profile_config() (&profile)
#define RUNTIME_FLAG(r,f) ((r)->flags[f])
#define RUNTIME_SET_FLAG(r,f,v) ((r)->flags[f]=(v))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr,...) do { int ret=(expr); if(ret) return ret; } while(0)
static unsigned open_count, keepalive_count, read_count, fail_count, scan_count;
static unsigned stream_count, aux_count;
static bool scan_active;
static int cancel_rc, connect_rc, infer_rc;
static uint32_t s_controller_first_frame_elapsed_ms;
static int ble_gap_disc_active(void) { return scan_active; }
static int ble_gap_disc_cancel(void) { if(!cancel_rc) scan_active=false; return cancel_rc; }
static int ble_hs_id_infer_auto(int mode, uint8_t *addr) { (void)mode; *addr=0; return infer_rc; }
static int ble_gap_connect(int addr, const void *peer, int timeout,
    const struct ble_gap_conn_params *params, void *cb, void *arg)
{ (void)addr;(void)peer;(void)timeout;(void)params;(void)cb;(void)arg;return connect_rc; }
static int ble_gap_disc(int addr, int duration, const struct ble_gap_disc_params *params,
    void *cb, void *arg)
{ (void)addr;(void)duration;(void)params;(void)cb;(void)arg;scan_count++;scan_active=true;return 0; }
#define controller_gap_event NULL
#define controller_scan_gap_event NULL
static void esp_bms_idf_runtime_project_controller_snapshot(esp_bms_idf_runtime_t *r) {(void)r;}
static int esp_bms_idf_runtime_ensure_ble_host(esp_bms_idf_runtime_t *r) {(void)r;return 0;}
static int esp_bms_idf_runtime_recover_ble_host(esp_bms_idf_runtime_t *r) {(void)r;return 0;}
static void controller_send_open(esp_bms_idf_runtime_t *r) {(void)r;open_count++;}
static void controller_send_keepalive(esp_bms_idf_runtime_t *r) {(void)r;keepalive_count++;}
static void controller_send_read_request(esp_bms_idf_runtime_t *r) {(void)r;read_count++;}
static void controller_send_stream_command(esp_bms_idf_runtime_t *r) {(void)r;stream_count++;}
static void controller_send_aux_command(esp_bms_idf_runtime_t *r) {(void)r;aux_count++;}
static void controller_fail_connection(esp_bms_idf_runtime_t *r, int conn, const char *stage, int rc)
{ (void)conn;(void)stage;(void)rc;fail_count++;r->controller_ble_phase=CONTROLLER_BLE_PHASE_BACKOFF;
  RUNTIME_SET_FLAG(r,CONTROLLER_SUBSCRIBED,false); }
"""

CASES = r"""
int main(void) {
    esp_bms_idf_runtime_t r = {0};
    struct ble_gatt_error ok = {0};
    r.controller_conn_handle=1;
    r.controller_ble_phase=CONTROLLER_BLE_PHASE_SUBSCRIBING;
    controller_write_cb(1,&ok,NULL,&r);
    assert(RUNTIME_FLAG(&r,CONTROLLER_SUBSCRIBED));
    assert(open_count==0 && keepalive_count==0);
    controller_tick(&r,999);
    assert(open_count==0);
    controller_tick(&r,1);
    assert(open_count==1);
    controller_tick(&r,1000);
    controller_tick(&r,1000);
    /* 4 步轮换：step0 开启、step1 App 变体、step2 辅助指令 */
    assert(open_count==1 && stream_count==1 && aux_count==1 && keepalive_count==0);
    r.controller_ble_phase=CONTROLLER_BLE_PHASE_ONLINE;
    r.active_data_source=ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER;
    const unsigned keepalive_start=keepalive_count, read_start=read_count;
    controller_tick(&r,999);
    assert(keepalive_count==keepalive_start && read_count==read_start);
    controller_tick(&r,1);
    assert(keepalive_count==keepalive_start+1);
    controller_tick(&r,1000);
    assert(read_count==read_start+1);
    r.controller_ble_phase=CONTROLLER_BLE_PHASE_SUBSCRIBING;
    controller_write_cb(1,&ok,NULL,&r);
    for(int i=0;i<9;i++) controller_tick(&r,1000);
    assert(fail_count==0);
    unsigned before_timeout=open_count;
    controller_tick(&r,999);
    assert(fail_count==0);
    controller_tick(&r,1);
    assert(fail_count==1 && open_count==before_timeout);
    assert(!RUNTIME_FLAG(&r,CONTROLLER_SUBSCRIBED));
    /* NUS 保留订阅后立即读及 200ms 轮询。 */
    profile.read_polling=true;
    const unsigned read_nus_start=read_count;
    r.controller_ble_phase=CONTROLLER_BLE_PHASE_SUBSCRIBING;
    controller_write_cb(1,&ok,NULL,&r);
    assert(read_count==read_nus_start+1);
    controller_tick(&r,200);
    assert(read_count==read_nus_start+2);
    /* 首帧前四步轮换：老开启指令、App 开启变体、App 辅助指令、轮询读包各一次。 */
    s_controller_profile = CONTROLLER_PROFILE_DISCOVERED;
    profile.read_polling = false;
    r.controller_ble_phase = CONTROLLER_BLE_PHASE_SUBSCRIBING;
    controller_write_cb(1, &ok, NULL, &r);
    const unsigned open_before = open_count, read_before = read_count;
    const unsigned stream_before = stream_count, aux_before = aux_count;
    for (int i = 0; i < 4; i++) controller_tick(&r, 1000);
    assert(open_count == open_before + 1);
    assert(stream_count == stream_before + 1);
    assert(aux_count == aux_before + 1);
    assert(read_count == read_before + 1);

    /* 首帧超时后轮换起始档位：不允许永远重试同一条 profile 假设。 */
    s_controller_profile = CONTROLLER_PROFILE_NUS;
    s_controller_profile_start = CONTROLLER_PROFILE_NUS;
    r.controller_ble_phase = CONTROLLER_BLE_PHASE_SUBSCRIBING;
    controller_write_cb(1, &ok, NULL, &r);
    for (int i = 0; i < 10; i++) controller_tick(&r, 1000);
    assert(s_controller_profile_start == CONTROLLER_PROFILE_FFE0);

    /* 取消成功但连接启动失败：标记与真实扫描状态同步。 */
    memset(&r,0,sizeof(r));
    r.controller_conn_handle=0xffff;
    RUNTIME_SET_FLAG(&r,BLE_HOST_SYNCED,true);
    RUNTIME_SET_FLAG(&r,CONTROLLER_SCAN_ACTIVE,true);
    scan_active=true; connect_rc=7;
    struct ble_gap_disc_desc disc={0};
    assert(controller_connect(&r,&disc)==ESP_FAIL);
    assert(!scan_active && !RUNTIME_FLAG(&r,CONTROLLER_SCAN_ACTIVE));
    assert(controller_start_scan(&r)==ESP_OK);
    assert(scan_count==1 && scan_active);
    /* 故意模拟历史残留标记，必须仍能启动真实扫描。 */
    scan_active=false;
    assert(controller_start_scan(&r)==ESP_OK);
    assert(scan_count==2 && scan_active);
    assert(controller_start_scan(&r)==ESP_OK);
    assert(scan_count==2);
    /* 取消失败时保留仍活跃的扫描，不误报空闲。 */
    cancel_rc=4;
    assert(controller_connect(&r,&disc)==ESP_FAIL);
    assert(scan_active && RUNTIME_FLAG(&r,CONTROLLER_SCAN_ACTIVE));
    return 0;
}
"""


class ControllerBleRecoveryTest(unittest.TestCase):
    def test_real_c_recovery_transitions(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 gcc 或 clang 执行真实 C 状态机")
        source = SOURCE.read_text(encoding="utf-8")
        constants = "\n".join(re.findall(
            r"^#define CONTROLLER_\w+_MS \d+U$", source, re.M
        ))
        signatures = {
            "controller_write_cb": "static int controller_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)",
            "controller_connect": "static esp_err_t controller_connect(esp_bms_idf_runtime_t *runtime, const struct ble_gap_disc_desc *disc)",
            "controller_start_scan": "static esp_err_t controller_start_scan(esp_bms_idf_runtime_t *runtime)",
            "controller_tick": "static bool controller_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)",
        }
        functions = "\n".join(
            signature + function_body(SOURCE, name)
            for name, signature in signatures.items()
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            harness = path / "controller_recovery.c"
            executable = path / "controller_recovery.exe"
            harness.write_text(constants + STUBS + functions + CASES, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
