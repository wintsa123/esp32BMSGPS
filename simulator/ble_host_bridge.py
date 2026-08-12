#!/usr/bin/env python3
"""Windows BLE to TCP bridge used by the desktop simulator."""

from __future__ import annotations

import argparse
import asyncio
import re
import sys
from dataclasses import dataclass
from typing import Any, Callable, Optional


BLUETOOTH_BASE_UUID = "0000{}-0000-1000-8000-00805f9b34fb"
KINDS = {"BMS", "CONTROLLER"}
MAC_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")
SCAN_RESULT_LIMIT = 12


def expand_uuid(value: str) -> str:
    value = value.lower()
    if re.fullmatch(r"[0-9a-f]{4}", value):
        return BLUETOOTH_BASE_UUID.format(value)
    return value


@dataclass(frozen=True)
class GattProfile:
    name: str
    service: str
    notify: str
    write: str


BMS_PROFILES = {
    0: GattProfile("ANT", expand_uuid("FFE0"), expand_uuid("FFE1"), expand_uuid("FFE1")),
    1: GattProfile("JK", expand_uuid("FFE0"), expand_uuid("FFE1"), expand_uuid("FFE1")),
    2: GattProfile("JBD", expand_uuid("FF00"), expand_uuid("FF01"), expand_uuid("FF02")),
    3: GattProfile("DALY", expand_uuid("FFF0"), expand_uuid("FFF1"), expand_uuid("FFF2")),
    4: GattProfile(
        "YANYANG",
        "0000fe59-0000-1000-8000-00805f9b34fb",
        "8ec90002-f315-4f60-9fb8-838830daea50",
        "8ec90001-f315-4f60-9fb8-838830daea50",
    ),
}

CONTROLLER_PROFILES = (
    GattProfile(
        "NUS",
        "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
        "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
        "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
    ),
    GattProfile("FFE0", expand_uuid("FFE0"), expand_uuid("FFEC"), expand_uuid("FFEF")),
)


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class Command:
    action: str
    kind: Optional[str] = None
    device_type: Optional[int] = None
    value: Optional[str] = None


@dataclass
class ScanRecord:
    device: Any
    address: str
    name: str
    rssi: int
    service_match: bool


def advertised_service_match(kind: str, advertisement: Any) -> bool:
    advertised = {
        str(uuid).lower() for uuid in (getattr(advertisement, "service_uuids", None) or ())
    }
    profiles = BMS_PROFILES.values() if kind == "BMS" else CONTROLLER_PROFILES
    return any(profile.service in advertised for profile in profiles)


def parse_command(line: str) -> Command:
    if not line or not line.isascii():
        raise ProtocolError("命令必须是非空 ASCII 文本")
    parts = line.split()
    action = parts[0].upper()

    if action == "QUIT" and len(parts) == 1:
        return Command(action)
    if action in {"SCAN", "DISCONNECT"} and len(parts) == 2:
        kind = parts[1].upper()
        if kind not in KINDS:
            raise ProtocolError("设备类别必须是 BMS 或 CONTROLLER")
        return Command(action, kind)
    if action == "CONNECT" and len(parts) == 4:
        kind = parts[1].upper()
        if kind not in KINDS:
            raise ProtocolError("设备类别必须是 BMS 或 CONTROLLER")
        try:
            device_type = int(parts[2], 10)
        except ValueError as exc:
            raise ProtocolError("设备类型必须是整数") from exc
        if (kind == "BMS" and device_type not in BMS_PROFILES) or (
            kind == "CONTROLLER" and device_type != 0
        ):
            raise ProtocolError("不支持的设备类型")
        if not MAC_RE.fullmatch(parts[3]):
            raise ProtocolError("蓝牙地址格式无效")
        return Command(action, kind, device_type, parts[3])
    if action == "WRITE" and len(parts) == 3:
        kind = parts[1].upper()
        if kind not in KINDS:
            raise ProtocolError("设备类别必须是 BMS 或 CONTROLLER")
        payload = parts[2]
        if not payload or len(payload) % 2 or not re.fullmatch(r"[0-9A-Fa-f]+", payload):
            raise ProtocolError("写入内容必须是偶数长度的十六进制")
        return Command(action, kind, value=payload)
    raise ProtocolError("未知命令或参数数量错误")


def text_hex(value: Any) -> str:
    return str(value or "").encode("utf-8").hex()


