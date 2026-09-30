#!/usr/bin/env python3
"""生成 eos_font_icon.c：收集 src 中实际使用的 RI_* 与 LV_SYMBOL_* 字符子集。

RI_*       -> third_party/RemixIcon/fonts/remixicon.ttf
LV_SYMBOL_* -> third_party/lvgl/scripts/built_in_font/FontAwesome5-*.woff

用法:
    python3 scripts/icon/gen_icon_font.py
输出:
    resources/font/eos_font_icon.c  (LVGL 压缩字体, bpp4, 22px)
"""
import re
import subprocess
import sys
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
EOS_ICON_H = SRC / "ui" / "symbol" / "eos_icon.h"
LV_SYMBOL_H = ROOT / "third_party" / "lvgl" / "src" / "font" / "lv_symbol_def.h"
RI_TTF = ROOT / "third_party" / "RemixIcon" / "fonts" / "remixicon.ttf"
FA_WOFF = ROOT / "third_party" / "lvgl" / "scripts" / "built_in_font" / "FontAwesome5-Solid+Brands+Regular.woff"
OUT = ROOT / "resources" / "font" / "eos_font_icon.c"

LV_FONT_CONV = os.environ.get(
    "LV_FONT_CONV",
    "/home/erashaperavm/.cache/qf_fontgen/node_modules/lv_font_conv/lv_font_conv.js",
)
FONT_SIZE = 22


def parse_ri_macros() -> dict[str, int]:
    """eos_icon.h: #define RI_XXX "\\uXXXX" -> {name: codepoint}"""
    table = {}
    pat = re.compile(r'^#define\s+(RI_[A-Z0-9_]+)\s+"\\u([0-9A-Fa-f]{4})"')
    for line in EOS_ICON_H.read_text(encoding="utf-8").splitlines():
        m = pat.match(line)
        if m:
            table[m.group(1)] = int(m.group(2), 16)
    return table


def parse_lv_symbol_macros() -> dict[str, int]:
    """lv_symbol_def.h: #define LV_SYMBOL_XXX "\\xEF\\x80\\x8C" -> {name: codepoint}"""
    table = {}
    name_pat = re.compile(r'^#define\s+(LV_SYMBOL_[A-Z0-9_]+)\s+"(.*)"')
    for line in LV_SYMBOL_H.read_text(encoding="utf-8").splitlines():
        m = name_pat.match(line)
        if not m:
            continue
        raw = m.group(2)
        # 解析 C 字符串字面量: "\xEF\x80\x8C" -> UTF-8 字节 -> 码点
        b = bytearray()
        i = 0
        while i < len(raw):
            if raw[i : i + 2] == "\\x" and i + 4 <= len(raw):
                b.append(int(raw[i + 2 : i + 4], 16))
                i += 4
            else:
                b.extend(raw[i].encode("utf-8"))
                i += 1
        s = b.decode("utf-8", errors="ignore")
        if s:
            table[m.group(1)] = ord(s[0])
    return table


# 仅作 JS 注册表(字符串常量, 不参与渲染), 引用全部宏会造成假阳性, 排除
SYMBOL_REG_FILES = ("src/ui/symbol/eos_icon.c", "src/ui/symbol/eos_icon.h")


def scan_used_macros() -> dict[str, set[str]]:
    """扫描 src 中引用到的 RI_* / LV_SYMBOL_* 宏名, 返回 {宏名: 引用文件}"""
    used = {}
    ri_pat = re.compile(r"\b(RI_[A-Z0-9_]+)\b")
    sym_pat = re.compile(r"\b(LV_SYMBOL_[A-Z0-9_]+)\b")
    for p in SRC.rglob("*"):
        if p.suffix not in (".c", ".h"):
            continue
        rel = p.relative_to(ROOT)
        if "ElenixOS-old-for-view" in str(rel) or "simulator" in str(rel):
            continue
        if str(rel) in SYMBOL_REG_FILES:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for name in set(ri_pat.findall(text)) | set(sym_pat.findall(text)):
            used.setdefault(name, set()).add(str(rel))
    return used


