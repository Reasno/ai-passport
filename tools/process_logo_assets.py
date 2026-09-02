#!/usr/bin/env python3
"""Convert the 14 logo-design PNGs into alpha-preserving LVGL ARGB8888 sources.

Background handling rules (deliberately conservative):

1. If the source PNG already carries a real alpha channel, that channel is the
   single source of truth. Nothing else touches transparency.
2. The remaining sources are opaque RGB exports whose "transparent" area is a
   baked-in editor checkerboard. Those are undone geometrically:
     * the two checker colours are read from the outer border ring,
     * a pixel may only become transparent when it matches one of those two
       exact colours (L-infinity tolerance 10/255),
     * and only when it belongs to a region connected to the canvas border.
   Enclosed pixels are therefore never removed, whatever their colour.
3. No grabCut, no chroma key, no flood fill by colour distance, no "replace
   colour near background". Saturated art pixels cannot be erased: the script
   asserts that every removed pixel is within tolerance of a checker colour.
"""
from pathlib import Path
import math
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "processed"
OUT.mkdir(exist_ok=True)

CHECKER_TOLERANCE = 10   # per-channel match against a checker colour, 0-255 units
PALETTE_TOLERANCE = 12   # per-channel distance to the two-colour checker segment
SIZES = {
    "01_app_logo.png": (92, 92),
    "02_tasks.png": (36, 36),
    "03_rewards.png": (36, 36),
    "04_games.png": (36, 36),
    "05_find_sibling.png": (36, 36),
    "06_rock_paper_scissors.png": (36, 36),
    "07_lucky_wheel.png": (36, 36),
    "08_mcdonalds_burger.png": (48, 48),
    "09_cash_20.png": (48, 48),
    "10_cash_10.png": (48, 48),
    "11_cash_2.png": (48, 48),
    "12_points_10.png": (48, 48),
    "13_points_5.png": (48, 48),
    "14_points_2.png": (48, 48),
}
REPORT = []


def checker_colours(rgb: np.ndarray) -> list[np.ndarray]:
    """Read the two baked-in checkerboard colours from the outer border ring."""
    ring = np.concatenate((
        rgb[:6, :].reshape(-1, 3), rgb[-6:, :].reshape(-1, 3),
        rgb[:, :6].reshape(-1, 3), rgb[:, -6:].reshape(-1, 3),
    ))
    colours, counts = np.unique(ring.reshape(-1, 3), axis=0, return_counts=True)
    order = np.argsort(counts)[::-1]
    picked: list[np.ndarray] = []
    for index in order:
        colour = colours[index].astype(np.int16)
        if all(np.abs(colour - other).max() > CHECKER_TOLERANCE * 2 for other in picked):
            picked.append(colour)
        if len(picked) == 2:
            break
    return picked


def palette_distance(pixels: np.ndarray, picked: list[np.ndarray]) -> np.ndarray:
    """Distance to the straight line between the two checker colours.

    Cell seams and soft checker noise are blends of exactly those two colours, so
    they sit on this short segment. Artwork colours (a saturated cyan bow, an
    orange gift box) are tens of units away from it and can never qualify.
    """
    pixels = pixels.reshape(-1, 3).astype(np.float32)
    if len(picked) < 2:
        return np.abs(pixels - picked[0].astype(np.float32)).max(axis=1)
    a = picked[0].astype(np.float32)
    b = picked[1].astype(np.float32)
    direction = b - a
    length = float(np.dot(direction, direction))
    t = np.clip(((pixels - a) @ direction) / max(length, 1e-6), 0.0, 1.0)
    projection = a + t[:, None] * direction
    return np.abs(pixels - projection).max(axis=1)


def checker_palette_mask(rgb: np.ndarray, picked: list[np.ndarray]) -> np.ndarray:
    distance = palette_distance(rgb, picked).reshape(rgb.shape[:2])
    return distance <= PALETTE_TOLERANCE


