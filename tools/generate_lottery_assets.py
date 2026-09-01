#!/usr/bin/env python3
"""Build the seven lottery reward images and the seven-segment wheel.

Source art and licenses:
- Burger base: Kenney, Pixel Platformer: Food Expansion, CC0 1.0.
  https://kenney.nl/assets/pixel-platformer-food-expansion
- Coin base: FacadeGaikan, collected by AntumDeluge on OpenGameArt, CC0 1.0.
  https://opengameart.org/content/cc0-currency-icons

The red-envelope, star badge, numerals, wheel sectors and pointer are original
pixel-level adaptations made for this firmware. They reuse the two CC0 assets'
compact palette and are distributed under the repository's license.
"""
from __future__ import annotations

import math
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main" / "assets"
OUT = ASSETS / "lottery"
BURGER_SOURCE = ASSETS / "lottery_source_burger_kenney_cc0.png"
COIN_SOURCE = ASSETS / "lottery_source_coin_oga_cc0.png"

REWARD_IDS = ("mcd", "cash20", "cash10", "cash2", "points10", "points5", "points2")
DIGITS = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "5": ("111", "100", "111", "001", "111"),
    "x": ("101", "101", "010", "101", "101"),
}


def trim_rgba(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    alpha = image.getchannel("A")
    box = alpha.getbbox()
    return image.crop(box) if box else image


def fit_pixel(image: Image.Image, width: int, height: int) -> Image.Image:
    image = trim_rgba(image)
    scale = min(width / image.width, height / image.height)
    size = (max(1, int(image.width * scale)), max(1, int(image.height * scale)))
    return image.resize(size, Image.Resampling.NEAREST)


def paste_center(canvas: Image.Image, image: Image.Image, y_offset: int = 0) -> None:
    canvas.alpha_composite(image, ((canvas.width - image.width) // 2, (canvas.height - image.height) // 2 + y_offset))


def bitmap_text(draw: ImageDraw.ImageDraw, text: str, x: int, y: int, scale: int, color: tuple[int, int, int, int]) -> None:
    cursor = x
    for char in text:
        glyph = DIGITS[char]
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    draw.rectangle((cursor + gx * scale, y + gy * scale,
                                    cursor + (gx + 1) * scale - 1, y + (gy + 1) * scale - 1), fill=color)
        cursor += 4 * scale


def red_envelope(value: str) -> Image.Image:
    image = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    outline, shadow, red, bright, gold = "#64253A", "#9B2945", "#D93A55", "#F05A66", "#FFD65A"
    d.rectangle((8, 8, 39, 41), fill=outline)
    d.rectangle((10, 8, 37, 39), fill=red)
    d.rectangle((10, 10, 37, 15), fill=bright)
    d.polygon(((10, 16), (23, 27), (37, 16), (37, 21), (23, 32), (10, 21)), fill=shadow)
    d.rectangle((18, 22, 28, 32), fill=gold)
    d.rectangle((20, 24, 26, 30), fill="#B52E45")
    text_w = (len(value) * 4 - 1) * 2
    bitmap_text(d, value, (48 - text_w) // 2, 34, 2, (255, 239, 184, 255))
    return image


def coin_badge(value: str, star: bool) -> Image.Image:
    source = Image.open(COIN_SOURCE).convert("RGBA")
    coin = fit_pixel(source, 36, 36)
    image = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    paste_center(image, coin, -4)
    d = ImageDraw.Draw(image)
    if star:
        cx, cy, r1, r2 = 24, 20, 8, 3
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            r = r1 if i % 2 == 0 else r2
            pts.append((round(cx + math.cos(a) * r), round(cy + math.sin(a) * r)))
        d.polygon(pts, fill="#FFF2A8", outline="#B97A22")
    text = "x" + value
    text_w = (len(text) * 4 - 1) * 2
    d.rectangle(((48 - text_w) // 2 - 2, 34, (48 + text_w) // 2 + 1, 46), fill="#17263B")
    bitmap_text(d, text, (48 - text_w) // 2, 35, 2, (255, 214, 90, 255))
    return image


def build_icons() -> dict[str, Image.Image]:
    burger = fit_pixel(Image.open(BURGER_SOURCE), 42, 42)
    mcd = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    paste_center(mcd, burger)
    return {
        "mcd": mcd,
        "cash20": red_envelope("20"),
        "cash10": red_envelope("10"),
        "cash2": coin_badge("2", False),
        "points10": coin_badge("10", True),
        "points5": coin_badge("5", True),
        "points2": coin_badge("2", True),
    }


def build_wheel(icons: dict[str, Image.Image]) -> Image.Image:
    logical = 88
    wheel = Image.new("RGBA", (logical, logical), (0, 0, 0, 0))
    d = ImageDraw.Draw(wheel)
    colors = ("#173554", "#24496B", "#2B3859", "#1D4960", "#303F68", "#17445A", "#26395B")
    bbox = (3, 3, logical - 4, logical - 4)
    sector = 360 / 7
    for i, color in enumerate(colors):
        start = -90 - sector / 2 + i * sector
        d.pieslice(bbox, start=start, end=start + sector, fill=color, outline="#6BB9FF", width=1)
    d.ellipse(bbox, outline="#FFD65A", width=2)
    d.ellipse((38, 38, 49, 49), fill="#FFD65A", outline="#7A4D16", width=1)
    for i, reward_id in enumerate(REWARD_IDS):
        angle = math.radians(-90 + i * sector)
        icon = fit_pixel(icons[reward_id], 18, 18)
        cx = round(logical / 2 + math.cos(angle) * 28)
        cy = round(logical / 2 + math.sin(angle) * 28)
        wheel.alpha_composite(icon, (cx - icon.width // 2, cy - icon.height // 2))
    return wheel.resize((176, 176), Image.Resampling.NEAREST)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    icons = build_icons()
    for reward_id, image in icons.items():
        image.save(OUT / f"lottery_{reward_id}.png", optimize=True)
    build_wheel(icons).save(OUT / "lottery_wheel.png", optimize=True)
    print(f"generated {len(icons)} reward icons and wheel in {OUT}")


if __name__ == "__main__":
    main()
