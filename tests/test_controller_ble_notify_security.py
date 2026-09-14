"""以真实 NimBLE 收包函数复现全局安全级别静默丢弃通知。"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define MYNEWT_VAL(x) 1
#define BLE_HS_ENOTSUP 1
#define BLE_HS_ENOMEM 2
#define BLE_HS_EBADDATA 3
#define BLE_HS_LOG(...) ((void)0)
#define le16toh(v) (v)
struct os_mbuf { uint8_t *om_data; };
struct __attribute__((packed)) ble_att_notify_req { uint8_t op; uint16_t banq_handle; };
struct ble_gap_sec_state { bool encrypted; };
static struct { int sm_sec_lvl; } ble_hs_cfg;
static bool encrypted;
static unsigned deliveries, frees;
static int ble_att_svr_pullup_req_base(struct os_mbuf **om, size_t size, void *unused)
{ (void)om;(void)size;(void)unused;return 0; }
static void ble_att_svr_get_sec_state(uint16_t conn, struct ble_gap_sec_state *state)
{ (void)conn;state->encrypted=encrypted; }
static void os_mbuf_free_chain(struct os_mbuf *om) { (void)om;frees++; }
static void os_mbuf_adj(struct os_mbuf *om, size_t size) { om->om_data+=size; }
static void ble_gap_notify_rx_event(uint16_t conn, uint16_t handle, struct os_mbuf *om, int indication)
{ assert(conn==1 && handle==3 && indication==0);assert(om->om_data[0]==0xaa);deliveries++; }
int ble_att_svr_rx_notify(uint16_t conn_handle, uint16_t cid, struct os_mbuf **rxom)
__FUNCTION__
int main(void) {
    uint8_t packet[]={0x1b,3,0,0xaa,0x89,0,0,0,0,0,0,0,0,0,0,0,0,0xdf,0xa1};
    struct os_mbuf buffer={packet};
    struct os_mbuf *om=&buffer;
    ble_hs_cfg.sm_sec_lvl=2;
    encrypted=false;
    assert(ble_att_svr_rx_notify(1,4,&om)==0);
    assert(om==NULL && deliveries==0 && frees==1);
    buffer.om_data=packet;om=&buffer;
    ble_hs_cfg.sm_sec_lvl=0;
    assert(ble_att_svr_rx_notify(1,4,&om)==0);
    assert(om==NULL && deliveries==1 && frees==1);
    buffer.om_data=packet;om=&buffer;
    ble_hs_cfg.sm_sec_lvl=2;encrypted=true;
    assert(ble_att_svr_rx_notify(1,4,&om)==0);
    assert(om==NULL && deliveries==2 && frees==1);
    return 0;
}
"""


class ControllerBleNotifySecurityTest(unittest.TestCase):
    def test_local_api_and_hid_keep_explicit_encryption(self):
        root = Path(__file__).resolve().parents[1]
        runtime = (root / "components/esp_bms_idf_runtime/esp_bms_idf_runtime.c").read_text(encoding="utf-8")
        self.assertNotIn("#if CONFIG_BT_NIMBLE_SM_LVL >= 2", runtime)
        for operation, flag in (
            ("READ", "BLE_GATT_CHR_F_READ_ENC"),
            ("WRITE", "BLE_GATT_CHR_F_WRITE_ENC"),
            ("NOTIFY", "BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC"),
        ):
            definitions = re.findall(rf"^#define BLE_MEDIA_HID_{operation}_SECURITY_FLAGS (.+)$", runtime, re.M)
            self.assertEqual(definitions, [flag])
        api = runtime.split("static const struct ble_gatt_svc_def BLE_API_GATT_SERVICES[]", 1)[1].split("static esp_err_t", 1)[0]
        self.assertIn("BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC", api)
        self.assertIn("BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC", api)
        for name in ("sdkconfig.defaults", "sdkconfig.defaults.esp32s3"):
            config = (root / "config/sdkconfig" / name).read_text(encoding="utf-8").splitlines()
            self.assertIn("CONFIG_BT_NIMBLE_SM_LVL=0", config)
            for key in ("SECURITY_ENABLE", "SM_LEGACY", "SM_SC"):
                self.assertIn(f"CONFIG_BT_NIMBLE_{key}=y", config)

    def test_real_nimble_receive_security_gate(self):
        idf = Path(os.environ.get("IDF_PATH", "C:/esp/esp-idf-v6.0.2"))
        source = idf / "components/bt/host/nimble/nimble/nimble/host/src/ble_att_svr.c"
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not source.is_file() or compiler is None:
            self.skipTest("需要本地 ESP-IDF NimBLE 源码及主机 C 编译器")
        harness = HARNESS.replace("__FUNCTION__", function_body(source, "ble_att_svr_rx_notify"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "notify.c"
            executable = root / "notify.exe"
            path.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 str(path), "-o", str(executable)], capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
