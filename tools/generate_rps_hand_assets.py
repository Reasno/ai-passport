#!/usr/bin/env python3
"""Cut the hand gesture out of the official AI Passport RPS art.

The upstream frames are 240x320 renders: a light-outlined hand on top of a radial
burst that fades to black at the corners. Embedding them as RGB565 baked that dark
burst into the UI, which reads as a black disc behind every gesture.

The extraction is purely geometric: flood fill the burst starting from the image
border, treating the hand's light outline as a wall, then keep the connected blob
that survives. No colour-distance keying is used, so nothing inside the gesture can
be eaten by mistake.
"""
import argparse
import subprocess
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main" / "assets" / "rps_official"
OUT_C = ROOT / "main" / "assets" / "rps_official"
LVGL_IMAGE = ROOT / "managed_components" / "lvgl__lvgl" / "scripts" / "LVGLImage.py"
NAMES = ("rock", "scissors", "paper")
BOX = (54, 72)
OUTLINE_MIN = 140  # min(r,g,b) at or above this is the hand's light outline / highlight
PAD = 1


def outline_mask(px, w, h):
    return [[min(px[x, y]) >= OUTLINE_MIN for x in range(w)] for y in range(h)]


def flood_background(light, w, h):
    """Mark every pixel reachable from the border without crossing the outline."""
    seen = [[False] * w for _ in range(h)]
    queue = deque()

    def seed(x, y):
        if not light[y][x] and not seen[y][x]:
            seen[y][x] = True
            queue.append((x, y))

    for x in range(w):
        seed(x, 0)
        seed(x, h - 1)
    for y in range(h):
        seed(0, y)
        seed(w - 1, y)
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and not light[ny][nx]:
                seen[ny][nx] = True
                queue.append((nx, ny))
    return seen


def largest_blob(keep, w, h):
    """8-connected largest component, so detached sparkles are dropped."""
    seen = [[False] * w for _ in range(h)]
    best = []
    for sy in range(h):
        for sx in range(w):
            if not keep[sy][sx] or seen[sy][sx]:
                continue
            blob = []
            seen[sy][sx] = True
            queue = deque([(sx, sy)])
            while queue:
                x, y = queue.popleft()
                blob.append((x, y))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h and keep[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            queue.append((nx, ny))
            if len(blob) > len(best):
                best = blob
    return best


def extract(path):
    source = Image.open(path).convert("RGB")
    w, h = source.size
    px = source.load()
    light = outline_mask(px, w, h)
    background = flood_background(light, w, h)
    keep = [[not background[y][x] for x in range(w)] for y in range(h)]
    blob = largest_blob(keep, w, h)
    if not blob:
        raise SystemExit(f"{path.name}: no gesture found")

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    target = out.load()
    for x, y in blob:
        r, g, b = px[x, y]
        target[x, y] = (r, g, b, 255)

    xs = [x for x, _ in blob]
    ys = [y for _, y in blob]
    box = (max(min(xs) - PAD, 0), max(min(ys) - PAD, 0),
           min(max(xs) + 1 + PAD, w), min(max(ys) + 1 + PAD, h))
    return out.crop(box), len(blob)


def fit(image, box):
    """Nearest-neighbour downscale that keeps the pixel-art edges crisp."""
    bw, bh = box
    scale = min(bw / image.width, bh / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    thumb = image.resize(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", box, (0, 0, 0, 0))
    canvas.alpha_composite(thumb, ((bw - size[0]) // 2, (bh - size[1]) // 2))
    return canvas


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", type=Path, help="also write a KP_CARD-backed preview sheet here")
    parser.add_argument("--no-convert", action="store_true", help="skip LVGLImage.py conversion")
    args = parser.parse_args()

    sheet = Image.new("RGB", (BOX[0] * len(NAMES), BOX[1]), (0x17, 0x26, 0x3B))
    for index, name in enumerate(NAMES):
        cut, pixels = extract(ASSETS / f"{name}.png")
        thumb = fit(cut, BOX)
        png = ASSETS / f"{name}_hand_54x72.png"
        thumb.save(png, optimize=True)
        opaque = sum(1 for p in thumb.getdata() if p[3] > 0)
        print(f"{name}: blob={pixels}px crop={cut.size} opaque={opaque}/{BOX[0] * BOX[1]} -> {png.name}")
        sheet.paste(Image.alpha_composite(
            Image.new("RGBA", BOX, (0x17, 0x26, 0x3B, 255)), thumb).convert("RGB"),
            (index * BOX[0], 0))
        if not args.no_convert:
            subprocess.run([
                "python3", str(LVGL_IMAGE), "--ofmt", "C", "--cf", "ARGB8888",
                "--compress", "NONE", "--output", str(OUT_C), str(png),
            ], check=True)
    if args.preview:
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.Resampling.NEAREST).save(args.preview)
        print(f"preview -> {args.preview}")


if __name__ == "__main__":
    main()
