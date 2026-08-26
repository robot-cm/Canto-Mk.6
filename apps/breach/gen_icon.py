#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_icon.py — 入侵协议(Breach Protocol) 图标生成器
=====================================================
产出 1: I:\\Canto Mk.6\\icon\\breach.webp    (512×512 RGBA webp, 扁平 Material 风格, 终端 ">_" motif)
产出 2: apps/breach/icon.bin             (48×48 RGBA PNG, 简化 ">_" + 光标, 启动器用)
配色取自应用主题: GREEN #00FF41 / YELLOW #FCEE0A / RED #FF003C / DARK #14141C; 背景透明。
"""

import os
from PIL import Image, ImageDraw

# 路径
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
WEBP_OUT = os.path.join(ROOT, "icon", "breach.webp")
BIN_OUT  = os.path.join(HERE, "icon.bin")

# 调色板 (RGBA)
GREEN  = (0x00, 0xFF, 0x41, 255)
YELLOW = (0xFC, 0xEE, 0x0A, 255)
RED    = (0xFF, 0x00, 0x3C, 255)
DARK   = (0x14, 0x14, 0x1C, 255)
T      = (0, 0, 0, 0)   # transparent


def draw_512():
    """512 设计源: 终端窗口(深色描边) + 三色圆点(mac 风) + 绿色 ">_" + 红色光标"""
    W = 512
    im = Image.new("RGBA", (W, W), T)
    d = ImageDraw.Draw(im)

    # 1) 终端外框(深色描边、透明填充)
    d.rounded_rectangle((56, 86, 456, 426), radius=40, outline=DARK, width=30)

    # 2) 标题栏分隔线
    d.line((86, 186, 426, 186), fill=DARK, width=14)

    # 3) 标题栏三圆点 (关/最小/最大, 红黄绿)
    r = 22
    for cx, fc in ((110, RED), (158, YELLOW), (206, GREEN)):
        d.ellipse((cx - r, 138 - r, cx + r, 138 + r), fill=fc)

    # 4) 提示符 ">" 绿色粗括号(两条粗线交于尖端, terminal 风)
    WC = 62
    d.line((78, 226, 254, 306), fill=GREEN, width=WC)
    d.line((78, 386, 254, 306), fill=GREEN, width=WC)

    # 5) 下划线 "_" 黄色圆角条
    d.rounded_rectangle((276, 354, 384, 394), radius=10, fill=YELLOW)

    # 6) 红色块状光标
    d.rounded_rectangle((394, 354, 434, 394), radius=4, fill=RED)

    os.makedirs(os.path.dirname(WEBP_OUT), exist_ok=True)
    im.save(WEBP_OUT, "WEBP", quality=95, method=6)
    print("wrote", WEBP_OUT, os.path.getsize(WEBP_OUT), "bytes")


def draw_48():
    """48 启动器: 简化版粗 ">_" + 光标(无外框/无圆点, 保证 48px 可读)"""
    W = 48
    im = Image.new("RGBA", (W, W), T)
    d = ImageDraw.Draw(im)

    # ">" 粗括号(两条粗线交于尖端)
    WC = 8
    d.line((7, 10, 24, 24), fill=GREEN, width=WC)
    d.line((7, 38, 24, 24), fill=GREEN, width=WC)
    # "_" 下划线
    d.rounded_rectangle((27, 32, 39, 38), radius=2, fill=YELLOW)
    # 红色光标
    d.rounded_rectangle((40, 32, 44, 38), radius=2, fill=RED)

    im.save(BIN_OUT, "PNG")
    print("wrote", BIN_OUT, os.path.getsize(BIN_OUT), "bytes")


if __name__ == "__main__":
    draw_512()
    draw_48()
