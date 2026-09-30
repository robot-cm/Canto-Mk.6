#!/usr/bin/env python3
"""
生成 22px 中文子集字体(GB2312 全量 6763 字 + 常用全角标点),
作为 eos_font_jbm_22 的 fallback,供 texthub 等 APP 渲染中文文本。

产物:resources/font/eos_font_han_sans_22.c (LVGL RLE 压缩,约几百 KB)
依赖:node + lv_font_conv 模块(用户机已有,如 ESP32Watch 项目 node_modules)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 用户机器上现成的 lv_font_conv(node 模块)
LV_FONT_CONV = "/home/erashaperavm/文档/ESP32Watch/node_modules/lv_font_conv/lv_font_conv.js"
if not os.path.exists(LV_FONT_CONV):
    LV_FONT_CONV = "/home/erashaperavm/.cache/qf_fontgen/node_modules/lv_font_conv/lv_font_conv.js"

SRC_OTF = os.path.join(ROOT, "third_party/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf")
OUT_C = os.path.join(ROOT, "resources/font/eos_font_han_sans_22.c")


def gb2312_chars():
    """GB2312 全部汉字(一级区16-55 + 二级区56-87, 每区 94 位)"""
    chars = []
    for q in range(16, 88):
        for w in range(1, 95):
            b = bytes([0xA0 + q, 0xA0 + w])
            try:
                chars.append(b.decode("gb2312"))
            except UnicodeDecodeError:
                pass  # 空位(GB2312 未定义码位)
    return chars


def main():
    chars = gb2312_chars()
    # 常用全角标点/符号(ASCII 部分由 jbm 覆盖,这里只补全角)
    extra = "，。！？；：、（）《》〈〉【】“”‘’—…·～￥×÷±℃°§¶№①-⑳㈠-㈩·"
    all_chars = "".join(dict.fromkeys(chars + list(extra)))
    print(f"charset size: {len(all_chars)} glyphs (GB2312 全量 + 全角标点)")

    cmd = [
        "node", LV_FONT_CONV,
        "--size", "22",
        "--bpp", "4",
        "--format", "lvgl",
        "--font", os.path.abspath(SRC_OTF),
        "--symbols", all_chars,
        "--lv-font-name", "eos_font_han_sans_22",
        "-o", os.path.abspath(OUT_C),
    ]
    print("running lv_font_conv ...")
    subprocess.run(cmd, check=True)
    print("done:", OUT_C)


if __name__ == "__main__":
    sys.exit(main())