class BleHostBridge:
    def __init__(self, bleak: Any, scan_timeout: float = 5.0) -> None:
        self.bleak = bleak
        self.scan_timeout = scan_timeout
        self.writer: Optional[asyncio.StreamWriter] = None
        self.clients: dict[str, Any] = {}
        self.profiles: dict[str, GattProfile] = {}
        self.scanned_devices: dict[str, dict[str, Any]] = {kind: {} for kind in KINDS}
        self.scan_tasks: dict[str, asyncio.Task[None]] = {}
        self._silent_disconnects: set[int] = set()
        self._loop: Optional[asyncio.AbstractEventLoop] = None

    async def emit(self, *fields: Any) -> None:
        writer = self.writer
        if writer is None or writer.is_closing():
            return
        writer.write(("\t".join(str(field) for field in fields) + "\n").encode("ascii"))
        try:
            await writer.drain()
        except (ConnectionError, asyncio.CancelledError):
            pass

    async def emit_error(self, kind: str, message: Any) -> None:
        await self.emit("ERROR", kind, text_hex(message))

    def _schedule_emit(self, *fields: Any) -> None:
        loop = self._loop
        if loop is None or loop.is_closed():
            return

        def create() -> None:
            asyncio.create_task(self.emit(*fields))

        loop.call_soon_threadsafe(create)

    async def handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        old_writer = self.writer
        if old_writer is not None and old_writer is not writer and not old_writer.is_closing():
            old_writer.close()
        self.writer = writer
        self._loop = asyncio.get_running_loop()
        await self.emit("READY", 1)
        try:
            while not reader.at_eof():
                raw = await reader.readline()
                if not raw:
                    break
                try:
                    line = raw.decode("utf-8").rstrip("\r\n")
                    command = parse_command(line)
                    if not await self.dispatch(command):
                        break
                except (UnicodeDecodeError, ProtocolError) as exc:
                    await self.emit_error("HOST", exc)
        finally:
            if self.writer is writer:
                self.writer = None
                await self.close_sessions()
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, AttributeError):
                pass

    async def dispatch(self, command: Command) -> bool:
        if command.action == "QUIT":
            return False
        assert command.kind is not None
        if command.action == "SCAN":
            old_task = self.scan_tasks.get(command.kind)
            if old_task and not old_task.done():
                old_task.cancel()
            task = asyncio.create_task(self.scan(command.kind))
            self.scan_tasks[command.kind] = task
        elif command.action == "CONNECT":
            assert command.device_type is not None and command.value is not None
            await self.connect(command.kind, command.device_type, command.value)
        elif command.action == "DISCONNECT":
            await self.disconnect(command.kind)
        elif command.action == "WRITE":
            assert command.value is not None
            await self.write(command.kind, command.value)
        return True

    async def close_sessions(self) -> None:
        tasks = [task for task in self.scan_tasks.values() if not task.done()]
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        self.scan_tasks.clear()
        for kind in tuple(self.clients):
            await self.disconnect(kind, announce=False)

    async def scan(self, kind: str) -> None:
        records: dict[str, ScanRecord] = {}
        self.scanned_devices[kind] = {}
        await self.emit("SCAN_CLEAR", kind)

        def detection_callback(device: Any, advertisement: Any) -> None:
            address = str(getattr(device, "address", ""))
            key = address.casefold()
            if not address:
                return
            name = getattr(advertisement, "local_name", None) or getattr(device, "name", None) or ""
            service_match = advertised_service_match(kind, advertisement)
            display_name = name or address
            rssi = getattr(advertisement, "rssi", None)
            if rssi is None:
                rssi = getattr(device, "rssi", 0)
            previous = records.get(key)
            if previous is None or (service_match, bool(name), int(rssi)) > (
                previous.service_match,
                previous.name != previous.address,
                previous.rssi,
            ):
                records[key] = ScanRecord(
                    device, address, display_name, int(rssi), service_match
                )

        scanner = self.bleak.BleakScanner(detection_callback=detection_callback)
        try:
            await scanner.start()
            await asyncio.sleep(self.scan_timeout)
            await scanner.stop()
            ranked = sorted(
                records.values(),
                key=lambda record: (
                    record.service_match,
                    record.name != record.address,
                    record.rssi,
                ),
                reverse=True,
            )[:SCAN_RESULT_LIMIT]
            for record in ranked:
                self.scanned_devices[kind][record.address.casefold()] = record.device
                await self.emit(
                    "SCAN_RESULT", kind, record.address, record.rssi, text_hex(record.name)
                )
            await self.emit("SCAN_DONE", kind)
        except asyncio.CancelledError:
            try:
                await scanner.stop()
            except Exception:
                pass
            raise
        except Exception as exc:
            await self.emit_error(kind, exc)
            await self.emit("SCAN_DONE", kind)

    def _disconnected_callback(self, kind: str) -> Callable[[Any], None]:
        def callback(client: Any) -> None:
            self.clients.pop(kind, None)
            self.profiles.pop(kind, None)
            client_id = id(client)
            if client_id in self._silent_disconnects:
                self._silent_disconnects.discard(client_id)
                return
            self._schedule_emit("DISCONNECTED", kind, text_hex("连接已断开"))

        return callback

    @staticmethod
    def _has_service(client: Any, uuid: str) -> bool:
        services = getattr(client, "services", None)
        if services is None:
            return False
        getter = getattr(services, "get_service", None)
        if getter is not None:
            return getter(uuid) is not None
        return any(str(getattr(service, "uuid", service)).lower() == uuid for service in services)

    async def connect(self, kind: str, device_type: int, address: str) -> None:
        await self.disconnect(kind, announce=False)
        target = self.scanned_devices[kind].get(address.casefold(), address)
        client = self.bleak.BleakClient(target, disconnected_callback=self._disconnected_callback(kind))
        try:
            await client.connect()
            if kind == "BMS":
                profile = BMS_PROFILES[device_type]
            else:
                profile = next(
                    (candidate for candidate in CONTROLLER_PROFILES if self._has_service(client, candidate.service)),
                    None,
                )
                if profile is None:
                    raise RuntimeError("控制器未提供 NUS 或 FFE0 服务")
            await client.start_notify(
                profile.notify,
                lambda _sender, data: self._schedule_emit("NOTIFY", kind, bytes(data).hex()),
            )
            self.clients[kind] = client
            self.profiles[kind] = profile
            name = getattr(target, "name", None) or getattr(client, "name", None) or address
            await self.emit("CONNECTED", kind, text_hex(name), profile.name)
        except Exception as exc:
            try:
                if getattr(client, "is_connected", False):
                    self._silent_disconnects.add(id(client))
                    await client.disconnect()
            except Exception:
                pass
            await self.emit_error(kind, exc)

    async def disconnect(self, kind: str, announce: bool = True) -> None:
        client = self.clients.pop(kind, None)
        self.profiles.pop(kind, None)
        if client is None:
            if announce:
                await self.emit("DISCONNECTED", kind, text_hex("未连接"))
            return
        try:
            self._silent_disconnects.add(id(client))
            await client.disconnect()
            if announce:
                await self.emit("DISCONNECTED", kind, text_hex("主动断开"))
        except Exception as exc:
            self._silent_disconnects.discard(id(client))
            await self.emit_error(kind, exc)

    async def write(self, kind: str, payload_hex: str) -> None:
        client = self.clients.get(kind)
        profile = self.profiles.get(kind)
        if client is None or profile is None:
            await self.emit_error(kind, "设备尚未连接")
            return
        try:
            await client.write_gatt_char(
                profile.write,
                bytes.fromhex(payload_hex),
                response=kind == "BMS",
            )
        except Exception as exc:
            await self.emit_error(kind, exc)


async def serve(bleak: Any, host: str, port: int, scan_timeout: float) -> None:
    bridge = BleHostBridge(bleak, scan_timeout)
    server = await asyncio.start_server(bridge.handle_client, host, port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
    print(f"BLE 桥已监听：{addresses}", flush=True)
    async with server:
        await server.serve_forever()


def load_bleak() -> Any:
    try:
        import bleak  # type: ignore
    except ImportError as exc:
        raise RuntimeError("未安装 bleak，请先执行：py -3 -m pip install bleak") from exc
    return bleak


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="LVGL 模拟器 Windows BLE TCP 桥")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--scan-timeout", type=float, default=5.0)
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("--port 必须在 1..65535 范围内")
    if args.scan_timeout <= 0:
        parser.error("--scan-timeout 必须大于 0")
    try:
        bleak = load_bleak()
        asyncio.run(serve(bleak, args.host, args.port, args.scan_timeout))
    except KeyboardInterrupt:
        return 0
    except RuntimeError as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
