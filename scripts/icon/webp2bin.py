#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file webp2bin.py
@brief Convert webp icons into LVGL binary image files (*.bin) for the SD card.

The firmware reads system icons from the SD card at
    /sdcard/.sys/res/img/<name>.bin   (LVGL FS path: /.sys/res/img/<name>.bin)
through the LVGL bin decoder. The file format is:

    lv_image_header_t (12 bytes, little-endian):
        magic(1)=0x19 | cf(1)=LV_COLOR_FORMAT_ARGB8888(0x10)
        flags(2)=0 | w(2) | h(2) | stride(2)=w*4 | reserved(3)=0
    then raw ARGB8888 pixels, little-endian byte order [B, G, R, A].

Usage:
    python3 scripts/icon/webp2bin.py                     # generate all system icons
    python3 scripts/icon/webp2bin.py --out /tmp/img      # custom output dir
    python3 scripts/icon/webp2bin.py --size 32 app       # single icon, custom size

Built-in vector fallbacks (drawn with PIL) cover the system icons that have
no webp source: app / logo / watchface. Style matches resources/images/icon/*.webp
(transparent bg, flat #2F80ED ink, no gradients).
"""

import argparse
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
WEBP_DIR = ROOT / "resources" / "images" / "icon"
DEFAULT_OUT = WEBP_DIR / "bin"

MAGIC = 0x19           # LV_IMAGE_HEADER_MAGIC
CF_ARGB8888 = 0x10     # LV_COLOR_FORMAT_ARGB8888

INK = (47, 128, 237)       # #2F80ED blue ink
DARK = (52, 73, 94)        # #34495E dark hands
WHITE = (255, 255, 255)


def save_lv_bin(im: Image.Image, out: Path) -> None:
    im = im.convert("RGBA")
    w, h = im.size
    stride = w * 4
    px = im.load()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            # ARGB8888 little-endian memory layout: [B, G, R, A]
            data += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    header = struct.pack("<BBBBHHH3B", MAGIC, CF_ARGB8888, 0, 0, w, h, stride, 0, 0, 0)
    out.write_bytes(header + bytes(data))


def gen_app_icon(size: int) -> Image.Image:
    """Generic app icon: blue rounded tile + 3x3 white dot grid."""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = size // 12
    d.rounded_rectangle([m, m, size - m, size - m], radius=size // 4, fill=INK)
    dot_r = max(1, size // 16)
    c0 = size * 7 // 24
    step = size // 4
    for i in range(3):
        for j in range(3):
            x = c0 + i * step
            y = c0 + j * step
            d.ellipse([x - dot_r, y - dot_r, x + dot_r, y + dot_r], fill=WHITE)
    return im


def gen_watchface(size: int) -> Image.Image:
    """Default watchface icon: clock face with ticks and 10:10 hands."""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = size / 2
    r = size / 2 - size / 12
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=INK, width=max(2, size // 16))
    for ang in (90, 180, 270, 0):
        a = math.radians(ang)
        x1 = cx + (r - size * 0.10) * math.cos(a)
        y1 = cy - (r - size * 0.10) * math.sin(a)
        x2 = cx + (r - size * 0.03) * math.cos(a)
        y2 = cy - (r - size * 0.03) * math.sin(a)
        d.line([(x1, y1), (x2, y2)], fill=INK, width=max(1, size // 24))
    ha = math.radians(60)
    hx = cx + size * 0.19 * math.cos(ha)
    hy = cy - size * 0.19 * math.sin(ha)
    ma = math.radians(300)
    mx = cx + size * 0.28 * math.cos(ma)
    my = cy - size * 0.28 * math.sin(ma)
    d.line([(cx, cy), (hx, hy)], fill=DARK, width=max(2, size // 16))
    d.line([(cx, cy), (mx, my)], fill=INK, width=max(1, size // 24))
    return im


def gen_logo(size: int) -> Image.Image:
    """Boot logo: blue ring + geometric white 'E'."""
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = size / 2
    r = size / 2 - size / 16
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=INK, width=max(4, size // 12))
    e_w = r * 0.66
    e_h = r * 0.72
    x0 = cx - e_w / 2
    y0 = cy - e_h / 2
    t = max(4, size // 16)
    d.line([(x0, y0), (x0 + e_w, y0)], fill=WHITE, width=t)
    d.line([(x0, y0), (x0, y0 + e_h)], fill=WHITE, width=t)
    d.line([(x0, y0 + e_h), (x0 + e_w, y0 + e_h)], fill=WHITE, width=t)
    d.line([(x0, y0 + e_h / 2), (x0 + e_w, y0 + e_h / 2)], fill=WHITE, width=t)
    return im


DRAW_BUILTIN = {
    "app": gen_app_icon,
    "logo": gen_logo,
    "watchface": gen_watchface,
}

# System icons: (source webp or None -> builtin draw) -> target bin name
SYSTEM_ICONS = {
    "settings": ("setting", 48),      # settings.bin
    "flash_light": ("torch", 48),     # flash_light.bin
    "album": ("album", 48),           # album.bin (Launcher native Album app)
    "texthub": ("texthub", 48),       # texthub.bin (Launcher native Texthub app)
    "app": (None, 48),                # app.bin (builtin)
    "watchface": (None, 48),          # watchface.bin (builtin)
    "logo": (None, 96),               # logo.bin (builtin, boot splash)
}


def gen_one(name: str, size: int, out_dir: Path, force: bool = False) -> Path:
    webp, _ = SYSTEM_ICONS.get(name, (name, size))
    src = WEBP_DIR / f"{webp}.webp" if webp else None
    if src and src.exists():
        im = Image.open(src).convert("RGBA")
        if im.size != (size, size):
            im = im.resize((size, size), Image.LANCZOS)
    elif name in DRAW_BUILTIN:
        im = DRAW_BUILTIN[name](size)
    else:
        print(f"SKIP {name}: no webp at {src} and no builtin drawer")
        return None
    out = out_dir / f"{name}.bin"
    if out.exists() and not force:
        print(f"KEEP {out} (exists, use --force to overwrite)")
        return out
    save_lv_bin(im, out)
    print(f"write {out} ({out.stat().st_size} B, {size}x{size})")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--size", type=int, default=None,
                    help="override icon size for the given names")
    ap.add_argument("--force", action="store_true",
                    help="overwrite existing .bin files")
    ap.add_argument("names", nargs="*",
                    help="icon names; empty = all system icons")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    names = args.names or list(SYSTEM_ICONS)
    for name in names:
        size = args.size or SYSTEM_ICONS.get(name, (name, 48))[1]
        gen_one(name, size, args.out, args.force)

    print("done ->", args.out)


if __name__ == "__main__":
    main()
