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
           5. Optional pre-encoding for the Album viewer (SD card). Each
              output image is a self-contained package: pre-encoded assets
              live in hidden folders NEXT to the image
                - <img dir>/.thumb/<base>.bin     : RGB565 LVGL bin, fitted
                  to <=240px per side (thumbnail state, zero-decode device).
                - <img dir>/.tiles/<base>/tile_<c>_<r>.bin : 256x256 RGB565
                  LVGL bins (edge tiles keep their real size), + meta.txt
                  "orig_w orig_h tile cols rows" for the 1:1 browse mode.
              The whole output is an "album" folder: drop it (or several of
              them, at any depth) into /sdcard/album/ on the card. The
              launcher hides dot-directories, so .thumb/.tiles never show up.
              The tile grid adapts to the ACTUAL size of every image, so any
              input dimension is handled (e.g. 2560x1440 -> 2048x1152).
              RGB565 is ~2 B/px while JPEG inputs are ~10-20x compressed, so
              raw tile data can exceed the input file. A SIZE BUDGET keeps
              this in check: by default thumb + tiles stay <= 1.2x the input
              file size, stepping the encoded resolution down (same aspect)
              when needed; the long edge never drops below --enc-min (512px).
              meta.txt always records the ACTUAL encoded size, so the device
              viewer pans/zooms correctly without any firmware change.

Usage:
    python3 scripts/album_optimize.py --input <dir> --output <dir>
    python3 scripts/album_optimize.py -i <dir> -o <dir> --max-size 300 --max-dim 2048 --quality-min 55
    python3 scripts/album_optimize.py -i <dir> -o <dir> --no-tiles --no-thumb
    python3 scripts/album_optimize.py -i <dir> -o <dir> --budget 1.2 --enc-min 512
    python3 scripts/album_optimize.py -i <dir> -o <dir> --jobs 8

Requires: Pillow  (pip install pillow)
Optional: numpy   (pip install numpy) — SIMD-accelerated RGB565 packing,
          falls back to a pure-Python loop when numpy is missing.
