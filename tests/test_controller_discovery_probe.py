"""编译真实探测选择函数，验证未知型号的 notify/write 选择与 UUID 等价判断。

被测代码全部来自 components/esp_bms_controller_ble/esp_bms_controller_ble.c，
本文件只提供最小 NimBLE/GATT 类型桩，不复制业务逻辑。
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/esp_bms_controller_ble/esp_bms_controller_ble.c"

STUBS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
typedef struct { ble_uuid_t u; uint32_t value; } ble_uuid32_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
typedef union {
    ble_uuid_t u;
    ble_uuid16_t u16;
    ble_uuid32_t u32;
    ble_uuid128_t u128;
} ble_uuid_any_t;

enum { BLE_UUID_TYPE_16 = 16, BLE_UUID_TYPE_32 = 32, BLE_UUID_TYPE_128 = 128 };
#define BLE_UUID16(u) ((ble_uuid16_t *)(uintptr_t)(const void *)(u))
#define BLE_UUID32(u) ((ble_uuid32_t *)(uintptr_t)(const void *)(u))
#define BLE_UUID128(u) ((ble_uuid128_t *)(uintptr_t)(const void *)(u))

#define BLE_GATT_CHR_F_WRITE_NO_RSP 0x04
#define BLE_GATT_CHR_F_WRITE 0x08
#define BLE_GATT_CHR_F_NOTIFY 0x10
#define BLE_GATT_CHR_F_INDICATE 0x20

#define CONTROLLER_DISCOVERY_SERVICE_MAX 8U
#define CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX 24U

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t start_handle;
    uint16_t end_handle;
} controller_discovery_service_t;

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t val_handle;
    uint32_t properties;
    uint8_t service_index;
} controller_discovery_characteristic_t;

static controller_discovery_service_t
    s_discovery_services[CONTROLLER_DISCOVERY_SERVICE_MAX];
static uint8_t s_discovery_service_count;
static controller_discovery_characteristic_t
    s_discovery_characteristics[CONTROLLER_DISCOVERY_CHARACTERISTIC_MAX];
static uint8_t s_discovery_characteristic_count;
static uint8_t s_discovery_service_cursor;

static int ble_uuid_cmp(const ble_uuid_t *uuid1, const ble_uuid_t *uuid2)
{
    if (uuid1->type != uuid2->type) {
        return uuid1->type - uuid2->type;
    }
    switch (uuid1->type) {
    case BLE_UUID_TYPE_16:
        return (int)BLE_UUID16(uuid1)->value - (int)BLE_UUID16(uuid2)->value;
    case BLE_UUID_TYPE_32:
        return BLE_UUID32(uuid1)->value < BLE_UUID32(uuid2)->value
                   ? -1
                   : (BLE_UUID32(uuid1)->value > BLE_UUID32(uuid2)->value ? 1 : 0);
    default:
        return memcmp(BLE_UUID128(uuid1)->value, BLE_UUID128(uuid2)->value, 16);
    }
}

static void set_uuid16(ble_uuid_any_t *uuid, uint16_t value)
{
    memset(uuid, 0, sizeof(*uuid));
    uuid->u.type = BLE_UUID_TYPE_16;
    uuid->u16.value = value;
}

/* 蓝牙 base 展开形式：0000xxxx-0000-1000-8000-00805f9b34fb（小端存放） */
static void set_uuid128_base(ble_uuid_any_t *uuid, uint16_t value)
{
    static const uint8_t base[16] = { 0xFBU, 0x34U, 0x9BU, 0x5FU, 0x80U, 0x00U,
                                      0x00U, 0x80U, 0x00U, 0x10U, 0x00U, 0x00U,
                                      0x00U, 0x00U, 0x00U, 0x00U };
    memset(uuid, 0, sizeof(*uuid));
    uuid->u.type = BLE_UUID_TYPE_128;
    memcpy(uuid->u128.value, base, sizeof(base));
    uuid->u128.value[12] = (uint8_t)(value & 0xFFU);
    uuid->u128.value[13] = (uint8_t)(value >> 8U);
}

static void add_service(uint16_t start_handle, uint16_t end_handle)
{
    controller_discovery_service_t *service =
        &s_discovery_services[s_discovery_service_count];
    set_uuid16(&service->uuid, 0xFFE0U);
    service->start_handle = start_handle;
    service->end_handle = end_handle;
    s_discovery_service_count++;
}

static void add_characteristic(uint8_t service_index, uint16_t uuid16,
                               uint16_t val_handle, uint32_t properties)
{
    controller_discovery_characteristic_t *characteristic =
        &s_discovery_characteristics[s_discovery_characteristic_count];
    set_uuid16(&characteristic->uuid, uuid16);
    characteristic->val_handle = val_handle;
    characteristic->properties = properties;
    characteristic->service_index = service_index;
    s_discovery_characteristic_count++;
}

static void reset_discovery(void)
{
    memset(s_discovery_services, 0, sizeof(s_discovery_services));
    memset(s_discovery_characteristics, 0, sizeof(s_discovery_characteristics));
    s_discovery_service_count = 0U;
    s_discovery_characteristic_count = 0U;
    s_discovery_service_cursor = 0U;
}
"""

