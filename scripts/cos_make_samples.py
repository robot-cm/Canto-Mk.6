#!/usr/bin/env python3
"""
Generate sample media for the ElenixOS simulator so the native Gallery and
Files apps have something to show on first run.

Usage:
    eos_make_samples.py <sim_fs_root>

Creates:
    <root>/gallery/   - a few PNG images (varying sizes to exercise fit-scaling)
    <root>/files/     - text files + a subdirectory (to exercise the browser)

Idempotent: safe to run on every build (POST_BUILD).
"""
import os
import sys
import zlib
import struct


def _chunk(typ, data):
    return struct.pack(">I", len(data)) + typ + data + \
        struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)


def write_png(path, w, h, pixels):
    """Write a truecolor (RGB, 8-bit) PNG. `pixels` is bytes of length w*h*3."""
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)  # PNG filter type 0 (none)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(_chunk(b"IHDR", ihdr))
        f.write(_chunk(b"IDAT", idat))
        f.write(_chunk(b"IEND", b""))


def solid(w, h, rgb):
    return bytes(rgb) * (w * h)


def gradient(w, h):
    buf = bytearray()
    for y in range(h):
        for x in range(w):
            buf.append(int(255 * x / max(1, w - 1)))
            buf.append(int(160 * y / max(1, h - 1)))
            buf.append(int(255 * (1 - x / max(1, w - 1))))
    return bytes(buf)


def main():
    if len(sys.argv) < 2:
        print("usage: eos_make_samples.py <sim_fs_root>")
        return 1

    root = sys.argv[1]
    gallery = os.path.join(root, "gallery")
    files = os.path.join(root, "files")
    docs = os.path.join(files, "docs")
    for d in (gallery, files, docs):
        os.makedirs(d, exist_ok=True)

    # --- Gallery: PNGs of varying sizes (fit-scaling test) ---
    write_png(os.path.join(gallery, "red.png"), 200, 200, solid(200, 200, (235, 87, 87)))
    write_png(os.path.join(gallery, "green.png"), 160, 120, solid(160, 120, (39, 174, 96)))
    write_png(os.path.join(gallery, "blue.png"), 120, 160, solid(120, 160, (47, 128, 237)))
    write_png(os.path.join(gallery, "gradient.png"), 160, 160, gradient(160, 160))

    # --- Files: text files + subdirectory ---
    with open(os.path.join(files, "readme.txt"), "w", encoding="utf-8") as f:
        f.write(
            "ElenixOS Files\n"
            "==============\n"
            "This is a sample text file for the Files browser.\n"
            "Tap a directory to descend; tap a file to read it.\n"
            "Use 'Up' to go to the parent folder and 'Exit' to leave.\n"
        )
    with open(os.path.join(files, "notes.txt"), "w", encoding="utf-8") as f:
        f.write(
            "Todo:\n"
            " - Wire Gallery/Files into the Launcher (done)\n"
            " - Verify fit-scaling for non-square images\n"
            " - Add long-press to delete? (future)\n"
        )
    with open(os.path.join(docs, "hello.txt"), "w", encoding="utf-8") as f:
        f.write("Hello from a nested directory inside the Files browser.\n")

    print("[samples] gallery + files seeded under %s" % root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
