#!/usr/bin/env python3
"""A macOS app icon from a square picture (needs Pillow).

  mac_icon.py <picture> <out.png>

Apple's layout for a Big Sur-style icon: a 1024x1024 canvas, the artwork in
an 824x824 rounded square centred on it (100 px margin), straight edges
and corners of radius 185 px, and a soft shadow under it. The result goes
through iconutil (tools/mac_app.sh).
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter

CANVAS, BODY, MARGIN = 1024, 824, 100
SS = 4                                   # supersampling for smooth edges


def squircle_mask(size):
    """Apple's app-icon shape: a square with straight edges and rounded
    corners, radius 22.37% of the side (185 of 824). A superellipse over the
    whole square, as before, has no straight stretch anywhere: the edges
    bulged next to stock icons (2026-10-23). Antialiased by supersampling."""
    big = size * SS
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1),
                                           radius=round(big * 0.2237), fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def main(src, dst):
    art = Image.open(src).convert("RGBA")
    w, h = art.size
    side = min(w, h)                     # centre crop to a square
    art = art.crop(((w - side) // 2, (h - side) // 2,
                    (w - side) // 2 + side, (h - side) // 2 + side))
    art = art.resize((BODY, BODY), Image.LANCZOS)
    mask = squircle_mask(BODY)
    art.putalpha(ImageChops.multiply(art.getchannel("A"), mask))

    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    shadow = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    sh = Image.new("RGBA", (BODY, BODY), (0, 0, 0, 90))
    sh.putalpha(ImageChops.multiply(sh.getchannel("A"), mask))
    shadow.paste(sh, (MARGIN, MARGIN + 12), sh)
    shadow = shadow.filter(ImageFilter.GaussianBlur(14))
    canvas = Image.alpha_composite(canvas, shadow)
    layer = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    layer.paste(art, (MARGIN, MARGIN), art)
    canvas = Image.alpha_composite(canvas, layer)
    canvas.save(dst)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
