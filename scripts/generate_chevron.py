#!/usr/bin/env python3
"""
Generate a modern chevron-down PNG for the QComboBox down-arrow.

The Pastel Pop HTML design uses a clean SVG chevron:
    <svg viewBox='0 0 24 24' stroke='%238d93b2' stroke-width='2.4'
         stroke-linecap='round' stroke-linejoin='round'>
      <path d='m6 9 6 6 6-6'/>
    </svg>

We render that path on a transparent background with Pillow's anti-aliased
line drawing. Two PNGs are produced (one with the muted #8d93b2 stroke,
one with a stronger #4a5070 stroke for hover/disabled contexts) and both
are added to app.qrc under :/icons/chevron-down.png and :/icons/chevron-down-strong.png.
"""

import os
import sys

from PIL import Image, ImageDraw


SCALE = 4
SRC_SIZE = 24
RGBA_SIZE = SRC_SIZE * SCALE
FINAL_SIZE = 24

MUTED = (0x8d, 0x93, 0xb2, 255)
STRONG = (0x4a, 0x50, 0x70, 255)


def render_chevron(stroke_rgba):
    img = Image.new("RGBA", (RGBA_SIZE, RGBA_SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    pts = [(6 * SCALE, 9 * SCALE),
           (12 * SCALE, 15 * SCALE),
           (18 * SCALE, 9 * SCALE)]
    width = int(round(2.4 * SCALE))

    for i in range(len(pts) - 1):
        d.line([pts[i], pts[i + 1]], fill=stroke_rgba, width=width)
        d.ellipse([pts[i][0] - width // 2, pts[i][1] - width // 2,
                   pts[i][0] + width // 2, pts[i][1] + width // 2],
                  fill=stroke_rgba)

    final = img.resize((FINAL_SIZE, FINAL_SIZE), Image.LANCZOS)
    return final


def main():
    print("Generating modern chevron-down PNG...")
    out_dir = "/home/z/my-project/resources/icons"
    os.makedirs(out_dir, exist_ok=True)

    muted_path = os.path.join(out_dir, "chevron-down.png")
    strong_path = os.path.join(out_dir, "chevron-down-strong.png")

    render_chevron(MUTED).save(muted_path, "PNG")
    render_chevron(STRONG).save(strong_path, "PNG")

    print(f"  -> {muted_path}  ({os.path.getsize(muted_path)} bytes)")
    print(f"  -> {strong_path}  ({os.path.getsize(strong_path)} bytes)")
    print("OK - chevron PNG generated.")


if __name__ == "__main__":
    main()
