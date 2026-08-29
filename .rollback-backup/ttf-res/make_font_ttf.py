#!/usr/bin/env python3
"""子集化思源黑体,生成适配 1.28 寸圆屏的中文 TTF。

用法:
    python3 make_font_ttf.py

输入:  third_party/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf
输出:  resources/font/font.ttf   (GB2312 常用字 + 标点 + ASCII, ~2MB)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "third_party/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf")
OUT_DIR = os.path.join(ROOT, "resources/font")
OUT = os.path.join(OUT_DIR, "font.ttf")

# 常用汉字 + 标点 + ASCII + 全角符号 + 常用符号区
#   U+0020-007E   ASCII 可见字符
#   U+00A0-00FF   拉丁-1 补充(°±×÷·¡¿)
#   U+2000-206F   通用标点(–—…)
#   U+2070-209F   上标/下标
#   U+20A0-20CF   货币符号
#   U+2100-214F   字母符号(℃℉№)
#   U+2150-218F   数字形式
#   U+2190-21FF   箭头(←↑→↓)
#   U+2200-22FF   数学符号(≈≠≤≥∞)
#   U+2300-23FF   杂项技术符号
#   U+2460-24FF   带圈数字
#   U+2500-257F   制表符
#   U+2580-259F   块元素
#   U+25A0-25FF   几何符号(■□▲△●○◆)
#   U+2600-26FF   杂项符号(☀☁★☆☺)
#   U+2700-27BF   装饰符号(✓✗✈)
#   U+2B00-2BFF   杂项符号和箭头
#   U+2E00-2E7F   补充标点
#   U+3000-303F   CJK 标点(、。"")《》…)
#   U+4E00-9FFF   基本汉字(20902 字,覆盖 GB2312 全部 + 大量生僻字)
#   U+FE30-FE4F   CJK 兼容形式
#   U+FE50-FE6F   小写变体
#   U+FF00-FFEF   全角 ASCII / 全角标点
UNICODES = ("U+0020-007E,U+00A0-00FF,U+2000-206F,U+2070-209F,U+20A0-20CF,"
            "U+2100-214F,U+2150-218F,U+2190-21FF,U+2200-22FF,U+2300-23FF,"
            "U+2460-24FF,U+2500-257F,U+2580-259F,U+25A0-25FF,U+2600-26FF,"
            "U+2700-27BF,U+2B00-2BFF,U+2E00-2E7F,U+3000-303F,U+4E00-9FFF,"
            "U+FE30-FE4F,U+FE50-FE6F,U+FF00-FFEF")


def main():
    if not os.path.exists(SRC):
        sys.exit(f"[错误] 源字体不存在: {SRC}")
    os.makedirs(OUT_DIR, exist_ok=True)

    cmd = [
        sys.executable, "-m", "fontTools.subset", SRC,
        f"--unicodes={UNICODES}",
        "--no-hinting",        # 小屏矢量直渲更干净,避免 hinting 锯齿
        "--drop-tables+=DSIG", # 去掉签名表,减小体积
        f"--output-file={OUT}",
    ]
    print("[运行]", " ".join(cmd))
    subprocess.run(cmd, check=True)

    size = os.path.getsize(OUT)
    print(f"[完成] 生成 {OUT}  ({size/1024/1024:.2f} MB)")


if __name__ == "__main__":
    main()
