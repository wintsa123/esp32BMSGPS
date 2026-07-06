#!/usr/bin/env python3
import argparse
import socket
import sys
import time
from urllib.parse import urlparse


def parse_endpoint(value: str) -> tuple[str, int]:
    if value.startswith("socket://"):
        parsed = urlparse(value)
        if not parsed.hostname or parsed.port is None:
            raise ValueError(f"invalid socket endpoint: {value}")
        return parsed.hostname, parsed.port

    if ":" not in value:
        raise ValueError("endpoint must be socket://host:port or host:port")

    host, port_text = value.rsplit(":", 1)
    return host, int(port_text)


def write_text_bytes(data: bytes, raw: bool) -> int:
    if raw:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        return data.count(0)

    nul_count = data.count(0)
    data = data.replace(b"\x00", b"").replace(b"\r\n", b"\n")
    if data:
        sys.stdout.write(data.decode("utf-8", errors="replace"))
        sys.stdout.flush()
    return nul_count


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read logs from a raw TCP serial bridge such as socket://192.168.2.10:4000."
    )
    parser.add_argument("endpoint", help="socket://host:port or host:port")
    parser.add_argument(
        "--duration",
        type=float,
        default=30.0,
        help="seconds to read; 0 reads until the socket closes",
    )
    parser.add_argument(
        "--connect-timeout",
        type=float,
        default=5.0,
        help="TCP connect timeout in seconds",
    )
    parser.add_argument(
        "--idle-timeout",
        type=float,
        default=2.0,
        help="socket read timeout in seconds",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="write bytes exactly as received, including NUL bytes",
    )
    args = parser.parse_args()

    try:
        host, port = parse_endpoint(args.endpoint)
    except ValueError as error:
        parser.error(str(error))

    deadline = None if args.duration <= 0 else time.monotonic() + args.duration
    total_bytes = 0
    nul_bytes = 0

    sys.stderr.write(f"[raw-socket-monitor] connecting to {host}:{port}\n")
    with socket.create_connection((host, port), timeout=args.connect_timeout) as sock:
        sock.settimeout(args.idle_timeout)
        sys.stderr.write("[raw-socket-monitor] connected\n")

        while deadline is None or time.monotonic() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue

            if not data:
                sys.stderr.write("[raw-socket-monitor] socket closed\n")
                break

            total_bytes += len(data)
            nul_bytes += write_text_bytes(data, args.raw)

    if nul_bytes and not args.raw:
        sys.stderr.write(f"[raw-socket-monitor] filtered {nul_bytes} NUL byte(s)\n")
    sys.stderr.write(f"[raw-socket-monitor] read {total_bytes} byte(s)\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
