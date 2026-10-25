#!/usr/bin/env python3
"""An app icon from the game's artwork (config/<game>/app.env APP_ICON).

    tools/app_icon.py switch <artwork> <out.jpg>

switch: 256x256, a bare baseline JPEG (4:2:0, no EXIF, no progressive, no
ICC profile): what hbmenu and the home menu show. Anything more and the
home menu draws a '?' or nothing (NTON re-encodes the same way); hbmenu
ignores an icon that is not 256x256. Non-square artwork is cropped to the
middle square first.

(The Mac's rounded .icns is tools/mac_icon.py, Windows' .ico
tools/windows/win_icon.py.)
"""
import os
import sys

from PIL import Image


def square(im):
    w, h = im.size
    s = min(w, h)
    return im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))


def switch(src, dst):
    im = square(Image.open(src).convert("RGB"))
    if im.size != (256, 256):
        im = im.resize((256, 256), Image.LANCZOS)
    clean = Image.frombytes("RGB", im.size, im.tobytes())   # pixels only: no EXIF, no ICC
    clean.save(dst, format="JPEG", quality=98, subsampling=2)
    print("  icon: %s -> %s (256x256 baseline JPEG)" % (os.path.relpath(src), os.path.relpath(dst)))


def main():
    if len(sys.argv) != 4 or sys.argv[1] != "switch":
        sys.exit(__doc__)
    switch(sys.argv[2], sys.argv[3])


if __name__ == "__main__":
    main()
