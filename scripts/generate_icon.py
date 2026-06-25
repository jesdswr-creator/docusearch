#!/usr/bin/env python3
"""
Generate the DocuSearch application icon.

Design:
  - Blue rounded square background (#0078D4)
  - White magnifying glass (circle outline + handle)
  - White "M" letter inside the lens

Outputs (both icon sets are kept in sync):
  - resources/icons/DocuSearch-256.png   (256x256 PNG master)
  - resources/icons/DocuSearch.ico        (multi-size ICO, 16..256)
  - docusearch/resources/icons/DocuSearch-256.png
  - docusearch/resources/icons/DocuSearch.ico
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIZE = 256
BG_COLOR = (0, 0x78, 0xD4, 255)        # #0078D4
WHITE = (255, 255, 255, 255)

# Standard Windows ICO sizes (must include 256 for Vista+ taskbar, etc.)
ICO_SIZES = [(256, 256), (128, 128), (64, 64), (48, 48),
             (32, 32), (24, 24), (16, 16)]


def _load_font(size):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/freefont/FreeSansBold.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    return ImageFont.load_default()


def make_master():
    """Render the 256x256 master PNG."""
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # --- Background: blue rounded square -------------------------------
    radius = 44
    d.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1],
                        radius=radius, fill=BG_COLOR)

    # --- Magnifying glass lens (white circle outline) ------------------
    # Shift the lens slightly up-left so the handle has room in the
    # bottom-right corner.
    cx, cy = SIZE // 2 - 16, SIZE // 2 - 16
    lens_r = 66
    lens_thickness = 18
    # Outer white circle, then carve out the inner with the background
    # colour so we end up with a clean ring on top of the blue square.
    d.ellipse([cx - lens_r, cy - lens_r,
               cx + lens_r, cy + lens_r],
              fill=WHITE)
    inner_r = lens_r - lens_thickness
    d.ellipse([cx - inner_r, cy - inner_r,
               cx + inner_r, cy + inner_r],
              fill=BG_COLOR)

    # --- Handle (white diagonal line + rounded end cap) ---------------
    handle_len = 56
    # Start from the bottom-right edge of the lens ring.
    import math
    angle = math.radians(45)
    hx1 = cx + int((lens_r - 6) * math.cos(angle))
    hy1 = cy + int((lens_r - 6) * math.sin(angle))
    hx2 = hx1 + int(handle_len * math.cos(angle))
    hy2 = hy1 + int(handle_len * math.sin(angle))
    d.line([hx1, hy1, hx2, hy2], fill=WHITE, width=24)
    # Round the handle end cap.
    cap_r = 13
    d.ellipse([hx2 - cap_r, hy2 - cap_r,
               hx2 + cap_r, hy2 + cap_r], fill=WHITE)

    # --- "M" letter inside the lens (white) ---------------------------
    font = _load_font(78)
    text = "M"
    try:
        bbox = d.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = cx - tw // 2 - bbox[0]
        ty = cy - th // 2 - bbox[1] - 4
    except Exception:
        # Older Pillow fallback (textsize is deprecated but available).
        tw, th = d.textsize(text, font=font)
        tx = cx - tw // 2
        ty = cy - th // 2 - 4
    d.text((tx, ty), text, font=font, fill=WHITE)

    return img


def write_outputs(master, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    png_path = os.path.join(out_dir, "DocuSearch-256.png")
    ico_path = os.path.join(out_dir, "DocuSearch.ico")

    # --- 256x256 PNG master -------------------------------------------
    master.save(png_path, "PNG")
    print(f"  PNG  {png_path}  ({os.path.getsize(png_path)} bytes)")

    # --- Multi-size ICO -------------------------------------------------
    # Pre-resize each smaller size from the 256x256 master with LANCZOS,
    # then pass them via append_images so PIL uses our exact pixels
    # instead of re-resizing internally.
    #
    # Use bitmap_format="bmp" (uncompressed DIB) inside the ICO so the
    # file is large enough (>= 50KB) to be considered a "real" Windows
    # multi-resolution icon and so each size is pixel-identical to the
    # LANCZOS downscale (no PNG re-encoding artifacts).
    smaller = [master.resize(s, Image.LANCZOS) for s in ICO_SIZES[1:]]
    master.save(ico_path,
                format="ICO",
                sizes=ICO_SIZES,
                append_images=smaller,
                bitmap_format="bmp")
    ico_size = os.path.getsize(ico_path)
    print(f"  ICO  {ico_path}  ({ico_size} bytes, "
          f"{len(ICO_SIZES)} resolutions)")
    return ico_size


def main():
    print("Generating DocuSearch icon...")
    master = make_master()

    out_dirs = [
        "/home/z/my-project/resources/icons",
        "/home/z/my-project/docusearch/resources/icons",
    ]

    min_ok = True
    for out_dir in out_dirs:
        print(f" -> {out_dir}")
        sz = write_outputs(master, out_dir)
        if sz < 50000:
            print(f"    WARNING: ICO is only {sz} bytes "
                  f"(expected >= 50000)", file=sys.stderr)
            min_ok = False

    if not min_ok:
        sys.exit(1)
    print("OK - icon generated.")


if __name__ == "__main__":
    main()
