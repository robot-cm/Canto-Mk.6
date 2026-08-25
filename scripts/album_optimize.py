#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file album_optimize.py
@brief Normalize images under a directory so that every one satisfies the
       Album app constraints:
           - file size      <= 300 KB
           - decoded width/height <= 2048 px
       Supported input formats: PNG / JPG / JPEG / BMP (case-insensitive).

       Strategy (quality-preserving, in order):
           1. Images already compliant are copied byte-for-byte (lossless).
           2. Dimension overflow  -> downscale (LANCZOS) to <= 2048 px.
           3. Size overflow       -> re-encode:
                - JPEG/JPG: quality ladder (starts high, steps down until the
                  300 KB budget fits).
                - PNG:     try optimized PNG (lossless); if still too big,
                           fall back to JPEG ladder (alpha flattened onto the
                           Album background color).
                - BMP:     try PNG (lossless conversion); if still too big,
                           fall back to JPEG ladder.
           4. Output mirrors the input tree; re-encoded files may change
              extension (.bmp -> .png/.jpg, .png -> .jpg), all still supported
              by the Album app.

Usage:
    python3 scripts/album_optimize.py --input <dir> --output <dir>
    python3 scripts/album_optimize.py -i <dir> -o <dir> --max-size 300 --max-dim 2048 --quality-min 55

