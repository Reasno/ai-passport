#!/usr/bin/env python3
"""Passively watch the debug serial console for a fixed window (no injection)."""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

with serial.Serial(PORT, 115200, timeout=0.2) as ser:
    deadline = time.time() + SECONDS
    buf = b""
    while time.time() < deadline:
        buf += ser.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode("utf-8", "replace").strip()
            if text:
                print(text, flush=True)
