"""RFC2217 bridge adapter for ESP32-S3 USB Serial/JTAG ports on Windows."""

import argparse
import logging
import socket
import time

import serial.rfc2217
import esp_rfc2217_server as rfc_server
from esptool.reset import HardReset, USBJTAGSerialReset
from esp_rfc2217_server.redirector import Redirector
from serial.rfc2217 import (
    COM_PORT_OPTION,
    SERVER_SET_BAUDRATE,
    SET_CONTROL,
    SET_CONTROL_DTR_OFF,
    SET_CONTROL_DTR_ON,
    SET_CONTROL_RTS_OFF,
    SET_CONTROL_RTS_ON,
    SET_CONTROL_REQ_FLOW_SETTING,
    SET_CONTROL_USE_HW_FLOW_CONTROL,
    SET_CONTROL_USE_NO_FLOW_CONTROL,
    SET_CONTROL_USE_SW_FLOW_CONTROL,
    SERVER_SET_CONTROL,
    SERVER_SET_DATASIZE,
    SERVER_SET_PARITY,
    SERVER_SET_STOPSIZE,
    SET_BAUDRATE,
    SET_DATASIZE,
    SET_PARITY,
    SET_STOPSIZE,
)


class UsbSerialJtagPortManager(serial.rfc2217.PortManager):
    """Translate remote default-reset requests to native USB Serial/JTAG reset."""

    def __init__(self, serial_port, connection, logger=None):
        self.is_download_mode = False
        self._logical_flow_control = SET_CONTROL_USE_NO_FLOW_CONTROL
        # USB Serial/JTAG transports bytes over USB; its RFC2217 client still
        # expects a UART-setting acknowledgement, but changing the Windows
        # usbser settings is both meaningless and unreliable.
        self._logical_settings = {
            SET_BAUDRATE: serial_port.baudrate.to_bytes(4, "big"),
            SET_DATASIZE: bytes((serial_port.bytesize,)),
            SET_PARITY: bytes((serial.rfc2217.RFC2217_PARITY_MAP[serial_port.parity],)),
            SET_STOPSIZE: bytes((serial.rfc2217.RFC2217_STOPBIT_MAP[serial_port.stopbits],)),
        }
        super().__init__(serial_port, connection, logger)

    def _run_reset(self, reset_strategy, name):
        # USB Serial/JTAG rejects serial reconfiguration while DTR/RTS reset
        # requests are in flight. Finish the native reset before accepting the
        # next RFC2217 parameter request.
        if self.logger:
            self.logger.info("Activating %s", name)
        reset_strategy()

    def _acknowledge_control(self, control):
        self.rfc2217_send_subnegotiation(SERVER_SET_CONTROL, control)

    def check_modem_lines(self, force_notification=False):
        """USB Serial/JTAG has no UART modem-state lines to poll."""

        return

    def _acknowledge_setting(self, option, value, server_option, expected_size):
        if len(value) != expected_size:
            if self.logger:
                self.logger.warning("Ignoring malformed RFC2217 setting: %r", option)
            return True
        if any(value):
            self._logical_settings[option] = value
        self.rfc2217_send_subnegotiation(server_option, self._logical_settings[option])
        return True

    def _telnet_process_subnegotiation(self, suboption):
        if suboption[0:1] == COM_PORT_OPTION:
            option = suboption[1:2]
            value = suboption[2:]
            if option == SET_BAUDRATE:
                return self._acknowledge_setting(option, value, SERVER_SET_BAUDRATE, 4)
            if option == SET_DATASIZE:
                return self._acknowledge_setting(option, value, SERVER_SET_DATASIZE, 1)
            if option == SET_PARITY:
                return self._acknowledge_setting(option, value, SERVER_SET_PARITY, 1)
            if option == SET_STOPSIZE:
                return self._acknowledge_setting(option, value, SERVER_SET_STOPSIZE, 1)
        if suboption[0:1] == COM_PORT_OPTION and suboption[1:2] == SET_CONTROL:
            control = suboption[2:3]
            if control == SET_CONTROL_REQ_FLOW_SETTING:
                self._acknowledge_control(self._logical_flow_control)
                return
            if control in (
                SET_CONTROL_USE_NO_FLOW_CONTROL,
                SET_CONTROL_USE_SW_FLOW_CONTROL,
                SET_CONTROL_USE_HW_FLOW_CONTROL,
            ):
                self._logical_flow_control = control
                self._acknowledge_control(control)
                return
            if control == SET_CONTROL_DTR_OFF:
                self.is_download_mode = False
                self.serial.dtr = False
                self._acknowledge_control(control)
                return
            if control == SET_CONTROL_RTS_OFF and not self.is_download_mode:
                self._run_reset(HardReset(self.serial, uses_usb=True), "usb_hard_reset")
                self._acknowledge_control(control)
                return
            if control == SET_CONTROL_DTR_ON and not self.is_download_mode:
                self.is_download_mode = True
                self._run_reset(USBJTAGSerialReset(self.serial), "usb_download_reset")
                self._acknowledge_control(control)
                return
            if control in (SET_CONTROL_DTR_ON, SET_CONTROL_RTS_ON, SET_CONTROL_RTS_OFF):
                self._acknowledge_control(control)
                return
        super()._telnet_process_subnegotiation(suboption)


