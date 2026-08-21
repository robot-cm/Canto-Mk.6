#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file sync_app_icons.py
@brief Sync each local app's launcher icon (icon.bin) from the shared webp
       design sources in the top-level `icon/` folder.

The firmware's LVGL fork can only decode PNG (lodepng); it CANNOT decode
webp at runtime. The launcher reads, per installed app:
    EOS_APP_INSTALLED_DIR/<id>/icon.bin   (a 48x48 RGBA PNG)
The `icon/*.webp` files (512px, transparent) are the canonical design
sources. This script keeps the two in sync:

  For every <name>.webp in icon/
    if apps/<name>/ exists (a local script app):
        - regenerate apps/<name>/icon.bin  (48x48 RGBA PNG, downscaled)
        - overwrite the running FS copy at
          simulator/build/fs/.sys/app/apps/com.elenix.<name>/icon.bin
          (so the change is visible without a full rebuild)

System/plugin apps (sys.*) whose icons live outside apps/ are skipped
with a notice; wire them separately once their install path is known.

Usage:
    python sync_app_icons.py
"""

import os
import glob
from PIL import Image

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.abspath(os.path.join(BASE_DIR, "..", ".."))
ICON_DIR = os.path.join(REPO_DIR, "icon")
APPS_DIR = os.path.join(REPO_DIR, "apps")
FS_DIR = os.path.join(REPO_DIR, "simulator", "build", "fs", ".sys", "app", "apps")

ICON_SIZE = 48


def make_icon_bin(webp_path, out_bin):
    """Downscale a webp design source into a 48x48 RGBA PNG icon.bin."""
    im = Image.open(webp_path).convert("RGBA")
    im = im.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
    im.save(out_bin, "PNG")


def main():
    if not os.path.isdir(ICON_DIR):
        print("ERROR: icon dir not found:", ICON_DIR)
        return 1

    synced = 0
    skipped = 0
    for webp in sorted(glob.glob(os.path.join(ICON_DIR, "*.webp"))):
        name = os.path.splitext(os.path.basename(webp))[0]
        appdir = os.path.join(APPS_DIR, name)
        if not os.path.isdir(appdir):
            # Not a local script app under apps/ -> system/plugin app, skip.
            print("skip  %-12s (no apps/%s source -> system/plugin app)" % (name, name))
            skipped += 1
            continue

        src_bin = os.path.join(appdir, "icon.bin")
        make_icon_bin(webp, src_bin)
        print("write %-12s -> %s" % (name, src_bin))

        # Push to the running simulator FS so it shows immediately.
        fs_bin = os.path.join(FS_DIR, "com.elenix." + name, "icon.bin")
        if os.path.exists(os.path.dirname(fs_bin)):
            make_icon_bin(webp, fs_bin)
            print("  + sync running FS: %s" % fs_bin)
        else:
            print("  (running FS copy not found; will apply on next eapk rebuild)")
        synced += 1

    print("\nSummary: %d local app icon(s) synced from icon folder, %d skipped (system/plugin)." %
          (synced, skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
