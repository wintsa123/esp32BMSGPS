#!/usr/bin/env python3

import argparse
import os
import socket
import struct
import sys
import urllib.request
from pathlib import Path


MAX_BODY_BYTES = 32 * 1024
MAX_PAYLOAD_BYTES = 524
FIXED_MSG_PAYLOAD_BYTES = {
    0x00: 20,
    0x01: 16,
    0x02: 92,
    0x03: 16,
    0x04: 92,
    0x05: 20,
    0x06: 16,
    0x07: 72,
    0x08: 68,
    0x09: 20,
    0x0B: 76,
    0x0C: 20,
    0x0D: 16,
    0x0E: 72,
    0x11: 88,
}


def casbin_checksum(message_class: int, message_id: int, payload: bytes) -> int:
    checksum = len(payload) | (message_class << 16) | (message_id << 24)
    for offset in range(0, len(payload), 4):
        checksum = (checksum + int.from_bytes(payload[offset : offset + 4], "little")) & 0xFFFFFFFF
    return checksum


def build_casbin(message_class: int, message_id: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD_BYTES or len(payload) % 4:
        raise ValueError("invalid CASBIN payload length")
    return (
        b"\xBA\xCE"
        + struct.pack("<HBB", len(payload), message_class, message_id)
        + payload
        + struct.pack("<I", casbin_checksum(message_class, message_id, payload))
    )


def casbin_payload_valid(message_class: int, message_id: int, payload: bytes) -> bool:
    if len(payload) > MAX_PAYLOAD_BYTES or len(payload) % 4:
        return False
    if message_class == 0x0B:
        return message_id == 0x01 and len(payload) == 56
    if message_class != 0x08:
        return False
    if message_id == 0x17:
        return len(payload) >= 16 and len(payload) == 16 + payload[14] * 2
    return len(payload) == FIXED_MSG_PAYLOAD_BYTES.get(message_id)


def iter_casbin_frames(data: bytes):
    if not data or len(data) > MAX_BODY_BYTES:
        raise ValueError("A-GNSS data is empty or too large")
    offset = 0
    while offset < len(data):
        if data[offset : offset + 2] != b"\xBA\xCE" or offset + 10 > len(data):
            raise ValueError(f"invalid CASBIN header at byte {offset}")
        payload_len, message_class, message_id = struct.unpack_from("<HBB", data, offset + 2)
        frame_len = payload_len + 10
        if payload_len > MAX_PAYLOAD_BYTES or payload_len % 4 or offset + frame_len > len(data):
            raise ValueError(f"invalid CASBIN length at byte {offset}")
        payload = data[offset + 6 : offset + 6 + payload_len]
        expected = struct.unpack_from("<I", data, offset + 6 + payload_len)[0]
        if expected != casbin_checksum(message_class, message_id, payload):
            raise ValueError(f"invalid CASBIN checksum at byte {offset}")
        if not casbin_payload_valid(message_class, message_id, payload):
            raise ValueError(
                f"invalid A-GNSS CASBIN message class=0x{message_class:02X} "
                f"id=0x{message_id:02X} length={payload_len}"
            )
        yield data[offset : offset + frame_len]
        offset += frame_len


def validate_casbin_stream(data: bytes) -> int:
    return sum(1 for _ in iter_casbin_frames(data))


def extract_server_data(reply: bytes) -> bytes:
    offset = reply.find(b"\xBA\xCE")
    if offset < 0:
        raise ValueError("server response contains no CASBIN data")
    data = reply[offset:]
    validate_casbin_stream(data)
    return data


def fetch_server_data(args: argparse.Namespace) -> bytes:
    user = args.user or os.environ.get("ZKW_AGNSS_USER", "freetrial")
    password = args.password or os.environ.get("ZKW_AGNSS_PASSWORD", "123456")
    request = (
        f"user={user};pwd={password};cmd=full;lat={args.lat};"
        f"lon={args.lon};alt={args.alt or 0};pacc={args.pacc};"
    ).encode("ascii")
    chunks = []
    with socket.create_connection((args.server_host, args.server_port), args.timeout) as client:
        client.settimeout(args.timeout)
        client.sendall(request)
        while True:
            try:
                chunk = client.recv(4096)
            except TimeoutError:
                break
            if not chunk:
                break
            chunks.append(chunk)
            if sum(map(len, chunks)) > MAX_BODY_BYTES + 256:
                raise ValueError("server response is too large")
    if not chunks:
        raise RuntimeError("A-GNSS server returned no data; use --input or --position-only")
    return extract_server_data(b"".join(chunks))


def build_position_aid(args: argparse.Namespace) -> bytes:
    altitude = 0.0 if args.alt is None else args.alt
    flags = 0x01 | 0x20
    if args.alt is None:
        flags |= 0x40
    payload = struct.pack(
        "<ddddffffIHBB",
        args.lat,
        args.lon,
        altitude,
        0.0,
        0.0,
        args.pacc * args.pacc,
        0.0,
        0.0,
        0,
        0,
        0,
        flags,
    )
    return build_casbin(0x0B, 0x01, payload)


def post_to_device(device: str, frame: bytes, timeout: float) -> str:
    url = device.rstrip("/") + "/api/gps/agnss"
    request = urllib.request.Request(
        url,
        data=frame,
        method="POST",
        headers={"Content-Type": "application/octet-stream"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8")


def self_test() -> None:
    args = argparse.Namespace(lat=30.0, lon=120.0, alt=None, pacc=1000.0)
    data = build_position_aid(args)
    assert len(data) == 66
    assert validate_casbin_stream(data) == 1
    assert extract_server_data(b"AGNSS data from CASIC.\nDataLength: 66.\n" + data) == data

    igp_payload = bytearray(132)
    igp_payload[14] = 58
    igp_payload[15] = 58
    igp = build_casbin(0x08, 0x17, bytes(igp_payload))
    assert len(igp_payload) > 128
    assert list(iter_casbin_frames(data + igp)) == [data, igp]

    invalid = build_casbin(0x08, 0x07, b"\0" * 4)
    try:
        validate_casbin_stream(invalid)
        raise AssertionError("short GPSEPH payload accepted")
    except ValueError:
        pass

    igp_payload[14] = 60
    invalid = build_casbin(0x08, 0x17, bytes(igp_payload))
    try:
        validate_casbin_stream(invalid)
        raise AssertionError("mismatched IGP payload accepted")
    except ValueError:
        pass
    print("A-GNSS helper self-test passed")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fetch or build CASIC A-GNSS data and inject it into the ESP32")
    parser.add_argument("--device", default="http://192.168.4.1")
    parser.add_argument("--lat", type=float)
    parser.add_argument("--lon", type=float)
    parser.add_argument("--alt", type=float)
    parser.add_argument("--pacc", type=float, default=1000.0, help="position accuracy in metres")
    parser.add_argument("--input", type=Path, help="pre-downloaded CASBIN data")
    parser.add_argument("--position-only", action="store_true", help="inject only coarse position without server data")
    parser.add_argument("--server-host", default="121.41.40.95")
    parser.add_argument("--server-port", type=int, default=2621)
    parser.add_argument("--user")
    parser.add_argument("--password")
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return args
    if args.input and args.position_only:
        parser.error("--input and --position-only are mutually exclusive")
    if not args.input and (args.lat is None or args.lon is None):
        parser.error("--lat and --lon are required unless --input is used")
    if args.lat is not None and not -90.0 <= args.lat <= 90.0:
        parser.error("--lat must be between -90 and 90")
    if args.lon is not None and not -180.0 <= args.lon <= 180.0:
        parser.error("--lon must be between -180 and 180")
    if args.pacc <= 0:
        parser.error("--pacc must be positive")
    return args


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    try:
        if args.input:
            data = args.input.read_bytes()
        elif args.position_only:
            data = build_position_aid(args)
        else:
            data = fetch_server_data(args)
        frames = list(iter_casbin_frames(data))
        if args.dry_run:
            print(f"validated {len(frames)} A-GNSS packet(s), {len(data)} bytes")
            return 0
        for frame in frames:
            post_to_device(args.device, frame, args.timeout)
        print(f"injected {len(frames)} A-GNSS packet(s), {len(data)} bytes")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"A-GNSS injection failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
