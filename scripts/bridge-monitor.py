#!/usr/bin/env python3
"""Read ESP32 serial log via RFC2217 bridge, print it, and save it locally."""
from datetime import datetime
from pathlib import Path
import sys

import serial

PORT = "rfc2217://192.168.2.10:4000?ign_set_control"
BAUD = 115200
LOG_DIR = Path(__file__).resolve().parents[1] / "logs"

LOG_DIR.mkdir(exist_ok=True)
log_path = LOG_DIR / f"serial-{datetime.now():%Y-%m-%d_%H-%M-%S}.log"
ser = serial.serial_for_url(PORT, BAUD, timeout=0.2)
print(f"connected {PORT}", flush=True)
print(f"saving log to {log_path}", flush=True)
try:
    with log_path.open("ab", buffering=0) as log_file:
        while True:
            data = ser.read(4096)
            if data:
                log_file.write(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
except KeyboardInterrupt:
    pass
finally:
    ser.close()
