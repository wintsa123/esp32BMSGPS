import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "esp_rfc2217_usb_bridge.py"
SPEC = importlib.util.spec_from_file_location("rfc2217_usb_bridge", SCRIPT)
BRIDGE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(BRIDGE)


class SerialStub:
    dtr = True


def manager_stub():
    manager = object.__new__(BRIDGE.UsbSerialJtagPortManager)
    manager.serial = SerialStub()
    manager._download_reset_armed = False
    manager._ignore_download_release = False
    resets = []
    manager._run_reset = lambda _strategy, name: resets.append(name)
    manager._acknowledge_control = lambda _control: None
    return manager, resets


def control(manager, value):
    manager._telnet_process_subnegotiation(
        BRIDGE.COM_PORT_OPTION + BRIDGE.SET_CONTROL + value
    )


class Rfc2217UsbBridgeTest(unittest.TestCase):
    def test_monitor_open_does_not_reset_the_s3(self):
        manager, resets = manager_stub()

        control(manager, BRIDGE.SET_CONTROL_DTR_ON)
        control(manager, BRIDGE.SET_CONTROL_RTS_ON)

        self.assertEqual(resets, [])

    def test_download_reset_does_not_consume_the_following_hard_reset(self):
        manager, resets = manager_stub()

        control(manager, BRIDGE.SET_CONTROL_DTR_OFF)
        control(manager, BRIDGE.SET_CONTROL_DTR_ON)
        control(manager, BRIDGE.SET_CONTROL_RTS_OFF)
        control(manager, BRIDGE.SET_CONTROL_RTS_ON)
        control(manager, BRIDGE.SET_CONTROL_RTS_OFF)

        self.assertEqual(resets, ["usb_download_reset", "usb_hard_reset"])


if __name__ == "__main__":
    unittest.main()
