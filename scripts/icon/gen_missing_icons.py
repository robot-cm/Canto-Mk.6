#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file gen_missing_icons.py
@brief Generate the 3 missing app icon design sources (webp, 512px, transparent)
       for the local apps that had no entry in the icon/ folder yet:
         alarm.webp  - alarm clock (orange bells + blue face)
         clock.webp  - wall clock  (blue outline + hands + ticks)
         notes.webp  - notepad     (green header + gray lines)
       Style matches the rest of icon/*.webp: transparent BG, thick flat ink
       strokes (#2F80ED family) + a single bright accent, no gradients.

Run AFTER renaming worldtime.webp -> globaltime.webp (the world-clock design
source for the globaltime app). Then run sync_app_icons.py to push to icon.bin.
"""

import os
from PIL import Image, ImageDraw

BASE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(BASE, "..", ".."))
ICON_DIR = os.path.join(REPO, "icon")

S = 512
INK = (47, 128, 237)       # #2F80ED blue ink
DARK = (52, 73, 94)        # #34495E dark hands
ORANGE = (242, 153, 74)    # #F2994A
GREEN = (39, 174, 96)      # #27AE60
GRAY = (154, 160, 166)     # #9AA0A6 lines
PAGE = (237, 239, 242)     # #EDEFF2 page


def new_canvas():
    return Image.new("RGBA", (S, S), (0, 0, 0, 0))


def gen_clock():
    im = new_canvas(); d = ImageDraw.Draw(im)
    cx, cy, r = 256, 256, 172
    # 4 quarter ticks
    for ang in (90, 180, 270, 0):
        import math
        a = math.radians(ang)
        x1 = cx + (r - 26) * math.cos(a); y1 = cy - (r - 26) * math.sin(a)
        x2 = cx + (r - 6) * math.cos(a);  y2 = cy - (r - 6) * math.sin(a)
        d.line([(x1, y1), (x2, y2)], fill=INK, width=16)
    # face outline
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=INK, width=30)
    # hands (10:10 classic)
    import math
    ha = math.radians(60);  hx = cx + 92 * math.cos(ha);  hy = cy - 92 * math.sin(ha)
    ma = math.radians(300); mx = cx + 140 * math.cos(ma); my = cy - 140 * math.sin(ma)
    d.line([(cx, cy), (hx, hy)], fill=DARK, width=30)
    d.line([(cx, cy), (mx, my)], fill=INK, width=22)
    d.ellipse([cx - 20, cy - 20, cx + 20, cy + 20], fill=INK)
    return im


def gen_alarm():
    im = new_canvas(); d = ImageDraw.Draw(im)
    # bells
    d.ellipse([96, 110, 236, 250], fill=ORANGE)
    d.ellipse([276, 110, 416, 250], fill=ORANGE)
    # hammer
    d.line([(256, 120), (256, 96)], fill=DARK, width=14)
    d.ellipse([244, 82, 268, 106], fill=DARK)
    # face
    cx, cy, r = 256, 270, 128
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=PAGE, outline=INK, width=16)
    # hands (10:10)
    import math
    ha = math.radians(60);  hx = cx + 64 * math.cos(ha);  hy = cy - 64 * math.sin(ha)
    ma = math.radians(300); mx = cx + 96 * math.cos(ma);  my = cy - 96 * math.sin(ma)
    d.line([(cx, cy), (hx, hy)], fill=DARK, width=18)
    d.line([(cx, cy), (mx, my)], fill=INK, width=14)
    d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=INK)
    # legs
    d.line([(cx - 56, cy + r - 14), (cx - 96, cy + r + 56)], fill=INK, width=18)
    d.line([(cx + 56, cy + r - 14), (cx + 96, cy + r + 56)], fill=INK, width=18)
    return im


def gen_notes():
    im = new_canvas(); d = ImageDraw.Draw(im)
    # page
    d.rounded_rectangle([96, 96, 416, 432], radius=28, fill=PAGE, outline=(199, 205, 212), width=6)
    # header bar
    d.rounded_rectangle([128, 120, 384, 186], radius=18, fill=GREEN)
    # text lines
    for i, y in enumerate((232, 282, 332, 382)):
        d.rounded_rectangle([128, y, 384, y + 14], radius=7, fill=GRAY)
    return im


def main():
    outs = {
        "alarm.webp": gen_alarm(),
        "clock.webp": gen_clock(),
        "notes.webp": gen_notes(),
    }
    for name, im in outs.items():
        p = os.path.join(ICON_DIR, name)
        im.save(p, "WEBP", lossless=True)
        print("wrote", p, im.size)
    # map worldtime -> globaltime (app dir is globaltime)
    src = os.path.join(ICON_DIR, "worldtime.webp")
    dst = os.path.join(ICON_DIR, "globaltime.webp")
    if os.path.exists(src) and not os.path.exists(dst):
        import shutil
        shutil.copy(src, dst)
        print("copied worldtime.webp -> globaltime.webp (app dir is globaltime)")
        os.remove(src)
        print("removed stale worldtime.webp")
    print("done")


if __name__ == "__main__":
    main()
