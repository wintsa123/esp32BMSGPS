"""Compile the actual BMS transport functions and exercise asynchronous failures."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body

SOURCE = Path(__file__).resolve().parents[1] / "components/esp_bms_bms_ble/esp_bms_bms_ble.c"
STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
typedef int bms_ble_phase_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE };
enum { BMS_WRITE_IN_FLIGHT, BMS_DEVICE_INFO_REQUESTED, BMS_DEVICE_INFO_KNOWN,
       BMS_SCAN_ACTIVE, BMS_SCAN_REQUESTED, BMS_SCAN_SNAPSHOT_DIRTY,
       CONTROLLER_SCAN_REQUESTED, BMS_BIND_ACTIVE, BMS_BLE_READY, BMS_BLE_SYNCED };
enum { BMS_BLE_PHASE_IDLE, BMS_BLE_PHASE_SCANNING, BMS_BLE_PHASE_CONNECTING,
       BMS_BLE_PHASE_DISCOVERING_SERVICE, BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC,
       BMS_BLE_PHASE_DISCOVERING_CCCD, BMS_BLE_PHASE_SUBSCRIBING,
       BMS_BLE_PHASE_ONLINE, BMS_BLE_PHASE_BACKOFF };
enum { ESP_BMS_IDF_BMS_TYPE_ANT, ESP_BMS_IDF_BMS_TYPE_JK, ESP_BMS_IDF_BMS_TYPE_JBD,
       ESP_BMS_IDF_BMS_TYPE_DALY, ESP_BMS_IDF_BMS_TYPE_YANYANG,
       ESP_BMS_IDF_BMS_TYPE_BLUEBABE, ESP_BMS_IDF_BMS_TYPE_MINGTANG, ESP_BMS_IDF_BMS_TYPE_EM02 };
enum { BLE_GATT_CHR_F_WRITE=8, BLE_GATT_CHR_F_WRITE_NO_RSP=4,
       BLE_GATT_CHR_F_NOTIFY=16, BLE_GATT_CHR_F_INDICATE=32 };
enum { BLE_HS_EDONE=14, BLE_HS_ENOTCONN=7, BLE_ERR_REM_USER_CONN_TERM=19,
       BLE_GATT_DSC_CLT_CFG_UUID16=0x2902 };
enum { BLE_GAP_EVENT_CONNECT, BLE_GAP_EVENT_DISCONNECT };
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define RUNTIME_FLAG(r,f) ((r)->flags[f])
#define RUNTIME_SET_FLAG(r,f,v) ((r)->flags[f]=(v))
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xffe1
#define BMS_STATUS_POLL_PERIOD_MS 500U
#define BMS_HEARTBEAT_TIMEOUT_MS 30000U
#define BMS_RECONNECT_BACKOFF_MS 3000U
#define ESP_BMS_YANYANG_UUID_LEN 16
struct ble_uuid { uint16_t value; };
typedef struct { struct ble_uuid u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID16_DECLARE(v) (&(struct ble_uuid){v})
static int ble_uuid_cmp(const struct ble_uuid *a,const struct ble_uuid *b) {return a->value != b->value;}
struct ble_gatt_error { int status; };
struct ble_gatt_attr { uint16_t handle; };
struct ble_gatt_chr { uint16_t def_handle,val_handle;uint8_t properties;union {struct ble_uuid u;} uuid; };
struct ble_gatt_dsc {uint16_t handle;union {struct ble_uuid u;} uuid;};
struct ble_gap_event {int type;struct {int status;uint16_t conn_handle;} connect;};
typedef struct {
    bool flags[10]; uint8_t bms_ble_phase,bms_poll_index,bms_type,bms_write_properties,bms_ant_channel;
    uint16_t bms_conn_handle,bms_char_val_handle,bms_write_char_val_handle,bms_cccd_handle,
             bms_cccd_value,bms_service_start_handle,bms_service_end_handle,bms_notify_end_handle,bms_frame_len;
    bool bms_ant_alt_available;
    uint32_t bms_ble_session,bms_status_poll_elapsed_ms;
    int64_t bms_establish_deadline_us,bms_telemetry_last_us;
} esp_bms_idf_runtime_t;
static esp_bms_idf_runtime_t *s_bms_runtime;
static int64_t now_us=1000000;
static int64_t esp_timer_get_time(void) {return now_us;}
static unsigned clear_count,terminate_count,cancel_count,ack_count,no_rsp_count,poll_count,scan_count,gap_count;
static uint16_t terminated_handle,descriptor_end;
static int terminate_rc,write_rc;
static void *pending_arg;
static int (*pending_cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*);
static void bms_clear_telemetry(esp_bms_idf_runtime_t *r) {(void)r;clear_count++;}
static void bms_set_info(esp_bms_idf_runtime_t *r,const char *s) {(void)r;(void)s;}
static bool bms_is_em02(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_EM02;}
static bool bms_is_jk(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_JK;}
static bool bms_is_jbd(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_JBD;}
static bool bms_is_daly(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_DALY;}
static bool bms_is_yanyang(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_YANYANG;}
static bool bms_is_bluebabe(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_BLUEBABE;}
static bool bms_is_mingtang(const esp_bms_idf_runtime_t *r) {return r->bms_type==ESP_BMS_IDF_BMS_TYPE_MINGTANG;}
static bool bms_is_jk_characteristic(const struct ble_gatt_chr *c) {return c->uuid.u.value==0xffe1;}
static uint8_t esp_bms_yanyang_write_uuid[16],esp_bms_yanyang_notify_uuid[16];
static ble_uuid128_t bms_uuid128(const uint8_t *u) {(void)u;return (ble_uuid128_t){0};}
static void esp_bms_jk_reset(void) {}
static int ble_gap_terminate(uint16_t h,int why) {(void)why;terminate_count++;terminated_handle=h;return terminate_rc;}
static int ble_gap_conn_cancel(void) {cancel_count++;return 0;}
static int ble_gap_disc_cancel(void) {return 0;}
static bool ble_gap_disc_active(void) {return false;}
static bool esp_bms_idf_runtime_bms_scan_project_snapshot(esp_bms_idf_runtime_t *r) {(void)r;return true;}
static esp_err_t bms_send_poll_request(esp_bms_idf_runtime_t *r,bool info) {(void)r;(void)info;poll_count++;return ESP_OK;}
static esp_err_t bms_start_scan(esp_bms_idf_runtime_t *r) {(void)r;scan_count++;return ESP_OK;}
static int ble_gattc_write_flat(uint16_t conn,uint16_t attr,const void *data,uint16_t len,
    int (*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg)
{(void)conn;(void)attr;(void)data;(void)len;ack_count++;pending_cb=cb;pending_arg=arg;return write_rc;}
static int ble_gattc_write_no_rsp_flat(uint16_t conn,uint16_t attr,const void *data,uint16_t len)
{(void)conn;(void)attr;(void)data;(void)len;no_rsp_count++;return write_rc;}
static int ble_gattc_disc_all_dscs(uint16_t conn,uint16_t start,uint16_t end,
    int (*cb)(uint16_t,const struct ble_gatt_error*,uint16_t,const struct ble_gatt_dsc*,void*),void *arg)
{(void)conn;(void)start;(void)cb;(void)arg;descriptor_end=end;return 0;}
static int bms_gap_event(struct ble_gap_event *e,void *arg) {(void)e;(void)arg;gap_count++;return 0;}
'''
CASES = r'''
static void setup(esp_bms_idf_runtime_t *r) {
    memset(r,0,sizeof(*r));r->bms_conn_handle=4;r->bms_char_val_handle=8;
    r->bms_write_char_val_handle=12;r->bms_cccd_handle=9;r->bms_service_end_handle=20;
    r->bms_write_properties=BLE_GATT_CHR_F_WRITE;r->bms_ble_session=1;
    r->flags[BMS_BIND_ACTIVE]=r->flags[BMS_BLE_READY]=r->flags[BMS_BLE_SYNCED]=true;
    r->bms_ble_phase=BMS_BLE_PHASE_ONLINE;s_bms_runtime=r;
    terminate_rc=write_rc=0;
}
int main(void) {
    esp_bms_idf_runtime_t r;
    struct ble_gatt_error ok={0},error={5},done={BLE_HS_EDONE};
    struct ble_gatt_attr attr={9};uint8_t frame[]={1,2};
    setup(&r);
    assert(bms_write_frame(&r,frame,sizeof(frame))==ESP_OK);
    assert(ack_count==1 && r.flags[BMS_WRITE_IN_FLIGHT]);
    void *old_arg=pending_arg;
    bms_reset_connection_state(&r,BMS_BLE_PHASE_BACKOFF);
    r.bms_conn_handle=4;r.bms_ble_phase=BMS_BLE_PHASE_SUBSCRIBING;r.bms_cccd_handle=9;
    r.flags[BMS_WRITE_IN_FLIGHT]=true;
    pending_cb(4,&ok,&attr,old_arg);
    assert(r.bms_ble_phase==BMS_BLE_PHASE_SUBSCRIBING && r.flags[BMS_WRITE_IN_FLIGHT]);
    /* Current subscription failure disconnects and cannot leave SUBSCRIBING stuck. */
    bms_write_cb(4,&error,NULL,bms_callback_arg(&r));
    assert(r.bms_ble_phase==BMS_BLE_PHASE_BACKOFF && !r.flags[BMS_WRITE_IN_FLIGHT]);
    assert(terminate_count==1 && terminated_handle==4);
    /* A failed terminate is retried; ENOTCONN releases the handle for reconnection. */
    terminate_rc=BLE_HS_ENOTCONN;bms_tick(&r,3000);
    assert(r.bms_conn_handle==0xffff);
    bms_tick(&r,3000);assert(scan_count==1);
    setup(&r);r.bms_write_properties=BLE_GATT_CHR_F_WRITE_NO_RSP;
    assert(bms_write_frame(&r,frame,2)==ESP_OK && no_rsp_count==1);
    assert(!r.flags[BMS_WRITE_IN_FLIGHT]);
    r.bms_type=ESP_BMS_IDF_BMS_TYPE_EM02;
    assert(bms_write_frame(&r,frame,2)==ESP_ERR_INVALID_STATE && no_rsp_count==1);
    r.bms_write_properties=BLE_GATT_CHR_F_WRITE;write_rc=3;
    assert(bms_write_frame(&r,frame,2)==ESP_FAIL && !r.flags[BMS_WRITE_IN_FLIGHT]);
    setup(&r);r.bms_ble_phase=BMS_BLE_PHASE_SUBSCRIBING;
    assert(bms_subscribe(&r)==ESP_OK);
    bms_write_cb(4,&ok,&attr,pending_arg);
    assert(r.bms_ble_phase==BMS_BLE_PHASE_ONLINE && poll_count==1);
    for (unsigned phase=BMS_BLE_PHASE_CONNECTING;phase<=BMS_BLE_PHASE_SUBSCRIBING;phase++) {
        setup(&r);r.bms_ble_phase=phase;r.bms_establish_deadline_us=now_us+100;
        unsigned before=terminate_count;
        bms_tick(&r,50);assert(r.bms_ble_phase==phase);
        now_us+=100;bms_tick(&r,50);
        assert(r.bms_ble_phase==BMS_BLE_PHASE_BACKOFF && terminate_count==before+1);
    }
    assert(cancel_count==1);
    /* Token protects a reused numeric handle from callbacks of the previous attempt. */
    setup(&r);old_arg=bms_callback_arg(&r);bms_advance_session(&r);
    struct ble_gap_event connected={BLE_GAP_EVENT_CONNECT,{0,4}};
    unsigned before=terminate_count;
    bms_connection_gap_event(&connected,old_arg);
    assert(gap_count==0 && terminate_count==before);
    connected.connect.conn_handle=10;bms_connection_gap_event(&connected,old_arg);
    assert(terminate_count==before+1 && terminated_handle==10);
    bms_connection_gap_event(&connected,bms_callback_arg(&r));assert(gap_count==1);
    setup(&r);old_arg=bms_callback_arg(&r);
    assert(bms_stop(&r));assert(!r.flags[BMS_BIND_ACTIVE] && r.bms_conn_handle==0xffff);
    connected.connect.conn_handle=4;
    before=terminate_count;bms_connection_gap_event(&connected,old_arg);
    assert(terminate_count==before+1);
    /* Failed cancellation keeps the handle until periodic terminate retry can reclaim it. */
    setup(&r);terminate_rc=5;
    assert(bms_stop(&r));assert(r.bms_conn_handle==4 && r.bms_ble_phase==BMS_BLE_PHASE_BACKOFF);
    terminate_rc=BLE_HS_ENOTCONN;bms_tick(&r,3000);assert(r.bms_conn_handle==0xffff);
    setup(&r);old_arg=bms_callback_arg(&r);r.flags[BMS_BIND_ACTIVE]=false;
    r.flags[BMS_SCAN_ACTIVE]=true;bms_on_ble_reset(&r);
    assert(!r.flags[BMS_SCAN_ACTIVE] && !r.flags[BMS_SCAN_REQUESTED]);
    assert(bms_callback_runtime(old_arg)==NULL);
    /* Discover only matching, usable characteristics, and stop CCCD range at next declaration. */
    setup(&r);r.bms_ble_phase=BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
    r.bms_char_val_handle=r.bms_write_char_val_handle=0;
    struct ble_gatt_chr notify={7,8,BLE_GATT_CHR_F_NOTIFY,{{0xffe1}}};
    bms_chr_cb(4,&ok,&notify,bms_callback_arg(&r));
    struct ble_gatt_chr unrelated={10,11,BLE_GATT_CHR_F_WRITE,{{0xffe2}}};
    bms_chr_cb(4,&ok,&unrelated,bms_callback_arg(&r));
    assert(r.bms_char_val_handle==8 && r.bms_notify_end_handle==9 && r.bms_write_char_val_handle==0);
    notify.properties |= BLE_GATT_CHR_F_WRITE_NO_RSP;
    bms_chr_cb(4,&ok,&notify,bms_callback_arg(&r));
    bms_chr_cb(4,&done,NULL,bms_callback_arg(&r));assert(descriptor_end==9);
    struct ble_gatt_dsc wrong={12,{{0x2902}}},correct={9,{{0x2902}}};
    r.bms_cccd_handle=0;
    bms_dsc_cb(4,&ok,8,&wrong,bms_callback_arg(&r));assert(r.bms_cccd_handle==0);
    bms_dsc_cb(4,&ok,8,&correct,bms_callback_arg(&r));assert(r.bms_cccd_handle==9);
    /* Alternate channels require the FFF5 capability marker and never fall back silently. */
    setup(&r);r.bms_ble_phase=BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;r.bms_ant_channel=1;
    r.bms_char_val_handle=r.bms_write_char_val_handle=0;
    struct ble_gatt_chr a_write={2,3,BLE_GATT_CHR_F_WRITE,{{0xfff3}}};
    struct ble_gatt_chr a_notify={5,6,BLE_GATT_CHR_F_NOTIFY,{{0xfff4}}};
    bms_chr_cb(4,&ok,&a_write,bms_callback_arg(&r));bms_chr_cb(4,&ok,&a_notify,bms_callback_arg(&r));
    bms_chr_cb(4,&done,NULL,bms_callback_arg(&r));assert(r.bms_ble_phase==BMS_BLE_PHASE_BACKOFF);
    setup(&r);r.bms_ble_phase=BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;r.bms_ant_channel=1;
    r.bms_char_val_handle=r.bms_write_char_val_handle=0;
    struct ble_gatt_chr b_write={10,11,BLE_GATT_CHR_F_WRITE_NO_RSP,{{0xfff5}}};
    bms_chr_cb(4,&ok,&a_write,bms_callback_arg(&r));bms_chr_cb(4,&ok,&a_notify,bms_callback_arg(&r));
    bms_chr_cb(4,&ok,&b_write,bms_callback_arg(&r));bms_chr_cb(4,&done,NULL,bms_callback_arg(&r));
    assert(r.bms_ble_phase==BMS_BLE_PHASE_DISCOVERING_CCCD && descriptor_end==9);
    return 0;
}
'''

class BmsBleRecoveryTest(unittest.TestCase):
    def test_real_transport_c(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("gcc or clang is required")
        source = SOURCE.read_text()
        names = ["bms_advance_session", "bms_callback_arg", "bms_callback_runtime",
                 "bms_reset_connection_state", "bms_fail_connection", "bms_write_cb",
                 "bms_write_frame", "bms_subscribe", "bms_dsc_cb",
                 "bms_start_descriptor_discovery", "bms_select_characteristic",
                 "bms_chr_cb", "bms_connection_gap_event", "bms_stop", "bms_on_ble_reset", "bms_tick"]
        declarations = []
        definitions = []
        for name in names:
            signature = re.search(r"^static [^;{}]+\b" + name + r"\([^;{}]*\)\s*(?=\{)", source, re.M).group(0)
            declarations.append(signature + ";")
            definitions.append(signature + function_body(SOURCE, name))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            harness, executable = path / "bms_recovery.c", path / "bms_recovery"
            harness.write_text(STUBS + "\n".join(declarations + definitions) + CASES)
            result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     str(harness), "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == "__main__":
    unittest.main()
