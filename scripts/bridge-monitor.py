#!/usr/bin/env python3
"""Read ESP32 serial log via RFC2217 bridge and print to stdout."""
import sys
import serial

PORT = "rfc2217://192.168.2.10:4000?ign_set_control"
BAUD = 115200

ser = serial.serial_for_url(PORT, BAUD, timeout=0.2)
print(f"connected {PORT}", flush=True)
try:
    while True:
        data = ser.read(4096)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
except KeyboardInterrupt:
    pass
finally:
    ser.close()
