#!/usr/bin/env python3
"""Convert the checked-in 96x156 RGBA avatars to LVGL 9 RGB565A8 C assets.

Uses only Python's standard library. RGB565 pixels are emitted little-endian,
followed by a full A8 plane, matching LV_COLOR_FORMAT_RGB565A8.
"""
from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_rgba_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path}: not a PNG")

    pos = 8
    chunks: list[bytes] = []
    width = height = 0
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError(f"{path}: expected non-interlaced 8-bit RGBA PNG")
        elif kind == b"IDAT":
            chunks.append(payload)
        elif kind == b"IEND":
            break

    packed = zlib.decompress(b"".join(chunks))
    row_bytes = width * 4
    expected = height * (row_bytes + 1)
    if len(packed) != expected:
        raise ValueError(f"{path}: unexpected decompressed size")

    rgba = bytearray()
    previous = bytearray(row_bytes)
    offset = 0
    for _ in range(height):
        filter_type = packed[offset]
        raw = packed[offset + 1:offset + 1 + row_bytes]
        offset += row_bytes + 1
        row = bytearray(row_bytes)
        for i, value in enumerate(raw):
            left = row[i - 4] if i >= 4 else 0
            up = previous[i]
            upper_left = previous[i - 4] if i >= 4 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = up
            elif filter_type == 3:
                predictor = (left + up) // 2
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else upper_left
            else:
                raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
            row[i] = (value + predictor) & 0xFF
        rgba.extend(row)
        previous = row
    return width, height, bytes(rgba)


def make_rgb565a8(rgba: bytes) -> bytes:
    colors = bytearray()
    alpha = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        colors.extend(struct.pack("<H", pixel))
        alpha.append(a)
    return bytes(colors + alpha)


def write_c(source: Path, output: Path, symbol: str) -> None:
    width, height, rgba = read_rgba_png(source)
    if (width, height) != (96, 156):
        raise ValueError(f"{source}: expected 96x156, got {width}x{height}")
    image = make_rgb565a8(rgba)
    lines = []
    for i in range(0, len(image), 16):
        lines.append("    " + ", ".join(f"0x{x:02x}" for x in image[i:i + 16]) + ",")
    output.write_text(
        '#include "lvgl.h"\n\n'
        f'static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {symbol}_map[] = {{\n'
        + "\n".join(lines)
        + f'\n}};\n\nconst lv_image_dsc_t {symbol} = {{\n'
          '    .header.magic = LV_IMAGE_HEADER_MAGIC,\n'
          '    .header.cf = LV_COLOR_FORMAT_RGB565A8,\n'
          '    .header.flags = 0,\n'
        f'    .header.w = {width},\n'
        f'    .header.h = {height},\n'
        f'    .header.stride = {width * 2},\n'
        f'    .data_size = sizeof({symbol}_map),\n'
        f'    .data = {symbol}_map,\n'
          '};\n',
        encoding="utf-8",
    )
    print(f"{source} -> {output} ({len(image)} bytes RGB565A8)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    assets = args.root / "main" / "assets"
    for role in ("brother", "sister"):
        write_c(assets / f"avatar_{role}.png", assets / f"avatar_{role}.c", f"avatar_{role}")


if __name__ == "__main__":
    main()
