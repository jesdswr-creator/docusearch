#!/usr/bin/env python3
"""
Generate a clean splash pixmap for DocuSearch.

Problem: The old splash used DocuSearch-256.png (256x256 blue rounded square)
shown via QSplashScreen with a dark stylesheet background. The result was
"bleeding edges" — the dark window background showed around the icon's
rounded corners.

Solution: build a wider, taller transparent pixmap with:
  - Transparent background (no bleed)
  - A large rounded card in pastel primary (lavender #6c7cf5)
  - A white inner circle
  - The DocuSearch icon (white search glyph) inside the circle
  - "DocuSearch" wordmark in white below
  - "Offline Document Search" subtitle in white/80%

The QSplashScreen sizes its window to the pixmap, so a transparent pixmap
means no edge bleed.
"""

import os

from PIL import Image, ImageDraw, ImageFont


W, H = 440, 280
CARD_W, CARD_H = 440, 280
CARD_RADIUS = 28

# Pastel lavender primary (matches the default theme).
CARD_BG = (0x6c, 0x7c, 0xf5, 255)
WHITE = (255, 255, 255, 255)
WHITE_80 = (255, 255, 255, 204)   # 80% opacity
WHITE_50 = (255, 255, 255, 128)

CIRCLE_R = 56


def _load_font(size, bold=False):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf" if bold else "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    return ImageFont.load_default()


def _draw_search_icon(d, cx, cy, r, stroke_rgba, stroke_w):
    """Draw a Lucide-style magnifying glass: circle outline + diagonal handle."""
    import math
    # Lens circle (slightly up-left so the handle fits in bottom-right).
    lens_cx = cx - int(r * 0.18)
    lens_cy = cy - int(r * 0.18)
    lens_r = int(r * 0.55)

    # Outer ring
    bbox = [lens_cx - lens_r, lens_cy - lens_r,
            lens_cx + lens_r, lens_cy + lens_r]
    # Draw a thick circle outline by drawing two filled circles and differencing.
    # Pillow doesn't have stroke for ellipse directly, so use two passes:
    # (a) draw filled outer circle in stroke color,
    # (b) draw filled inner circle in CARD_BG to leave a ring.
    d.ellipse(bbox, fill=stroke_rgba)
    inner_pad = stroke_w
    d.ellipse([bbox[0] + inner_pad, bbox[1] + inner_pad,
               bbox[2] - inner_pad, bbox[3] - inner_pad], fill=CARD_BG)

    # Handle (diagonal line + rounded cap)
    angle = math.radians(45)
    hx1 = lens_cx + int((lens_r - 2) * math.cos(angle))
    hy1 = lens_cy + int((lens_r - 2) * math.sin(angle))
    handle_len = int(r * 0.45)
    hx2 = hx1 + int(handle_len * math.cos(angle))
    hy2 = hy1 + int(handle_len * math.sin(angle))
    d.line([hx1, hy1, hx2, hy2], fill=stroke_rgba, width=stroke_w)
    cap_r = stroke_w // 2 + 1
    d.ellipse([hx2 - cap_r, hy2 - cap_r, hx2 + cap_r, hy2 + cap_r], fill=stroke_rgba)


def make_splash():
    """Render the splash pixmap."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 1. Rounded card background
    d.rounded_rectangle([0, 0, W - 1, H - 1],
                        radius=CARD_RADIUS, fill=CARD_BG)

    # 2. White inner circle (top area)
    circle_cx = W // 2
    circle_cy = 90
    d.ellipse([circle_cx - CIRCLE_R, circle_cy - CIRCLE_R,
               circle_cx + CIRCLE_R, circle_cy + CIRCLE_R], fill=WHITE)

    # 3. Search icon inside the white circle (drawn in CARD_BG color
    #    so it's visible against white).
    _draw_search_icon(d, circle_cx, circle_cy, CIRCLE_R - 12,
                      stroke_rgba=CARD_BG, stroke_w=7)

    # 4. "DocuSearch" wordmark
    title_font = _load_font(30, bold=True)
    title = "DocuSearch"
    try:
        bbox = d.textbbox((0, 0), title, font=title_font)
        tw = bbox[2] - bbox[0]
    except Exception:
        tw = 200
    tx = (W - tw) // 2
    ty = 175
    d.text((tx, ty), title, font=title_font, fill=WHITE)

    # 5. Subtitle
    sub_font = _load_font(14, bold=False)
    sub = "Offline Document Search"
    try:
        bbox = d.textbbox((0, 0), sub, font=sub_font)
        sw = bbox[2] - bbox[0]
    except Exception:
        sw = 200
    sx = (W - sw) // 2
    sy = ty + 38
    d.text((sx, sy), sub, font=sub_font, fill=WHITE_80)

    return img


def main():
    print("Generating DocuSearch splash pixmap...")
    img = make_splash()
    out_dir = "/home/z/my-project/resources/icons"
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "splash.png")
    img.save(out_path, "PNG")
    print(f"  -> {out_path}  ({os.path.getsize(out_path)} bytes, {img.size[0]}x{img.size[1]})")
    print("OK - splash generated.")


if __name__ == "__main__":
    main()