def filter_by_cmap(font_path: str, codepoints: set[int]) -> set[int]:
    """仅保留字体实际覆盖的码点; fontTools 不可用时跳过"""
    try:
        from fontTools.ttLib import TTFont
    except Exception as e:
        print(f"WARN: fontTools 不可用({e}), 跳过 cmap 检查, 缺失字符由 lv_font_conv 忽略")
        return codepoints
    f = TTFont(font_path, lazy=True)
    cmap = set()
    for t in f["cmap"].tables:
        cmap.update(t.cmap.keys())
    f.close()
    return {cp for cp in codepoints if cp in cmap}


def main():
    ri_table = parse_ri_macros()
    sym_table = parse_lv_symbol_macros()
    used_map = scan_used_macros()
    used = set(used_map)
    print(f"RI 宏总数: {len(ri_table)}, LV_SYMBOL 宏总数: {len(sym_table)}")
    print(f"src 中引用的宏: {len(used)} (RI: {sum(1 for m in used if m.startswith('RI_'))}, "
          f"LV_SYMBOL: {sum(1 for m in used if m.startswith('LV_SYMBOL_'))})")
    for name in sorted(used, key=lambda n: (-len(used_map[n]), n)):
        if name.startswith("RI_") or name.startswith("LV_SYMBOL_"):
            print(f"  {name}: {','.join(sorted(used_map[name])[:3])}")

    ri_cps = set()
    fa_cps = set()
    unknown = []
    for name in used:
        if name.startswith("RI_"):
            cp = ri_table.get(name)
            if cp is not None:
                ri_cps.add(cp)
            else:
                unknown.append(name)
        elif name.startswith("LV_SYMBOL_"):
            cp = sym_table.get(name)
            if cp is not None:
                fa_cps.add(cp)
            else:
                unknown.append(name)
    if unknown:
        print("WARN: 未在符号表中找到的宏:", ", ".join(sorted(unknown)[:20]))

    # 去掉控制字符(NUL/TAB 等, argv 无法携带)
    ri_cps = {cp for cp in ri_cps if cp >= 0x20}
    fa_cps = {cp for cp in fa_cps if cp >= 0x20}

    # 仅保留字体实际覆盖的码点
    ri_cps = filter_by_cmap(str(RI_TTF), ri_cps)
    fa_cps = filter_by_cmap(str(FA_WOFF), fa_cps)
    print(f"remixicon 子集: {len(ri_cps)} 字符, FontAwesome 子集: {len(fa_cps)} 字符")

    if not ri_cps and not fa_cps:
        print("无任何字符, 不生成.")
        sys.exit(1)

    cmd = [
        "node", LV_FONT_CONV,
        "--font", str(RI_TTF), "--symbols", "".join(chr(c) for c in sorted(ri_cps)),
        "--font", str(FA_WOFF), "--symbols", "".join(chr(c) for c in sorted(fa_cps)),
        "--size", str(FONT_SIZE), "--bpp", "4", "--format", "lvgl",
        "--lv-font-name", "eos_font_icon",
        "-o", str(OUT),
    ]
    print("运行:", " ".join(cmd[:6]), "...")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout:
        print(r.stdout[-4000:])
    if r.stderr:
        print("STDERR:", r.stderr[-2000:])
    if r.returncode != 0:
        print("lv_font_conv 失败")
        sys.exit(r.returncode)

    # 输出统计
    ri_chars = "".join(chr(c) for c in sorted(ri_cps))
    fa_chars = "".join(chr(c) for c in sorted(fa_cps))
    print(f"\n生成完成: {OUT}")
    print(f"RI 字符: {ri_chars}")
    print(f"FA 字符: {fa_chars}")


if __name__ == "__main__":
    main()
