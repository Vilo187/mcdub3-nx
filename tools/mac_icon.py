#!/usr/bin/env python3
"""A macOS app icon from a square picture (needs Pillow).

  mac_icon.py <picture> <out.png>

Apple's layout for a Big Sur-style icon: a 1024x1024 canvas, the artwork in
an 824x824 rounded square centred on it (100 px margin), corners as a
superellipse (the "squircle", not a plain circular arc), and a soft shadow
under it. The result goes through iconutil (tools/mac_app.sh).
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter

CANVAS, BODY, MARGIN = 1024, 824, 100
SS = 4                                   # supersampling for smooth edges


def squircle_mask(size, n=5.0):
    """A superellipse |x|^n + |y|^n <= 1, antialiased by supersampling."""
    big = size * SS
    mask = Image.new("L", (big, big), 0)
    px = mask.load()
    r = big / 2.0
    for y in range(big):
        dy = abs((y + 0.5 - r) / r) ** n
        if dy >= 1.0:
            continue
        # Solve |x|^n = 1 - |y|^n for the row's half-width.
        half = (1.0 - dy) ** (1.0 / n) * r
        x0, x1 = int(r - half), int(r + half)
        for x in range(max(0, x0), min(big, x1 + 1)):
            px[x, y] = 255
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
