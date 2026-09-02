#!/usr/bin/env python3
"""Reset the board, inject a single find-ring, and trace the aftermath.

Used to tell a transient render spike apart from a state the ring leaves behind:
the ring auto-stops after 30s, so any watchdog hits past that point mean the
LVGL task never went back to idle.
"""
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
WATCH = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0

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


ser = serial.Serial(PORT, 115200, timeout=0.1)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.2)
ser.setRTS(False)
time.sleep(1.5)
ser.reset_input_buffer()

t = threading.Thread(target=reader, args=(ser,), daemon=True)
t.start()
time.sleep(3.0)

t0 = time.time()
lines.append((time.time(), "---- inject RING espnow 5150"))
ser.write(b"RING espnow 5150\n")
ser.flush()

time.sleep(WATCH)
stop.set()
t.join(timeout=2)
ser.close()

for ts, text in lines:
    tag = "watchdog got triggered" in text
    if tag or "----" in text or "kp_" in text or "KP_" in text:
        print(f"[t+{ts - t0:6.1f}s] {text}")
