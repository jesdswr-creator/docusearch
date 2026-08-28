#!/usr/bin/env python3
"""
Generate the compact DocuSearch splash pixmap.

History:
  v1.0  256x256 app icon on a dark stylesheet background -> "bleeding edges".
  v1.1  960x600 (shown 1:1) navy billboard card. Looked premium on paper but
        covered half of a 1080p laptop screen; users asked for it to shrink.
  v1.2  THIS script: same brand language, compact footprint.

Asset spec:
  - Canvas 1080x680 @2x, drawn over a fully transparent background
    (QSplashScreen sizes its window to the pixmap, so transparent margins
    mean no edge bleed against any desktop).
  - main.cpp applies setDevicePixelRatio(2.0) after loading, so the window
    renders at a tidy 540x340 LOGICAL pixels on every display: big enough
    for logo + wordmark + status line, never a half-screen billboard.
  - The "Starting..." status line is baked into the artwork near the bottom,
    inside the card, instead of being overlaid via showMessage() which would
    land in the transparent margin outside the rounded corners.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

W, H = 1080, 680                      # @2x canvas -> displays 540x340
MARGIN = 8                            # transparent breathing room
CARD = (MARGIN, MARGIN, W - MARGIN, H - MARGIN)
RADIUS = 36

# Brand palette (Midnight-family neutrals + indigo/violet accents).
NAVY_TOP = (16, 26, 46)      # #101a2e
NAVY_BOT = (24, 38, 66)      # #182642
INDIGO   = (77, 141, 246)    # #4d8df6
VIOLET   = (177, 138, 255)   # #b18aff
WHITE    = (255, 255, 255)
WHITE_85 = (232, 237, 245, 217)
WHITE_60 = (232, 237, 245, 153)

ICON_PATH = os.path.join(os.path.dirname(__file__),
                         "..", "resources", "icons", "DocuSearch-256.png")
OUT_PATH = os.path.join(os.path.dirname(__file__),
                        "..", "resources", "icons", "splash.png")


def _load_font(size, bold=False):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold
            else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold
            else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf" if bold
            else "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    return ImageFont.load_default()


def _vgrad(size, top, bot):
    """Vertical gradient RGBA image."""
    w, h = size
    g = Image.new("RGBA", size)
    px = g.load()
    for y in range(h):
        t = y / max(1, h - 1)
        r = int(top[0] + (bot[0] - top[0]) * t)
        gg = int(top[1] + (bot[1] - top[1]) * t)
        b = int(top[2] + (bot[2] - top[2]) * t)
        for x in range(w):
            px[x, y] = (r, gg, b, 255)
    return g


def _radial_glow(size, color, max_alpha):
    """Soft radial glow blob used for the ambient lighting."""
    w, h = size
    glow = Image.new("L", size, 0)
    d = ImageDraw.Draw(glow)
    cx, cy = w // 2, h // 2
    steps = 64
    for i in range(steps, 0, -1):
        t = i / steps
        a = int(max_alpha * (1.0 - t) ** 2)
        rx, ry = int(w / 2 * t), int(h / 2 * t)
        d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=a)
    layer = Image.new("RGBA", size, color + (0,))
    layer.putalpha(glow)
    return layer


def build():
    # OPAQUE WHITE base. The old fully-transparent canvas let the desktop
    # bleed through the 8px margin ring and the rounded corners — users
    # reported "transparent areas that should be white". The card still
    # reads as a rounded navy panel, now sitting on a clean white plate.
    img = Image.new("RGBA", (W, H), (255, 255, 255, 255))

    # Card body: vertical navy gradient.
    body = _vgrad((W - 2 * MARGIN, H - 2 * MARGIN), NAVY_TOP, NAVY_BOT)

    # Ambient glows (indigo top-right, violet bottom-left).
    glow_tr = _radial_glow(body.size, INDIGO, 70)
    glow_bl = _radial_glow(body.size, VIOLET, 48)
    body.alpha_composite(glow_tr, (body.size[0] // 3, -body.size[1] // 3))
    body.alpha_composite(glow_bl, (-body.size[0] // 4, body.size[1] // 3))

    # Rounded-corner mask.
    mask = Image.new("L", body.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, body.size[0] - 1, body.size[1] - 1), RADIUS, fill=255)
    img.paste(body, (MARGIN, MARGIN), mask)

    d = ImageDraw.Draw(img)
    card_cx = W // 2

    # ── App icon on a soft underlay ──────────────────────────────
    icon_size = 150
    underlay = 190
    uy_top = 84
    uy_cy = uy_top + underlay // 2
    d.rounded_rectangle(
        (card_cx - underlay // 2, uy_top, card_cx + underlay // 2,
         uy_top + underlay),
        radius=44, fill=(255, 255, 255, 18))
    d.rounded_rectangle(
        (card_cx - underlay // 2, uy_top, card_cx + underlay // 2,
         uy_top + underlay),
        radius=44, outline=(255, 255, 255, 34), width=2)

    if os.path.exists(ICON_PATH):
        icon = Image.open(ICON_PATH).convert("RGBA").resize(
            (icon_size, icon_size), Image.LANCZOS)
        img.alpha_composite(
            icon, (card_cx - icon_size // 2, uy_cy - icon_size // 2))
    else:
        # Fallback: draw a magnifying-glass glyph manually.
        r = 52
        lx, ly = card_cx - 12, uy_cy - 12
        d.ellipse((lx - r, ly - r, lx + r, ly + r),
                  outline=WHITE, width=10)
        ang = math.radians(45)
        hx, hy = lx + r * math.cos(ang) * 0.72, ly + r * math.sin(ang) * 0.72
        d.line((hx, hy, hx + 46, hy + 46), fill=WHITE, width=14)

    # ── Wordmark ─────────────────────────────────────────────────
    f_word = _load_font(74, bold=True)
    word = "DocuSearch"
    wb = d.textbbox((0, 0), word, font=f_word)
    wy = uy_top + underlay + 42
    d.text((card_cx - (wb[2] - wb[0]) // 2, wy), word,
           font=f_word, fill=WHITE)

    # ── Accent underline bar (indigo -> violet) ──────────────────
    bar_w, bar_h = 132, 10
    bar_x0, bar_y0 = card_cx - bar_w // 2, wy + (wb[3] - wb[1]) + 30
    for x in range(bar_w):
        t = x / max(1, bar_w - 1)
        c = tuple(int(INDIGO[i] + (VIOLET[i] - INDIGO[i]) * t)
                  for i in range(3))
        d.rectangle((bar_x0 + x, bar_y0, bar_x0 + x + 1, bar_y0 + bar_h),
                    fill=c + (255,))
    # round the ends
    d.ellipse((bar_x0, bar_y0, bar_x0 + bar_h, bar_y0 + bar_h), fill=INDIGO)
    d.ellipse((bar_x0 + bar_w - bar_h, bar_y0,
               bar_x0 + bar_w, bar_y0 + bar_h), fill=VIOLET)

    # ── Tagline ──────────────────────────────────────────────────
    f_tag = _load_font(31)
    tagline = "Find anything. Instantly. Offline."
    tb = d.textbbox((0, 0), tagline, font=f_tag)
    d.text((card_cx - (tb[2] - tb[0]) // 2, bar_y0 + bar_h + 26), tagline,
           font=f_tag, fill=WHITE_60)

    # ── Baked status line (replaces splash.showMessage overlay) ──
    f_status = _load_font(27)
    status = "Preparing your library..."
    sb = d.textbbox((0, 0), status, font=f_status)
    sy = CARD[3] - 58 - (sb[3] - sb[1])
    d.text((card_cx - (sb[2] - sb[0]) // 2, sy), status,
           font=f_status, fill=WHITE_85)

    # Flatten to RGB — the splash must be fully opaque; any residual
    # alpha would re-introduce see-through pixels on odd compositors.
    img.convert("RGB").save(OUT_PATH)
    print(f"Wrote {os.path.abspath(OUT_PATH)} ({W}x{H} @2x "
          f"-> displays {W//2}x{H//2} logical)")


if __name__ == "__main__":
    build()