CASES = r"""
int main(void) {
    ble_uuid_any_t uuid16 = { 0 };
    ble_uuid_any_t uuid128 = { 0 };
    controller_discovery_characteristic_t notify = { 0 };
    controller_discovery_characteristic_t write = { 0 };

    /* 128 位 base 展开形式必须与 16 位 UUID 等价：否则对端换一种声明宽度就
     * 会被判成"特征缺失"，型号直接连不上。 */
    set_uuid16(&uuid16, 0xFFECU);
    set_uuid128_base(&uuid128, 0xFFECU);
    assert(controller_uuid16_value(&uuid128.u) == 0xFFECU);
    assert(controller_uuid_matches(&uuid128.u, &uuid16.u));
    assert(controller_uuid_matches(&uuid16.u, &uuid128.u));
    set_uuid128_base(&uuid128, 0xFFF2U);
    assert(!controller_uuid_matches(&uuid128.u, &uuid16.u));
    set_uuid128_base(&uuid128, 0x1801U); /* 短 UUID 也要能还原 */
    assert(controller_uuid16_value(&uuid128.u) == 0x1801U);

    /* 同一服务内的 notify + write 成对优先：即使别的服务里有更高分的 notify */
    reset_discovery();
    add_service(0x0010U, 0x0020U);
    add_characteristic(0U, 0xFFE1U, 0x0011U, BLE_GATT_CHR_F_NOTIFY);
    add_characteristic(0U, 0xFFE2U, 0x0012U, BLE_GATT_CHR_F_WRITE);
    add_service(0x0030U, 0x0040U);
    add_characteristic(1U, 0xFFECU, 0x0031U, BLE_GATT_CHR_F_NOTIFY);
    assert(controller_discovery_select(&notify, &write));
    assert(notify.val_handle == 0x0011U && write.val_handle == 0x0012U);
    assert(notify.service_index == 0U && write.service_index == 0U);

    /* 只声明 Indicate 的模块也必须被选中，并保留其特征属性供 CCCD 取值 */
    reset_discovery();
    add_service(0x0010U, 0x0020U);
    add_characteristic(0U, 0xFFECU, 0x0011U, BLE_GATT_CHR_F_INDICATE);
    add_service(0x0030U, 0x0040U);
    add_characteristic(1U, 0xFFE2U, 0x0031U, BLE_GATT_CHR_F_WRITE_NO_RSP);
    assert(controller_discovery_select(&notify, &write));
    assert(notify.val_handle == 0x0011U);
    assert((notify.properties & BLE_GATT_CHR_F_INDICATE) != 0U);
    assert((notify.properties & BLE_GATT_CHR_F_NOTIFY) == 0U);
    assert(write.val_handle == 0x0031U);

    /* 同一服务内 UUID 分数更高者胜出（0xFFEC > 0xFFE1） */
    reset_discovery();
    add_service(0x0010U, 0x0020U);
    add_characteristic(0U, 0xFFE1U, 0x0011U, BLE_GATT_CHR_F_NOTIFY);
    add_characteristic(0U, 0xFFECU, 0x0013U, BLE_GATT_CHR_F_NOTIFY);
    add_characteristic(0U, 0xFFE2U, 0x0012U, BLE_GATT_CHR_F_WRITE);
    assert(controller_discovery_select(&notify, &write));
    assert(notify.val_handle == 0x0013U);

    /* 只有 notify 没有 write：不能假装可用，必须失败交由上层报诊断 */
    reset_discovery();
    add_service(0x0010U, 0x0020U);
    add_characteristic(0U, 0xFFECU, 0x0011U, BLE_GATT_CHR_F_NOTIFY);
    assert(!controller_discovery_select(&notify, &write));

    /* 空清单也要安全失败 */
    reset_discovery();
    assert(!controller_discovery_select(&notify, &write));
    return 0;
}
"""


class ControllerDiscoveryProbeTest(unittest.TestCase):
    def test_real_c_discovery_selection(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 gcc 或 clang 执行真实 C 选择逻辑")
        signatures = {
            "controller_uuid16_value": (
                "static uint16_t controller_uuid16_value(const ble_uuid_t *uuid)"
            ),
            "controller_uuid_matches": (
                "static bool controller_uuid_matches(const ble_uuid_t *candidate, "
                "const ble_uuid_t *expected)"
            ),
            "controller_discovery_uuid_rank": (
                "static uint8_t controller_discovery_uuid_rank(uint16_t value, bool notify_role)"
            ),
            "controller_discovery_is_notify": (
                "static bool controller_discovery_is_notify("
                "const controller_discovery_characteristic_t *characteristic)"
            ),
            "controller_discovery_is_write": (
                "static bool controller_discovery_is_write("
                "const controller_discovery_characteristic_t *characteristic)"
            ),
            "controller_discovery_score": (
                "static uint16_t controller_discovery_score("
                "const controller_discovery_characteristic_t *characteristic, "
                "bool notify_role)"
            ),
            "controller_discovery_select": (
                "static bool controller_discovery_select("
                "controller_discovery_characteristic_t *notify_out, "
                "controller_discovery_characteristic_t *write_out)"
            ),
        }
        functions = "\n".join(
            signature + function_body(SOURCE, name)
            for name, signature in signatures.items()
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            harness = path / "controller_discovery.c"
            executable = path / "controller_discovery.exe"
            harness.write_text(STUBS + functions + CASES, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(harness), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
