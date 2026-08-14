import asyncio
import unittest
from types import SimpleNamespace
from unittest import mock

from simulator import ble_host_bridge as bridge_module


class RecordingBridge(bridge_module.BleHostBridge):
    def __init__(self, bleak, scan_timeout=0):
        super().__init__(bleak, scan_timeout)
        self.events = []

    async def emit(self, *fields):
        self.events.append(tuple(str(field) for field in fields))


class FakeServices:
    def __init__(self, uuids):
        self.uuids = {uuid.lower() for uuid in uuids}

    def get_service(self, uuid):
        return object() if uuid.lower() in self.uuids else None


class FakeClient:
    service_uuids = []

    def __init__(self, address, disconnected_callback):
        self.address = address
        self.name = "测试设备"
        self.disconnected_callback = disconnected_callback
        self.services = FakeServices(self.service_uuids)
        self.is_connected = False
        self.notifications = []
        self.writes = []

    async def connect(self):
        self.is_connected = True

    async def disconnect(self):
        self.is_connected = False

    async def start_notify(self, uuid, callback):
        self.notifications.append((uuid, callback))

    async def write_gatt_char(self, uuid, data, response):
        self.writes.append((uuid, data, response))


class FakeScanner:
    advertisements = []

    def __init__(self, detection_callback):
        self.callback = detection_callback

    async def start(self):
        for device, advertisement in self.advertisements:
            self.callback(device, advertisement)

    async def stop(self):
        pass


class FakeWriter:
    def __init__(self):
        self.data = bytearray()

    def is_closing(self):
        return False

    def write(self, data):
        self.data.extend(data)

    async def drain(self):
        pass


