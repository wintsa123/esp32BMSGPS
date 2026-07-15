#!/usr/bin/env python3

import argparse
import os
import socket
import struct
import sys
import urllib.request
from pathlib import Path


MAX_BODY_BYTES = 32 * 1024
MAX_PAYLOAD_BYTES = 128
ALLOWED_MSG_IDS = {
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x11,
    0x17,
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


def validate_casbin_stream(data: bytes) -> int:
    if not data or len(data) > MAX_BODY_BYTES:
        raise ValueError("A-GNSS data is empty or too large")
    offset = 0
    packets = 0
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
        if not (
            (message_class == 0x0B and message_id == 0x01)
            or (message_class == 0x08 and message_id in ALLOWED_MSG_IDS)
        ):
            raise ValueError(
                f"unsupported A-GNSS CASBIN message class=0x{message_class:02X} id=0x{message_id:02X}"
            )
        offset += frame_len
        packets += 1
    return packets


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


def post_to_device(device: str, data: bytes, timeout: float) -> str:
    url = device.rstrip("/") + "/api/gps/agnss"
    request = urllib.request.Request(
        url,
        data=data,
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
        packets = validate_casbin_stream(data)
        if args.dry_run:
            print(f"validated {packets} A-GNSS packet(s), {len(data)} bytes")
            return 0
        print(post_to_device(args.device, data, args.timeout))
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"A-GNSS injection failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
