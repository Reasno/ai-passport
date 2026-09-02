#!/usr/bin/env python3
"""Inject find-ring packets over the debug serial console and record kp_find logs.

Verifies the dual-stack de-duplication contract without a second Passport:
the same ts arriving on both transports must ring once, a new ts must ring
again, and a repeat inside the window must be ignored.
"""
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
lines = []
stop = threading.Event()


def reader(ser):
    buf = b""
    while not stop.is_set():
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode("utf-8", "replace").strip()
            if text:
                lines.append((time.time(), text))


with serial.Serial(PORT, 115200, timeout=0.1) as ser:
    t = threading.Thread(target=reader, args=(ser,), daemon=True)
    t.start()
    time.sleep(1.0)
    ser.reset_input_buffer()

    steps = [
        ("RING espnow 4242", "fresh: first sighting of 4242"),
        ("RING mqtt 4242", "duplicate: same ts on the other transport"),
        ("RING espnow 4242", "duplicate: same ts, same transport"),
        ("RING mqtt 9001", "fresh: different ts"),
        ("RING espnow 9001", "duplicate: 9001 already claimed"),
    ]
    for cmd, note in steps:
        marker = f"---- {cmd}  ({note})"
        lines.append((time.time(), marker))
        ser.write((cmd + "\n").encode())
        ser.flush()
        time.sleep(1.5)
    time.sleep(2.0)
    stop.set()
    t.join(timeout=2)

keep = ("----", "kp_find:", "KP_RING")
verbose = "--all" in sys.argv
for _, text in lines:
    if verbose or any(k in text for k in keep):
        print(text)