Requires: Pillow   (pip install pillow)
"""

import argparse
import io
import os
import shutil
import sys

from PIL import Image

EXTS = (".png", ".jpg", ".jpeg", ".bmp")
DEFAULT_MAX_SIZE = 300 * 1024   # Album MAX_FILE
DEFAULT_MAX_DIM = 2048          # Album decode sanity cap
# Background the Album UI paints the image onto (dark), used only when a
# transparent PNG must be flattened to JPEG.
ALBUM_BG = (12, 18, 26)


def collect_images(root):
    """Return list of (rel_path, abs_path) for every supported image under root."""
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.lower().endswith(EXTS):
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, root)
                found.append((rel, full))
    return found


def encode(im, fmt, **kw):
    buf = io.BytesIO()
    im.save(buf, fmt, **kw)
    return buf.getvalue()


def flatten_rgba_to_rgb(im):
    """Composite RGBA image onto the Album background color -> RGB."""
    bg = Image.new("RGBA", im.size, ALBUM_BG + (255,))
    im = Image.alpha_composite(bg, im.convert("RGBA"))
    return im.convert("RGB")


def jpeg_ladder(im, max_size, q_min):
    """Return the highest-quality JPEG blob that fits max_size.

    Steps quality down from 92 to q_min; the smallest blob produced is always
    returned so the caller never fails to get a result.
    """
    src = im.convert("RGB") if im.mode != "RGB" else im
    best = None
    for q in range(92, q_min - 1, -3):
        data = encode(src, "JPEG", quality=q, optimize=True)
        best = data
        if len(data) <= max_size:
            break
    return best


def reencode(im, src_fmt, max_size, q_min):
    """Re-encode image to fit max_size; returns (ext, blob) or (None, None)."""
    ext = src_fmt[1:]                      # 'jpg' / 'png' / 'bmp'

    if src_fmt == "bmp":
        # Lossless path first: BMP -> PNG.
        data = encode(im, "PNG", optimize=True)
        if len(data) <= max_size:
            return (".png", data)
        # Fall back to JPEG ladder.
        return (".jpg", jpeg_ladder(im, max_size, q_min))

    if src_fmt == "png":
        # 1) Lossless: optimized PNG (drop alpha channel when fully opaque).
        base = im.convert("RGBA")
        if base.getextrema()[3] == (255, 255):          # fully opaque
            opaque = base.convert("RGB")
        else:
            opaque = None
        for candidate in (opaque, im):
            if candidate is None:
                continue
            data = encode(candidate, "PNG", optimize=True)
            if len(data) <= max_size:
                return (".png", data)
        # 2) Fall back to JPEG ladder (flatten transparency onto bg).
        return (".jpg", jpeg_ladder(im, max_size, q_min))

    if src_fmt in ("jpg", "jpeg"):
        return (".jpg", jpeg_ladder(im, max_size, q_min))

    return (None, None)


def process_one(rel, src, root, out_dir, max_size, max_dim, q_min):
    """Normalize one image; returns (status, detail)."""
    try:
        im = Image.open(src)
        im.load()
    except Exception as e:
        return ("failed", "decode: %s" % e)

    w, h = im.size
    src_fmt = os.path.splitext(src)[1].lower().lstrip(".")
    src_size = os.path.getsize(src)

    # 1) Dimension cap: downscale so max(w, h) <= max_dim (LANCZOS).
    resized = False
    if w > max_dim or h > max_dim:
        r = max_dim / float(max(w, h))
        im = im.resize((max(1, int(w * r)), max(1, int(h * r))), Image.LANCZOS)
        w, h = im.size
        resized = True

    out_full = os.path.join(out_dir, rel)

    # 2) Already compliant and untouched -> lossless copy.
    if not resized and src_size <= max_size:
        os.makedirs(os.path.dirname(out_full), exist_ok=True)
        shutil.copyfile(src, out_full)
        return ("copied", "%d B" % src_size)

    # 3) Re-encode to fit the size budget.
    ext, data = reencode(im, src_fmt, max_size, q_min)
    if data is None:
        return ("failed", "unsupported format")

    out_full = os.path.splitext(out_full)[0] + ext
    os.makedirs(os.path.dirname(out_full), exist_ok=True)
    with open(out_full, "wb") as f:
        f.write(data)

    detail = "%dx%d -> %s (%d B%s)" % (
        im.size[0], im.size[1], ext.lstrip("."), len(data),
        ", resized" if resized else "")
    return ("optimized", detail)


def main():
    ap = argparse.ArgumentParser(
        description="Normalize images to Album constraints "
                    "(<=300 KB, dim <=2048px). See module docstring.")
    ap.add_argument("-i", "--input", required=True,
                    help="input directory to scan recursively")
    ap.add_argument("-o", "--output", required=True,
                    help="output directory (tree mirrored)")
    ap.add_argument("--max-size", type=int, default=DEFAULT_MAX_SIZE,
                    help="max file size in bytes (default %d)" % DEFAULT_MAX_SIZE)
    ap.add_argument("--max-dim", type=int, default=DEFAULT_MAX_DIM,
                    help="max decoded width/height in px (default %d)" % DEFAULT_MAX_DIM)
    ap.add_argument("--quality-min", type=int, default=55,
                    help="lowest JPEG quality allowed (default 55)")
    args = ap.parse_args()

    if not os.path.isdir(args.input):
        print("ERROR: input dir not found: %s" % args.input, file=sys.stderr)
        return 1

    items = collect_images(args.input)
    if not items:
        print("No supported images found under %s" % args.input)
        return 0

    os.makedirs(args.output, exist_ok=True)

    n_copy = n_opt = n_fail = 0
    total_in = total_out = 0
    for rel, full in items:
        status, detail = process_one(rel, full, args.input, args.output,
                                     args.max_size, args.max_dim,
                                     args.quality_min)
        if status == "copied":
            n_copy += 1
        elif status == "optimized":
            n_opt += 1
        else:
            n_fail += 1
        total_in += os.path.getsize(full)
        out_full = os.path.join(args.output, rel)
        if os.path.exists(out_full) or os.path.exists(
                os.path.splitext(out_full)[0] + ".png") or os.path.exists(
                os.path.splitext(out_full)[0] + ".jpg"):
            for cand in (out_full,
                         os.path.splitext(out_full)[0] + ".png",
                         os.path.splitext(out_full)[0] + ".jpg"):
                if os.path.exists(cand):
                    total_out += os.path.getsize(cand)
                    break
        print("[%s] %s -> %s  (%s)" % (status, full, "OK" if status != "failed"
                                       else "SKIP", detail))

    print("----")
    print("copied(already ok): %d   optimized: %d   failed: %d   total: %d"
          % (n_copy, n_opt, n_fail, len(items)))
    if total_in:
        print("bytes: %d -> %d  (%.1f%% saved)"
              % (total_in, total_out,
                 100.0 * (1.0 - total_out / float(total_in)) if total_out else 0))
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
