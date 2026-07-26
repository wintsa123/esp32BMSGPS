"""RFC2217 bridge adapter for ESP32-S3 USB Serial/JTAG ports on Windows."""

import logging
import threading

import serial.rfc2217
import esp_rfc2217_server as rfc_server
from esptool.reset import HardReset, USBJTAGSerialReset
from esp_rfc2217_server.redirector import Redirector
from serial.rfc2217 import (
    COM_PORT_OPTION,
    SET_CONTROL,
    SET_CONTROL_DTR_OFF,
    SET_CONTROL_DTR_ON,
    SET_CONTROL_RTS_OFF,
    SET_CONTROL_RTS_ON,
)


class UsbSerialJtagPortManager(serial.rfc2217.PortManager):
    """Translate remote default-reset requests to native USB Serial/JTAG reset."""

    def __init__(self, serial_port, connection, logger=None):
        self.is_download_mode = False
        super().__init__(serial_port, connection, logger)

    def _start_reset(self, reset_strategy, name):
        reset_thread = threading.Thread(target=reset_strategy)
        reset_thread.daemon = True
        reset_thread.name = name
        reset_thread.start()

    def _telnet_process_subnegotiation(self, suboption):
        if suboption[0:1] == COM_PORT_OPTION and suboption[1:2] == SET_CONTROL:
            control = suboption[2:3]
            if control == SET_CONTROL_DTR_OFF:
                self.is_download_mode = False
                self.serial.dtr = False
                return
            if control == SET_CONTROL_RTS_OFF and not self.is_download_mode:
                self._start_reset(HardReset(self.serial, uses_usb=True), "usb_hard_reset")
                return
            if control == SET_CONTROL_DTR_ON and not self.is_download_mode:
                self.is_download_mode = True
                self._start_reset(USBJTAGSerialReset(self.serial), "usb_download_reset")
                return
            if control in (SET_CONTROL_DTR_ON, SET_CONTROL_RTS_ON, SET_CONTROL_RTS_OFF):
                return
        super()._telnet_process_subnegotiation(suboption)


class UsbSerialJtagRedirector(Redirector):
    """Keep native USB reset control lines and avoid usbser write completion stalls."""

    def __init__(self, serial_instance, socket_instance, debug=False, esp32r0delay=False):
        # Windows usbser accepts USB Serial/JTAG writes but can wait forever for
        # an overlapped-write completion that the driver never signals.
        serial_instance.write_timeout = 0
        super().__init__(serial_instance, socket_instance, debug, esp32r0delay)
        self.rfc2217 = UsbSerialJtagPortManager(
            self.serial,
            self,
            logger=logging.getLogger("rfc2217.server") if debug else None,
        )


def main():
    rfc_server.Redirector = UsbSerialJtagRedirector
    rfc_server.main()


if __name__ == "__main__":
    main()
