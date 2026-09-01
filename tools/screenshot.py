#!/usr/bin/env python3
"""Request and reconstruct an AI Passport LVGL screenshot over USB Serial/JTAG."""
import argparse
import binascii
import struct
import sys
import time
from pathlib import Path

HEADER_MAGIC = b"KPSS2 "
RECORD_MAGIC = b"KPRC"
RECORD = struct.Struct("<4sHHHHI")
RECORD_CRC = struct.Struct("<I")
TRAILER_MAGIC = b"KPSS_END "
WIDTH = 240
HEIGHT = 320
MAX_RECORD_BYTES = WIDTH * HEIGHT * 2


class SerialStream:
    """Buffered serial reader that can resynchronise after console-log noise."""

    def __init__(self, port):
        self.port = port
        self.buffer = bytearray()

    def fill(self, size):
        while len(self.buffer) < size:
            chunk = self.port.read(max(256, size - len(self.buffer)))
            if not chunk:
                raise TimeoutError(f"serial timeout with {len(self.buffer)}/{size} buffered bytes")
            self.buffer.extend(chunk)

    def seek_line(self, marker):
        while True:
            index = self.buffer.find(marker)
            if index >= 0:
                del self.buffer[:index]
                while b"\n" not in self.buffer:
                    self.fill(len(self.buffer) + 1)
                end = self.buffer.index(b"\n") + 1
                line = bytes(self.buffer[:end])
                del self.buffer[:end]
                return line
            if b"Guru Meditation Error" in self.buffer:
                raise RuntimeError("device crashed while producing screenshot")
            if len(self.buffer) > len(marker) - 1:
                del self.buffer[:-(len(marker) - 1)]
            self.fill(len(self.buffer) + 1)

    def next_record(self, width, height):
        while True:
            index = self.buffer.find(RECORD_MAGIC)
            if index < 0:
                if b"Guru Meditation Error" in self.buffer:
                    raise RuntimeError("device crashed while producing screenshot")
                if len(self.buffer) > len(RECORD_MAGIC) - 1:
                    del self.buffer[:-(len(RECORD_MAGIC) - 1)]
                self.fill(len(self.buffer) + 1)
                continue
            if index:
                del self.buffer[:index]

            self.fill(RECORD.size)
            raw_header = bytes(self.buffer[:RECORD.size])
            magic, x1, y1, x2, y2, length = RECORD.unpack(raw_header)
            valid_area = 0 <= x1 <= x2 < width and 0 <= y1 <= y2 < height
            expected = (x2 - x1 + 1) * (y2 - y1 + 1) * 2 if valid_area else -1
            if magic != RECORD_MAGIC or not valid_area or length != expected or length > MAX_RECORD_BYTES:
                del self.buffer[0]
                continue

            total = RECORD.size + length + RECORD_CRC.size
            self.fill(total)
            payload_end = RECORD.size + length
            payload = bytes(self.buffer[RECORD.size:payload_end])
            remote_crc = RECORD_CRC.unpack(self.buffer[payload_end:total])[0]
            local_crc = binascii.crc32(raw_header)
            local_crc = binascii.crc32(payload, local_crc) & 0xFFFFFFFF
            if remote_crc != local_crc:
                # Keep all remaining bytes so a later KPRC can be found after corruption/noise.
                del self.buffer[0]
                continue

            del self.buffer[:total]
            return raw_header, (x1, y1, x2, y2), payload


def wait_page_response(port, page):
    expected = f"KP_PAGE_OK {page}".encode()
    while True:
        line = port.readline()
        if not line:
            raise TimeoutError(f"did not receive PAGE response for {page}")
        if b"KP_PAGE_ERR " in line:
            raise RuntimeError(line[line.index(b"KP_PAGE_ERR "):].decode("ascii", "replace").strip())
        if expected in line:
            return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="for example /dev/cu.usbmodem1101")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by native USB but required by pyserial")
    parser.add_argument("--output", type=Path, default=Path("screenshot.png"))
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--page", choices=("HOME", "TASKS", "REDEEM", "LOTTERY", "GAMES", "FIND", "RPS"),
                        help="switch to a side-effect-free debug preview before capture")
    parser.add_argument("--settle-ms", type=int, default=300, help="delay after PAGE acknowledgement")
    args = parser.parse_args()
    try:
        import serial
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("install host dependencies: python3 -m pip install pyserial Pillow") from exc

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as port:
        port.reset_input_buffer()
        if args.page:
            port.write(f"PAGE {args.page}\n".encode())
            port.flush()
            wait_page_response(port, args.page)
            time.sleep(max(args.settle_ms, 0) / 1000.0)
        port.write(b"SCREENSHOT\n")
        port.flush()

        stream = SerialStream(port)
        line = stream.seek_line(HEADER_MAGIC).decode("ascii", "replace").strip().split()
        if line != ["KPSS2", str(WIDTH), str(HEIGHT), "RGB565LE", "KPRC"]:
            raise RuntimeError(f"unexpected screenshot header: {' '.join(line)}")

        rgb = bytearray(WIDTH * HEIGHT * 3)
        covered = bytearray(WIDTH * HEIGHT)
        covered_count = 0
        frame_crc = 0
        records = 0
        while covered_count < WIDTH * HEIGHT:
            raw_header, area, payload = stream.next_record(WIDTH, HEIGHT)
            x1, y1, x2, y2 = area
            frame_crc = binascii.crc32(raw_header, frame_crc)
            frame_crc = binascii.crc32(payload, frame_crc)
            offset = 0
            for y in range(y1, y2 + 1):
                for x in range(x1, x2 + 1):
                    value = payload[offset] | payload[offset + 1] << 8
                    offset += 2
                    pos = y * WIDTH + x
                    if not covered[pos]:
                        covered[pos] = 1
                        covered_count += 1
                    dst = pos * 3
                    rgb[dst] = ((value >> 11) & 0x1F) * 255 // 31
                    rgb[dst + 1] = ((value >> 5) & 0x3F) * 255 // 63
                    rgb[dst + 2] = (value & 0x1F) * 255 // 31
            records += 1

        fields = stream.seek_line(TRAILER_MAGIC).decode("ascii", "replace").strip().split()
        if len(fields) != 3 or fields[0] != "KPSS_END":
            raise RuntimeError(f"unexpected trailer: {' '.join(fields)}")
        remote_crc, remote_pixels = int(fields[1], 16), int(fields[2])
        if remote_crc != frame_crc & 0xFFFFFFFF:
            raise RuntimeError(f"frame CRC mismatch: device={remote_crc:08x} host={frame_crc & 0xFFFFFFFF:08x}")
        if remote_pixels != WIDTH * HEIGHT or covered_count != WIDTH * HEIGHT:
            raise RuntimeError(
                f"incomplete frame: device={remote_pixels}, covered={covered_count}/{WIDTH * HEIGHT}")

    Image.frombytes("RGB", (WIDTH, HEIGHT), bytes(rgb)).save(args.output)
    print(f"saved {args.output} ({records} chunks, CRC {remote_crc:08x})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"screenshot failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
