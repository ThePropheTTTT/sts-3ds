#!/usr/bin/env python3
"""Bake CJK bitmap font atlases for the 3DS port.

The 3DS has no font rasterizer we want to ship, and a full CJK OTF is 23 MB.
Instead we rasterize only the characters the game actually uses, pack them into
power-of-two alpha atlases, and emit a flat glyph table the runtime binary-searches.

Outputs (all under --out):
    zhs_<size>_<page>.png    grayscale coverage, white RGB + alpha
    font_zhs.json            metrics + glyph table, for tools/mockup.py
    font_zhs.h               same table as C arrays, for the engine

The atlas PNG is meant to be converted with tex3ds -f a4 (4bpp alpha),
so one 1024x1024 page costs 512 KB of texture memory.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract import ShelfPacker  # noqa: E402  (same greedy packer as the art bake)

# StS inline markup that must not reach the atlas: !D! !B! !M! NL #y #r [E]
_MARKUP = re.compile(r"![A-Z]!|#[a-z]|\bNL\b|\[[A-Z]\]")

# Always bake ASCII so numbers, costs and English ids render without a fallback.
_ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# Punctuation that appears in the corpus but is cheap to guarantee.
_EXTRA = "。，、；：？！“”‘’…—·《》「」（）【】％＋－×÷"

# Wording of our own HUD/menus, which the game's JSON does not contain.
_UI_WORDS = (
    "拖牌瞄准目标战斗回合结束能量抽弃堆胜利失败确认取消继续返回"
    "牌组地图篝火商店事件遗物药水金币生命格挡力量敏捷虚弱易伤中毒"
    "消耗虚无固有升级牌库弃置选择随机伤害抽牌当前状态"
)


def collect_charset(cards_path: str, l10n_glob: str) -> list[str]:
    """Every character the shipped JSON can ask us to draw."""
    chars: set[str] = set(_ASCII) | set(_EXTRA) | set(_UI_WORDS)

    def add(text: str) -> None:
        for ch in _MARKUP.sub("", text or ""):
            if not ch.isspace():
                chars.add(ch)

    if os.path.exists(cards_path):
        with open(cards_path, encoding="utf-8") as fp:
            for card in json.load(fp):
                add(card.get("name", ""))
                for atom in card.get("text") or []:
                    if atom and atom[0] == "t" and len(atom) > 1:
                        add(atom[1])

    def walk(node) -> None:
        if isinstance(node, str):
            add(node)
        elif isinstance(node, dict):
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    for path in sorted(glob.glob(l10n_glob)):
        with open(path, encoding="utf-8") as fp:
            walk(json.load(fp))

    # Everything the C side prints must exist too: enemy names, relic names,
    # menu wording. Scan the sources for text literals instead of maintaining a
    # second list by hand (missing glyphs = blank text on hardware).
    #
    # Not just CJK: the generated l10n carries punctuation the JSON corpus never
    # had, and a lost dash is as blank as a lost ideograph.  U+2013 only ever
    # appeared in port/source/l10n_gen.h that way and drew as nothing until this
    # scan widened; anything non-ASCII in the sources is a candidate glyph.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    for pattern in ("port/source/*.c", "port/source/*.h"):
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            with open(path, encoding="utf-8") as fp:
                for ch in fp.read():
                    if ord(ch) >= 0x80 and not ch.isspace():
                        chars.add(ch)
    return sorted(chars, key=ord)


def rasterize(font: ImageFont.FreeTypeFont, ch: str, ascent: int, descent: int):
    """Return (ink image, bx, by, advance) with (bx, by) relative to the line top."""
    advance = int(round(font.getlength(ch)))
    width = max(advance, 1) + 8                      # room for negative bearings
    height = ascent + descent + 4
    canvas = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(canvas)
    draw.text((4, 2), ch, font=font, fill=255)       # y=2 is the ascent line
    box = canvas.getbbox()
    if box is None:                                  # space and friends
        return None, 0, 0, advance
    ink = canvas.crop(box)
    return ink, box[0] - 4, box[1] - 2, advance


def bake_size(font_path: str, px: int, charset: list[str], atlas: int, pad: int):
    font = ImageFont.truetype(font_path, px)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs = {}
    missing = []
    for ch in charset:
        try:
            ink, bx, by, advance = rasterize(font, ch, ascent, descent)
        except Exception:
            missing.append(ch)
            continue
        glyphs[ch] = (ink, bx, by, advance)

    # Tall glyphs first keeps the shelf rows even.
    order = sorted(glyphs, key=lambda c: (-(glyphs[c][0].size[1] if glyphs[c][0] else 0), ord(c)))
    packer = ShelfPacker(size=atlas, pad=pad)
    for ch in order:
        ink = glyphs[ch][0]
        packer.add(ch, ink.size[0] if ink else 1, ink.size[1] if ink else 1)

    pages = []
    for index in range(len(packer.pages)):
        pages.append(Image.new("L", (atlas, atlas), 0))
    for ch, (page, x, y) in packer.placed.items():
        ink = glyphs[ch][0]
        if ink is not None:
            pages[page].paste(ink, (x, y))

    table = {}
    for ch in charset:
        ink, bx, by, advance = glyphs[ch]
        page, x, y = packer.placed[ch]
        w, h = ink.size if ink else (1, 1)
        table[ord(ch)] = [page, x, y, w, h, bx, by, advance]

    return {
        "px": px,
        "ascent": ascent,
        "descent": descent,
        "line_height": line_height,
        "pages": pages,
        "glyphs": table,
        "missing": missing,
    }


def write_header(path: str, baked: list[dict], atlas: int) -> None:
    lines = [
        "/* Generated by tools/bake_font.py -- do not edit. */",
        "#ifndef STS_FONT_ZHS_H",
        "#define STS_FONT_ZHS_H",
        "",
        "typedef struct {",
        "    unsigned int  cp;      /* UTF-8 codepoint, table is sorted by it */",
        "    unsigned short x, y;   /* top-left pixel in the page */",
        "    unsigned char page, w, h, adv;",
        "    signed char   bx, by;  /* ink offset from the pen/top of line */",
        "} StsGlyph;",
        "",
        f"#define STS_FONT_ATLAS_SIZE {atlas}",
        "",
    ]
    for entry in baked:
        size = entry["px"]
        glyphs = entry["glyphs"]
        lines.append(f"#define STS_FONT_{size}_COUNT {len(glyphs)}")
        lines.append(f"#define STS_FONT_{size}_LINE_HEIGHT {entry['line_height']}")
        lines.append(f"#define STS_FONT_{size}_ASCENT {entry['ascent']}")
        lines.append(f"#define STS_FONT_{size}_PAGES {len(entry['pages'])}")
        lines.append(f"static const StsGlyph sts_font_{size}[] = {{")
        for cp in sorted(glyphs):
            page, x, y, w, h, bx, by, adv = glyphs[cp]
            lines.append(
                "    {%d, %d, %d, %d, %d, %d, %d, %d, %d},"
                % (cp, x, y, page, w, h, adv, bx, by)
            )
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    with open(path, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(lines) + "\n")


def main(argv=None) -> int:
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--font", default=os.path.join(here, "assets/fonts/SourceHanSerifSC-Medium.otf"))
    ap.add_argument("--cards", default=os.path.join(here, "assets/data/cards.json"))
    ap.add_argument("--l10n", default=os.path.join(here, "assets/data/l10n/*.json"))
    ap.add_argument("--out", default=os.path.join(here, "assets/font"))
    ap.add_argument("--sizes", default="10,14,18", help="comma separated pixel sizes")
    ap.add_argument("--atlas", type=int, default=1024)
    ap.add_argument("--pad", type=int, default=1)
    ap.add_argument("--max-chars", type=int, default=0, help="debug: cap the charset")
    args = ap.parse_args(argv)

    if not os.path.exists(args.font):
        print(f"font not found: {args.font}", file=sys.stderr)
        return 2

    charset = collect_charset(args.cards, args.l10n)
    if args.max_chars:
        charset = charset[: args.max_chars]
    cjk = sum(1 for ch in charset if "\u4e00" <= ch <= "\u9fff")
    print(f"charset: {len(charset)} chars ({cjk} CJK)")

    os.makedirs(args.out, exist_ok=True)
    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
    baked = []
    for px in sizes:
        entry = bake_size(args.font, px, charset, args.atlas, args.pad)
        baked.append(entry)
        for index, page in enumerate(entry["pages"]):
            name = f"zhs_{px}_{index}.png"
            # Keep RGB pure white: citro2d modulates texture RGB by the tint colour,
            # so storing coverage in both RGB and alpha would double-darken glyphs.
            white = Image.new("L", page.size, 255)
            rgba = Image.merge("RGBA", (white, white, white, page))
            rgba.save(os.path.join(args.out, name))
        print(
            f"  {px:>2}px: {len(entry['glyphs'])} glyphs -> {len(entry['pages'])} page(s) "
            f"of {args.atlas}px, line_height={entry['line_height']} "
            f"(ascent {entry['ascent']} + descent {entry['descent']})"
            + (f", missing {len(entry['missing'])}" if entry["missing"] else "")
        )

    manifest = {
        "atlas": args.atlas,
        "font": os.path.basename(args.font),
        "charset": "".join(charset),
        "sizes": {
            str(entry["px"]): {
                "ascent": entry["ascent"],
                "descent": entry["descent"],
                "line_height": entry["line_height"],
                "pages": [f"zhs_{entry['px']}_{i}.png" for i in range(len(entry["pages"]))],
                "glyphs": {str(cp): v for cp, v in entry["glyphs"].items()},
            }
            for entry in baked
        },
    }
    with open(os.path.join(args.out, "font_zhs.json"), "w", encoding="utf-8", newline="\n") as fp:
        json.dump(manifest, fp, ensure_ascii=False, separators=(",", ":"))
    write_header(os.path.join(args.out, "font_zhs.h"), baked, args.atlas)
    total = sum(os.path.getsize(os.path.join(args.out, f)) for f in os.listdir(args.out))
    print(f"wrote {args.out} ({total / 1024:.0f} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