Images are processed in parallel (--jobs, default = CPU count).
"""

import argparse
import io
import multiprocessing as mp
import os
import shutil
import struct
import sys

from PIL import Image

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    np = None
    HAVE_NUMPY = False

EXTS = (".png", ".jpg", ".jpeg", ".bmp")
DEFAULT_MAX_SIZE = 300 * 1024   # Album MAX_FILE
DEFAULT_MAX_DIM = 2048          # Album decode sanity cap
# Background the Album UI paints the image onto (dark), used only when a
# transparent PNG must be flattened to JPEG.
ALBUM_BG = (12, 18, 26)

# LVGL binary image format (see scripts/icon/webp2bin.py save_lv_bin)
LV_MAGIC = 0x19
CF_RGB565 = 0x12                # LV_COLOR_FORMAT_RGB565
THUMB_MAX = 240                 # thumbnail fit box (px)
TILE_DEFAULT = 256              # tile size (px)
ENC_MIN = 512                   # floor for the pre-encode long edge (px)
BUDGET_DEFAULT = 1.2            # max pre-encode bytes vs the input file size
LV_BIN_HEADER = 12              # LVGL bin header bytes


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


def _rgb565_blob(im):
    """RGB image -> raw RGB565 little-endian bytes ([b0|b1]: R5<<11|G6<<5|B5).

    numpy (when available) does this vectorized on SIMD-width ops; the
    fallback streams pixels via struct.iter_unpack instead of per-pixel
    px[x, y] calls (several times faster).
    """
    im = im.convert("RGB")
    if HAVE_NUMPY:
        a = np.asarray(im, dtype=np.uint16)            # (h, w, 3)
        v = ((a[..., 0] & 0xF8) << 8) | ((a[..., 1] & 0xFC) << 3) | (a[..., 2] >> 3)
        return v.astype("<u2", copy=False).tobytes()
    w, h = im.size
    raw = im.tobytes()
    data = bytearray(w * h * 2)
    off = 0
    for r, g, b in struct.iter_unpack("BBB", raw):
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data[off] = v & 0xFF
        data[off + 1] = v >> 8
        off += 2
    return bytes(data)


def save_rgb565_bin(im, out):
    """Write LVGL RGB565 binary image (12B LE header + pixels) to `out`."""
    w, h = im.size
    stride = w * 2
    pix = _rgb565_blob(im)
    header = struct.pack("<BBHHHHH", LV_MAGIC, CF_RGB565, 0, w, h, stride, 0)
    with open(out, "wb") as f:
        f.write(header)
        f.write(pix)


def gen_thumb(im, out, max_side=THUMB_MAX):
    """Fit image into max_side box (LANCZOS) and save RGB565 bin."""
    w, h = im.size
    if w > max_side or h > max_side:
        r = max_side / float(max(w, h))
        im = im.resize((max(1, int(w * r)), max(1, int(h * r))), Image.LANCZOS)
    save_rgb565_bin(im, out)


def gen_tiles(im, out_dir, tile=TILE_DEFAULT):
    """Slice image into tile x tile RGB565 bins (edge tiles keep real size)
    plus meta.txt "orig_w orig_h tile cols rows". Grid adapts to any size.

    Converts the WHOLE image to RGB565 once, then row-slices tiles out of
    that single blob (instead of re-converting every tile), which makes the
    tile grid generation effectively free after one pass.
    """
    w, h = im.size
    cols = (w + tile - 1) // tile
    rows = (h + tile - 1) // tile
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "meta.txt"), "w") as f:
        f.write("%d %d %d %d %d\n" % (w, h, tile, cols, rows))
    pix = _rgb565_blob(im)
    stride = w * 2
    for c in range(cols):
        x0 = c * tile
        x1 = min(x0 + tile, w)
        for r in range(rows):
            y0 = r * tile
            y1 = min(y0 + tile, h)
            tw, th = x1 - x0, y1 - y0
            tbytes = b"".join(
                pix[y * stride + x0 * 2:y * stride + x1 * 2]
                for y in range(y0, y1))
            header = struct.pack("<BBHHHHH", LV_MAGIC, CF_RGB565, 0, tw, th,
                                 tw * 2, 0)
            with open(os.path.join(out_dir, "tile_%d_%d.bin" % (c, r)),
                      "wb") as f:
                f.write(header)
                f.write(tbytes)


def _tiles_bytes(w, h, tile=TILE_DEFAULT):
    """Exact bytes of the tile grid at w x h (headers + RGB565 pixels)."""
    cols = (w + tile - 1) // tile
    rows = (h + tile - 1) // tile
    n = 0
    for c in range(cols):
        tw = min(tile, w - c * tile)
        for r in range(rows):
            th = min(tile, h - r * tile)
            n += LV_BIN_HEADER + tw * th * 2
    return n


def _thumb_bytes(w, h):
    """Exact bytes of the thumbnail as gen_thumb would produce."""
    tw, th = w, h
    if tw > THUMB_MAX or th > THUMB_MAX:
        r = THUMB_MAX / float(max(tw, th))
        tw, th = max(1, int(tw * r)), max(1, int(th * r))
    return LV_BIN_HEADER + tw * th * 2


def enc_size_for_budget(w, h, budget_bytes, tile=TILE_DEFAULT,
                        enc_min=ENC_MIN, include_thumb=True):
    """Largest (same aspect ratio) pre-encode size whose thumb + tiles fit
    budget_bytes. RGB565 is ~2 B/px, so when the input JPEG is small this
    steps the resolution down (12% per step) until the budget fits; the long
    edge never drops below enc_min. Returns (w, h)."""
    def cost(a, b):
        n = _tiles_bytes(a, b, tile)
        if include_thumb:
            n += _thumb_bytes(a, b)
        return n
    if cost(w, h) <= budget_bytes:
        return w, h
    while True:
        long = max(w, h)
        if long <= enc_min:
            return w, h
        step = max(0.88, enc_min / float(long))
        w2 = max(1, int(w * step))
        h2 = max(1, int(h * step))
        if (w2, h2) == (w, h):            # no progress -> stop
            return w, h
        if max(w2, h2) <= enc_min or cost(w2, h2) <= budget_bytes:
            return w2, h2
        w, h = w2, h2


def flatten_rgba_to_rgb(im):
    """Composite RGBA image onto the Album background color -> RGB."""
    bg = Image.new("RGBA", im.size, ALBUM_BG + (255,))
    im = Image.alpha_composite(bg, im.convert("RGBA"))
    return im.convert("RGB")


def jpeg_ladder(im, max_size, q_min):
    """Return the highest-quality JPEG blob that fits max_size.

    JPEG output size grows monotonically with quality, so binary-search the
    largest q in [q_min, 92] that fits (~7 encodes instead of ~13 linear
    steps). If none fit, the smallest (lowest-q) blob is returned so the
    caller never fails to get a result.
    """
    src = im.convert("RGB") if im.mode != "RGB" else im
    lo, hi = q_min, 92
    fit = None
    smallest = None
    while lo <= hi:
        q = (lo + hi) // 2
        data = encode(src, "JPEG", quality=q, optimize=True)
        if smallest is None or len(data) < len(smallest):
            smallest = data
        if len(data) <= max_size:
            fit = data
            lo = q + 1
        else:
            hi = q - 1
    return fit if fit is not None else smallest


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


def process_one(rel, src, root, out_dir, max_size, max_dim, q_min,
                do_thumb=True, do_tiles=True, tile_size=TILE_DEFAULT,
                budget_ratio=BUDGET_DEFAULT, enc_min=ENC_MIN):
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
        status, detail = ("copied", "%d B" % src_size)
    else:
        # 3) Re-encode to fit the size budget.
        ext, data = reencode(im, src_fmt, max_size, q_min)
        if data is None:
            return ("failed", "unsupported format")

        out_full = os.path.splitext(out_full)[0] + ext
        os.makedirs(os.path.dirname(out_full), exist_ok=True)
        with open(out_full, "wb") as f:
            f.write(data)

        status, detail = ("optimized", "%dx%d -> %s (%d B%s)" % (
            im.size[0], im.size[1], ext.lstrip("."), len(data),
            ", resized" if resized else ""))

    # 4) Pre-encode thumbnail + tiles from the FINAL image (adaptive grid,
    #    matches whatever the final size is). RGB565 is ~2 B/px vs the ~10x
    #    JPEG input, so the encoded resolution is converged to keep
    #    thumb + tiles <= budget_ratio x the input file size.
    # Assets travel with each image: hidden ".thumb"/".tiles" folders NEXT to
    # the file, so one output = one self-contained package you can drop
    # anywhere under /sdcard/album/ (any depth). The launcher hides dot-dirs.
    base = os.path.splitext(os.path.basename(out_full))[0]
    img_dir = os.path.dirname(out_full)
    enc_im = im
    if do_thumb or do_tiles:
        budget = max(1, int(src_size * budget_ratio))
        enc_w, enc_h = enc_size_for_budget(im.size[0], im.size[1], budget,
                                           tile_size, enc_min,
                                           include_thumb=do_thumb)
        if (enc_w, enc_h) != (im.size[0], im.size[1]):
            enc_im = im.resize((enc_w, enc_h), Image.LANCZOS)
            detail += " | enc %dx%d" % (enc_w, enc_h)
    if do_thumb:
        try:
            tdir = os.path.join(img_dir, ".thumb")
            os.makedirs(tdir, exist_ok=True)
            gen_thumb(enc_im, os.path.join(tdir, base + ".bin"))
        except Exception as e:
            detail += " | thumb FAIL: %s" % e
    if do_tiles:
        try:
            gen_tiles(enc_im, os.path.join(img_dir, ".tiles", base),
                      tile_size)
        except Exception as e:
            detail += " | tiles FAIL: %s" % e

    return (status, detail)


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
    ap.add_argument("--no-thumb", action="store_true",
                    help="skip RGB565 thumbnail pre-encoding")
    ap.add_argument("--no-tiles", action="store_true",
                    help="skip RGB565 tile grid pre-encoding (1:1 browse mode)")
    ap.add_argument("--tile-size", type=int, default=TILE_DEFAULT,
                    help="tile edge in px (default %d)" % TILE_DEFAULT)
    ap.add_argument("--budget", type=float, default=BUDGET_DEFAULT,
                    help="max pre-encode bytes (thumb+tiles) as a ratio of the "
                         "input file size (default %.1f)" % BUDGET_DEFAULT)
    ap.add_argument("--enc-min", type=int, default=ENC_MIN,
                    help="floor for the pre-encode long edge in px "
                         "(default %d)" % ENC_MIN)
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="parallel workers (default: CPU count)")
    args = ap.parse_args()

    if not os.path.isdir(args.input):
        print("ERROR: input dir not found: %s" % args.input, file=sys.stderr)
        return 1

    items = collect_images(args.input)
    if not items:
        print("No supported images found under %s" % args.input)
        return 0

    # The whole output is a self-contained "album" package: <output>/album/
    # holds the images plus album_thumbnail/album_tiles subfolders. Drop that
    # album/ folder onto /sdcard/ on the card and it just works.
    album_root = os.path.join(args.output, "album")
    os.makedirs(album_root, exist_ok=True)

    # Process images in parallel (each image is independent); results are
    # printed in input order afterwards.  Images that only get copied are
    # cheap, but re-encode + pre-encode dominate and parallelize linearly.
    jobs = args.jobs if args.jobs > 0 else mp.cpu_count()
    tasks = [(rel, full, args.input, album_root, args.max_size, args.max_dim,
              args.quality_min, not args.no_thumb, not args.no_tiles,
              args.tile_size, args.budget, args.enc_min)
             for rel, full in items]
    if jobs > 1 and len(tasks) > 1:
        with mp.Pool(processes=jobs) as pool:
            results = pool.starmap(process_one, tasks)
    else:
        results = [process_one(*t) for t in tasks]

    n_copy = n_opt = n_fail = 0
    total_in = total_out = 0
    for (rel, full), (status, detail) in zip(items, results):
        if status == "copied":
            n_copy += 1
        elif status == "optimized":
            n_opt += 1
        else:
            n_fail += 1
        total_in += os.path.getsize(full)
        out_full = os.path.join(album_root, rel)
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
    pre_total = 0
    for rp, _dn, fs in os.walk(album_root):
        hidden = os.sep + ".thumb" in rp or os.sep + ".tiles" in rp
        if hidden:
            for fn in fs:
                pre_total += os.path.getsize(os.path.join(rp, fn))
    if total_in and pre_total:
        print("pre-encode (thumb+tiles): %d B = %.2fx input  (budget %.2fx)"
              % (pre_total, pre_total / float(total_in), args.budget))
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
