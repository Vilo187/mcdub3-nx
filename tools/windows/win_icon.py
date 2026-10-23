#!/usr/bin/env python3
"""A Windows icon (.ico) from a picture (needs Pillow).

  win_icon.py <picture> <out.ico>

The picture's centre square, with the softly rounded corners of a
Windows 11 app icon (about 8% of the side), in the sizes Explorer, the
taskbar and the Start menu ask for: 16 to 256 px. tools/build_windows.sh
embeds it in mcdub3.exe (the desktop shortcut takes it from there).
"""
import sys
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SS = 4                                   # supersampling for smooth corners


def main(src, dst):
    im = Image.open(src).convert("RGBA")
    side = min(im.size)
    left, top = (im.width - side) // 2, (im.height - side) // 2
    im = im.crop((left, top, left + side, top + side)).resize((256, 256), Image.LANCZOS)
    big = 256 * SS
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=int(big * 0.08), fill=255)
    im.putalpha(mask.resize((256, 256), Image.LANCZOS))
    im.save(dst, format="ICO", sizes=[(s, s) for s in SIZES])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