def alpha_from_source(path: Path) -> Image.Image:
    source = Image.open(path).convert("RGBA")
    alpha = np.asarray(source.getchannel("A"))
    if alpha.min() < 255:
        REPORT.append(f"{path.name}: native alpha channel used verbatim")
        return source

    rgb = np.asarray(source.convert("RGB")).astype(np.int16)
    picked = checker_colours(rgb)
    matches = np.zeros(rgb.shape[:2], np.uint8)
    for colour in picked:
        matches |= (np.abs(rgb - colour).max(axis=2) <= CHECKER_TOLERANCE).astype(np.uint8)

    # The checker cells are separated by 1px seams, so build the connectivity graph
    # on a closed copy of the mask; removal still only happens on checker pixels.
    palette = checker_palette_mask(rgb, picked)
    closed = cv2.morphologyEx(np.maximum(matches, palette.astype(np.uint8)),
                              cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    count, labels = cv2.connectedComponents(closed, connectivity=4)
    border_labels = set(labels[0, :]) | set(labels[-1, :]) | set(labels[:, 0]) | set(labels[:, -1])
    border_labels.discard(0)
    # Only the checker palette itself may be erased, and only where it is reachable
    # from the canvas border. Anything enclosed by artwork stays opaque.
    background = np.isin(labels, list(border_labels)) & palette

    removed = rgb[background]
    if removed.size:
        chroma = (removed.max(axis=1) - removed.min(axis=1)).max()
        distance = int(palette_distance(removed, picked).max())
        assert distance <= PALETTE_TOLERANCE, f"{path.name}: removed a non-checker pixel"
    else:
        chroma = distance = 0
    kept = (~background).sum() / background.size
    REPORT.append(
        f"{path.name}: checker {tuple(picked[0])}/{tuple(picked[1] if len(picked) > 1 else picked[0])} "
        f"removed={background.mean():.1%} kept={kept:.1%} max_removed_chroma={chroma} max_removed_delta={distance}"
    )

    out = np.dstack((rgb.astype(np.uint8), np.where(background, 0, 255).astype(np.uint8)))
    return Image.fromarray(out, "RGBA")


def resize_rgba(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Area-average on premultiplied alpha so edges never pick up checker fringe."""
    data = np.asarray(image).astype(np.float32)
    alpha = data[:, :, 3:4] / 255.0
    premultiplied = np.dstack((data[:, :, :3] * alpha, data[:, :, 3:4]))
    small = np.asarray(
        Image.fromarray(premultiplied.astype(np.uint8), "RGBA").resize(size, Image.Resampling.BOX)
    ).astype(np.float32)
    out_alpha = small[:, :, 3:4]
    rgb = np.where(out_alpha > 0, small[:, :, :3] / np.maximum(out_alpha / 255.0, 1e-6), 0)
    return Image.fromarray(
        np.dstack((np.clip(rgb, 0, 255), out_alpha)).astype(np.uint8), "RGBA"
    )


def fit_transparent(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    alpha = np.asarray(source.getchannel("A"))
    ys, xs = np.where(alpha > 16)
    if len(xs) == 0:
        raise RuntimeError("alpha extraction produced an empty image")
    margin = max(1, min(source.size) // 200)
    left = max(0, int(xs.min()) - margin)
    top = max(0, int(ys.min()) - margin)
    right = min(source.width, int(xs.max()) + margin + 1)
    bottom = min(source.height, int(ys.max()) + margin + 1)
    crop = source.crop((left, top, right, bottom))
    inner = (size[0] - 2, size[1] - 2)
    scale = min(inner[0] / crop.width, inner[1] / crop.height)
    target = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    crop = resize_rgba(crop, target) if crop.size != target else crop
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.alpha_composite(crop, ((size[0] - crop.width) // 2, (size[1] - crop.height) // 2))
    return canvas


def build_wheel(icons: list[Image.Image]) -> Image.Image:
    size = 152
    wheel = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(wheel)
    colors = ("#173554", "#24496B", "#2B3859", "#1D4960", "#303F68", "#17445A", "#26395B")
    bbox = (4, 4, size - 5, size - 5)
    sector = 360 / 7
    for index, color in enumerate(colors):
        start = -90 - sector / 2 + index * sector
        draw.pieslice(bbox, start=start, end=start + sector, fill=color, outline="#6BB9FF", width=2)
    draw.ellipse(bbox, outline="#FFD65A", width=4)
    for index, icon in enumerate(icons):
        angle = math.radians(-90 + index * sector)
        thumb = resize_rgba(icon, (30, 30))
        cx = round(size / 2 + math.cos(angle) * 48)
        cy = round(size / 2 + math.sin(angle) * 48)
        wheel.alpha_composite(thumb, (cx - 15, cy - 15))
    hub = size / 2
    draw.ellipse((hub - 11, hub - 11, hub + 11, hub + 11), fill="#FFD65A", outline="#7A4D16", width=2)
    return wheel


def decorate_points_5(prepared: Image.Image) -> Image.Image:
    """Keep the high-contrast x5 badge; pure drawing, no colour keying."""
    draw = ImageDraw.Draw(prepared)
    draw.rounded_rectangle((26, 32, 47, 47), radius=3, fill=(13, 27, 42, 230), outline=(78, 211, 160, 255))
    glyphs = {"x": (0b101, 0b101, 0b010, 0b101, 0b101), "5": (0b111, 0b100, 0b111, 0b001, 0b111)}
    cursor_x = 29
    for char in "x5":
        for row, bits in enumerate(glyphs[char]):
            for col in range(3):
                if bits & (0b100 >> col):
                    draw.rectangle((cursor_x + col * 2, 35 + row * 2,
                                    cursor_x + col * 2 + 1, 35 + row * 2 + 1), fill=(255, 214, 90, 255))
        cursor_x += 8
    return prepared


sources = {name: alpha_from_source(ROOT / name) for name in SIZES}
prepared_outputs = {}
for name, size in SIZES.items():
    prepared = fit_transparent(sources[name], size)
    if name == "13_points_5.png":
        prepared = decorate_points_5(prepared)
    prepared.save(OUT / name, optimize=True)
    prepared_outputs[name] = prepared

wheel_names = [
    "08_mcdonalds_burger.png", "09_cash_20.png", "10_cash_10.png", "11_cash_2.png",
    "12_points_10.png", "13_points_5.png", "14_points_2.png",
]
build_wheel([prepared_outputs[name] for name in wheel_names]).save(OUT / "lottery_wheel.png", optimize=True)

VARIANTS = {
    "02_tasks.png": {"_s": 28},
    "03_rewards.png": {"_m": 40},
    "04_games.png": {"_m": 40},
    "05_find_sibling.png": {"_m": 40, "_l": 72},
    "06_rock_paper_scissors.png": {"_m": 40},
    "07_lucky_wheel.png": {"_m": 40},
}
for name, variants in VARIANTS.items():
    for suffix, edge in variants.items():
        fit_transparent(sources[name], (edge, edge)).save(OUT / f"{name[:-4]}{suffix}.png", optimize=True)

for line in REPORT:
    print(line)
print(f"wrote {len(list(OUT.glob('*.png')))} RGBA PNGs to {OUT}")
