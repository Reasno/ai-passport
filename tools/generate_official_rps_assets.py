#!/usr/bin/env python3
"""Downscale the official AI Passport RPS preview art to UI thumbnails.

The source PNGs come from upstream/demo/rock-paper-scissors. Output is raw
RGB565 little-endian for ESP-IDF EMBED_FILES and LVGL 9 image descriptors.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main" / "assets" / "rps_official"
WIDTH = 54
HEIGHT = 72


def to_rgb565_le(image: Image.Image) -> bytes:
    out = bytearray()
    for r, g, b in image.convert("RGB").getdata():
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out.extend((value & 0xFF, value >> 8))
    return bytes(out)


for name in ("rock", "scissors", "paper"):
    source = Image.open(ASSETS / f"{name}.png").convert("RGB")
    thumb = source.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    thumb.save(ASSETS / f"{name}_54x72.png")
    (ASSETS / f"{name}_54x72.rgb565").write_bytes(to_rgb565_le(thumb))
    print(f"generated {name}: {WIDTH}x{HEIGHT}, {WIDTH * HEIGHT * 2} bytes")