class UsbSerialJtagRedirector(Redirector):
    """Keep native USB reset control lines and avoid usbser write completion stalls."""

    _USB_WRITE_CHUNK_SIZE = 64
    _USB_WRITE_SETTLE_SECONDS = 0.005

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

    def _write_usb_payload(self, payload):
        """Serialize usbser writes without reusing its pending OVERLAPPED buffer."""

        for offset in range(0, len(payload), self._USB_WRITE_CHUNK_SIZE):
            chunk = payload[offset:offset + self._USB_WRITE_CHUNK_SIZE]
            written = self.serial.write(chunk)
            if written != len(chunk):
                raise serial.SerialTimeoutException(
                    f"usbser accepted {written} of {len(chunk)} bytes"
                )
            time.sleep(self._USB_WRITE_SETTLE_SECONDS)

    def writer(self):
        """Copy RFC2217 payloads to USB Serial/JTAG without corrupting large writes."""

        while self.alive:
            try:
                data = self.socket.recv(1024)
                if not data:
                    break
                payload = b"".join(self.rfc2217.filter(data))
                if payload:
                    self._write_usb_payload(payload)
            except OSError as error:
                self.log.error("%s", error)
                break
        self.stop()


def main():
    """Serve COM6 without the upstream UART-only DTR/RTS lifecycle calls."""

    rfc_server.check_deprecated_py_suffix("esp_rfc2217_server")
    parser = argparse.ArgumentParser(
        description="RFC 2217 Serial to Network (TCP/IP) redirector.",
        epilog="Only one connection at once is supported.",
    )
    parser.add_argument("SERIALPORT")
    parser.add_argument("-p", "--localport", type=int, default=2217, metavar="TCPPORT")
    parser.add_argument("-v", "--verbose", dest="verbosity", action="count", default=0)
    parser.add_argument("--r0", action="store_true")
    parser.add_argument(
        "--one-client",
        action="store_true",
        help="exit after one client session so the supervising bridge can reopen COM",
    )
    args = parser.parse_args()

    verbosity = min(args.verbosity, 3)
    level = (logging.WARNING, logging.INFO, logging.DEBUG, logging.NOTSET)[verbosity]
    logging.basicConfig(format="%(levelname)s: %(message)s", level=logging.INFO)
    logging.getLogger("rfc2217").setLevel(level)

    serial_port = serial.serial_for_url(args.SERIALPORT, do_not_open=True, exclusive=True)
    serial_port.timeout = 3
    logging.info("RFC 2217 TCP/IP to Serial redirector - type Ctrl-C / BREAK to quit")
    try:
        serial_port.open()
    except serial.SerialException as error:
        logging.error("Could not open serial port %s: %s", serial_port.name, error)
        return 1

    logging.info("Serving serial port: %s", serial_port.name)
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(("", args.localport))
    server_socket.listen(1)
    logging.info("TCP/IP port: %s", args.localport)

    try:
        host_ip = socket.gethostbyname(socket.gethostname())
    except OSError:
        host_ip = "127.0.0.1"
    logging.info(
        "Waiting for connection ... use the 'rfc2217://%s:%s?ign_set_control' as a PORT",
        host_ip,
        args.localport,
    )

    try:
        while True:
            server_socket.settimeout(5)
            client_socket = None
            try:
                while client_socket is None:
                    try:
                        client_socket, address = server_socket.accept()
                    except TimeoutError:
                        print(".", end="", flush=True)
            except KeyboardInterrupt:
                print("")
                logging.info("Exited with keyboard interrupt")
                break

            redirector = None
            try:
                logging.info("Connected by %s:%s", address[0], address[1])
                client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                # Native USB Serial/JTAG rejects the upstream server's direct
                # RTS/DTR toggles here. The PortManager below performs only the
                # USB-specific reset sequence requested by the remote client.
                redirector = UsbSerialJtagRedirector(
                    serial_port,
                    client_socket,
                    verbosity > 0,
                    args.r0,
                )
                redirector.shortcircuit()
            except KeyboardInterrupt:
                print(flush=True)
                break
            except OSError as error:
                logging.error("%s", error)
            finally:
                if redirector is not None:
                    redirector.stop()
                if client_socket is not None:
                    client_socket.close()
                logging.info("Disconnected")
            if args.one_client:
                # Reopening the native USB endpoint between clients avoids
                # retaining a failed usbser session into the next flash.
                break
    finally:
        server_socket.close()
        serial_port.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
