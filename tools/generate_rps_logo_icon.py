#!/usr/bin/env python3
"""Rebuild the 石头剪刀布 menu icon (logo_06_rock_paper_scissors[_m]).

Why this script exists
----------------------
The original design export `06_rock_paper_scissors.png` bakes a dark navy
disc into the artwork *and* marks that disc as opaque in its alpha channel.
Its alpha is a plain filled circle, so no alpha-based background removal can
ever drop the disc, and colour keying / grabCut are explicitly forbidden
(they eat the line art). The icon is therefore rebuilt from assets that
already carry a real per-pixel alpha channel: the hand sprites in
`main/assets/rps_gestures/`, which are drawn programmatically on a fully
transparent canvas.

Rules honoured here:
  * transparency comes exclusively from the source PNG alpha channel;
  * no cv2.grabCut, no chroma key, no colour-distance background removal;
  * RGB values are never inspected, only the alpha channel is read (for the
    tight crop) and alpha compositing is used for placement.
"""
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
GESTURES = ROOT / "main/assets/rps_gestures"
DESIGN = ROOT / "main/assets/logo_design"
CSRC = ROOT / "main/assets/logo_argb8888"

VARIANTS = {"": 36, "_m": 40}


def load_cropped(name: str) -> Image.Image:
    """Load a gesture sprite and crop it to its alpha bounding box."""
    image = Image.open(GESTURES / f"rps_gesture_{name}.png").convert("RGBA")
    alpha = np.asarray(image.getchannel("A"))
    if alpha.max() == 0:
        raise RuntimeError(f"{name}: source sprite has no opaque pixels")
    ys, xs = np.where(alpha > 0)
    return image.crop((int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1))


def resize_rgba(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Area-average on premultiplied alpha so edges keep clean transparency."""
    data = np.asarray(image).astype(np.float32)
    alpha = data[:, :, 3:4] / 255.0
    premultiplied = np.dstack((data[:, :, :3] * alpha, data[:, :, 3:4]))
    small = np.asarray(
        Image.fromarray(premultiplied.astype(np.uint8), "RGBA").resize(size, Image.Resampling.BOX)
    ).astype(np.float32)
    out_alpha = small[:, :, 3:4]
    rgb = np.where(out_alpha > 0, small[:, :, :3] / np.maximum(out_alpha / 255.0, 1e-6), 0)
    return Image.fromarray(np.dstack((np.clip(rgb, 0, 255), out_alpha)).astype(np.uint8), "RGBA")


def scale_to_height(image: Image.Image, height: int) -> Image.Image:
    width = max(1, round(image.width * height / image.height))
    return resize_rgba(image, (width, height))


def build_icon(edge: int) -> Image.Image:
    """Rock (bottom-left) versus scissors (top-right) on a transparent canvas."""
    canvas = Image.new("RGBA", (edge, edge), (0, 0, 0, 0))
    hand_height = round(edge * 0.56)
    rock = scale_to_height(load_cropped("rock"), hand_height)
    scissors = scale_to_height(load_cropped("scissors"), hand_height)

    canvas.alpha_composite(rock, (0, edge - rock.height))
    canvas.alpha_composite(scissors, (edge - scissors.width, 0))

    alpha = np.asarray(canvas.getchannel("A"))
    if alpha[0, 0] or alpha[-1, -1]:
        raise RuntimeError("icon corners must stay transparent")
    return canvas


def emit_c(image: Image.Image, symbol: str) -> str:
    data = np.asarray(image).astype(np.uint8)
    height, width = data.shape[:2]
    # LVGL ARGB8888 is little-endian: bytes are stored B, G, R, A.
    ordered = data[:, :, [2, 1, 0, 3]]
    # Fully transparent pixels carry no colour, keep them as all zero bytes.
    ordered = np.where(ordered[:, :, 3:4] == 0, 0, ordered).astype(np.uint8)
    rows = [
        "    " + ",".join(f"0x{value:02x}" for value in ordered[row].reshape(-1)) + ","
        for row in range(height)
    ]
    guard = f"LV_ATTRIBUTE_{symbol.upper()}"
    return f"""#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)
#include <lvgl.h>
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef {guard}
#define {guard}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {guard}
uint8_t {symbol}_map[] = {{

{chr(10).join(rows)}

}};

const lv_image_dsc_t {symbol} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_ARGB8888,
    .flags = 0,
    .w = {width},
    .h = {height},
    .stride = {width * 4},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({symbol}_map),
  .data = {symbol}_map,
  .reserved = NULL,
}};
"""


def main() -> None:
    for suffix, edge in VARIANTS.items():
        icon = build_icon(edge)
        design_path = DESIGN / f"06_rock_paper_scissors{suffix}.png"
        icon.save(design_path, optimize=True)
        symbol = f"logo_06_rock_paper_scissors{suffix}"
        (CSRC / f"{symbol}.c").write_text(emit_c(icon, symbol), encoding="utf-8")
        opaque = int((np.asarray(icon.getchannel("A")) > 0).sum())
        print(f"{symbol}: {edge}x{edge} opaque={opaque}/{edge * edge} "
              f"transparent={100 * (1 - opaque / edge ** 2):.1f}% -> {design_path.name}")


if __name__ == "__main__":
    main()