class BleHostBridgeTest(unittest.IsolatedAsyncioTestCase):
    def make_bleak(self, client_class=FakeClient, scanner_class=FakeScanner):
        return SimpleNamespace(BleakClient=client_class, BleakScanner=scanner_class)

    def test_profiles_expand_16_bit_uuids(self):
        ant = bridge_module.BMS_PROFILES[0]
        jbd = bridge_module.BMS_PROFILES[2]
        daly = bridge_module.BMS_PROFILES[3]
        self.assertEqual(ant.service, "0000ffe0-0000-1000-8000-00805f9b34fb")
        self.assertEqual(jbd.notify, "0000ff01-0000-1000-8000-00805f9b34fb")
        self.assertEqual(daly.write, "0000fff2-0000-1000-8000-00805f9b34fb")
        self.assertEqual(
            bridge_module.BMS_PROFILES[4].notify,
            "8ec90002-f315-4f60-9fb8-838830daea50",
        )
        ffe0 = bridge_module.CONTROLLER_PROFILES[1]
        self.assertEqual(ffe0.notify, "0000ffec-0000-1000-8000-00805f9b34fb")
        self.assertEqual(ffe0.write, ffe0.notify)

    def test_parse_valid_commands(self):
        command = bridge_module.parse_command("CONNECT BMS 4 AA:BB:CC:DD:EE:FF")
        self.assertEqual((command.kind, command.device_type, command.value),
                         ("BMS", 4, "AA:BB:CC:DD:EE:FF"))
        self.assertEqual(bridge_module.parse_command("WRITE CONTROLLER 00aB").value, "00aB")
        self.assertEqual(bridge_module.parse_command("QUIT").action, "QUIT")

    def test_parse_rejects_invalid_lines(self):
        invalid = (
            "CONNECT BMS 5 AA:BB:CC:DD:EE:FF",
            "CONNECT CONTROLLER 1 AA:BB:CC:DD:EE:FF",
            "CONNECT BMS 0 not-a-mac",
            "WRITE BMS abc",
            "SCAN OTHER",
            "SCAN BMS extra",
            "扫描 BMS",
        )
        for line in invalid:
            with self.subTest(line=line), self.assertRaises(bridge_module.ProtocolError):
                bridge_module.parse_command(line)

    async def test_wire_events_use_tab_delimited_ascii(self):
        bridge = bridge_module.BleHostBridge(self.make_bleak())
        writer = FakeWriter()
        bridge.writer = writer
        await bridge.emit("SCAN_RESULT", "BMS", "AA:BB:CC:DD:EE:FF", -42, "e794b5e6b1a0")
        self.assertEqual(
            bytes(writer.data),
            b"SCAN_RESULT\tBMS\tAA:BB:CC:DD:EE:FF\t-42\te794b5e6b1a0\n",
        )

    async def test_scan_encodes_name_and_deduplicates_address(self):
        FakeScanner.advertisements = [
            (SimpleNamespace(address="AA:BB:CC:DD:EE:FF", name="fallback", rssi=-80),
             SimpleNamespace(local_name="电池", rssi=-42)),
            (SimpleNamespace(address="aa:bb:cc:dd:ee:ff", name="duplicate", rssi=-70),
             SimpleNamespace(local_name=None, rssi=-70)),
        ]
        bridge = RecordingBridge(self.make_bleak())
        bridge._loop = asyncio.get_running_loop()
        await bridge.scan("BMS")
        await asyncio.sleep(0)
        self.assertEqual(bridge.events[0], ("SCAN_CLEAR", "BMS"))
        results = [event for event in bridge.events if event[0] == "SCAN_RESULT"]
        self.assertEqual(results, [("SCAN_RESULT", "BMS", "AA:BB:CC:DD:EE:FF", "-42",
                                    "电池".encode().hex())])
        self.assertIn(("SCAN_DONE", "BMS"), bridge.events)
        self.assertEqual(bridge.scanned_devices["BMS"]["aa:bb:cc:dd:ee:ff"].name, "fallback")

    async def test_scan_includes_ascii_and_unnamed_devices(self):
        ascii_device = SimpleNamespace(
            address="AA:BB:CC:DD:EE:01", name="JK-BMS", rssi=-50
        )
        unnamed_device = SimpleNamespace(
            address="AA:BB:CC:DD:EE:02", name=None, rssi=-60
        )
        FakeScanner.advertisements = [
            (ascii_device, SimpleNamespace(local_name="JK-BMS", rssi=-50, service_uuids=[])),
            (unnamed_device, SimpleNamespace(local_name=None, rssi=-60, service_uuids=[])),
        ]
        bridge = RecordingBridge(self.make_bleak())
        bridge._loop = asyncio.get_running_loop()

        await bridge.scan("BMS")

        results = [event for event in bridge.events if event[0] == "SCAN_RESULT"]
        self.assertEqual([event[2] for event in results], [ascii_device.address,
                                                          unnamed_device.address])
        self.assertEqual(results[0][4], "JK-BMS".encode().hex())
        self.assertEqual(results[1][4], unnamed_device.address.encode().hex())

    async def test_scan_prioritizes_supported_service_before_twelve_name_matches(self):
        advertisements = []
        for index in range(12):
            advertisements.append((
                SimpleNamespace(address=f"AA:BB:CC:DD:00:{index:02X}", name=f"设备{index}", rssi=-20),
                SimpleNamespace(local_name=f"设备{index}", rssi=-20, service_uuids=[]),
            ))
        target = SimpleNamespace(address="AA:BB:CC:DD:EE:FF", name="Target", rssi=-90)
        advertisements.append((
            target,
            SimpleNamespace(
                local_name="Target",
                rssi=-90,
                service_uuids=[bridge_module.BMS_PROFILES[2].service],
            ),
        ))
        FakeScanner.advertisements = advertisements
        bridge = RecordingBridge(self.make_bleak())
        bridge._loop = asyncio.get_running_loop()
        await bridge.scan("BMS")
        results = [event for event in bridge.events if event[0] == "SCAN_RESULT"]
        self.assertEqual(len(results), bridge_module.SCAN_RESULT_LIMIT)
        self.assertEqual(results[0][2], target.address)
        self.assertIn(target.address.casefold(), bridge.scanned_devices["BMS"])

    async def test_new_scan_cancels_previous_scan_for_same_kind(self):
        bridge = RecordingBridge(self.make_bleak())
        previous = asyncio.create_task(asyncio.sleep(10))
        bridge.scan_tasks["BMS"] = previous
        with mock.patch.object(bridge, "scan", mock.AsyncMock()) as scan:
            await bridge.dispatch(bridge_module.parse_command("SCAN BMS"))
            await asyncio.sleep(0)
        self.assertTrue(previous.cancelled())
        scan.assert_awaited_once_with("BMS")

    async def test_controller_prefers_nus(self):
        class NusClient(FakeClient):
            service_uuids = [bridge_module.CONTROLLER_PROFILES[0].service,
                             bridge_module.CONTROLLER_PROFILES[1].service]

        bridge = RecordingBridge(self.make_bleak(NusClient))
        bridge._loop = asyncio.get_running_loop()
        await bridge.connect("CONTROLLER", 0, "AA:BB:CC:DD:EE:FF")
        self.assertEqual(bridge.profiles["CONTROLLER"].name, "NUS")
        self.assertEqual(bridge.events[-1][-1], "NUS")

    async def test_controller_falls_back_to_ffe0(self):
        class FfeClient(FakeClient):
            service_uuids = [bridge_module.CONTROLLER_PROFILES[1].service]

        bridge = RecordingBridge(self.make_bleak(FfeClient))
        bridge._loop = asyncio.get_running_loop()
        await bridge.connect("CONTROLLER", 0, "AA:BB:CC:DD:EE:FF")
        self.assertEqual(bridge.profiles["CONTROLLER"].name, "FFE0")
        self.assertEqual(bridge.clients["CONTROLLER"].notifications[0][0],
                         bridge_module.CONTROLLER_PROFILES[1].notify)
        await bridge.write("CONTROLLER", "AA13EC0701F1A25D")
        self.assertEqual(bridge.clients["CONTROLLER"].writes, [(
            bridge_module.CONTROLLER_PROFILES[1].write,
            bytes.fromhex("AA13EC0701F1A25D"),
            False,
        )])

    async def test_controller_without_supported_service_reports_error(self):
        bridge = RecordingBridge(self.make_bleak())
        bridge._loop = asyncio.get_running_loop()
        await bridge.connect("CONTROLLER", 0, "AA:BB:CC:DD:EE:FF")
        self.assertEqual(bridge.events[-1][0:2], ("ERROR", "CONTROLLER"))
        self.assertNotIn("CONTROLLER", bridge.clients)

    async def test_write_uses_selected_characteristic_with_bms_response(self):
        bridge = RecordingBridge(self.make_bleak())
        bridge._loop = asyncio.get_running_loop()
        await bridge.connect("BMS", 2, "AA:BB:CC:DD:EE:FF")
        client = bridge.clients["BMS"]
        await bridge.write("BMS", "A501ff")
        self.assertEqual(client.writes, [(
            bridge_module.BMS_PROFILES[2].write, b"\xa5\x01\xff", True
        )])

    def test_missing_bleak_has_clear_error(self):
        with mock.patch.dict("sys.modules", {"bleak": None}):
            with self.assertRaisesRegex(RuntimeError, "未安装 bleak"):
                bridge_module.load_bleak()


if __name__ == "__main__":
    unittest.main()
