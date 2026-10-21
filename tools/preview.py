#!/usr/bin/env python3
"""Render the extracted card table back to PNG.

This is the sanity check for tools/extract.py: if the art, the Chinese strings,
the markup tokens and the stats all made it through, a card drawn here looks
like a card.  The layout is deliberately simple and *not* the game's own frame
art, so nothing here depends on cardui offsets we have not verified yet.

    python tools/preview.py --assets assets --sheet 12
"""

from __future__ import annotations

import argparse
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TYPE_COLOR = {
    "ATTACK": (139, 47, 47),
    "SKILL": (47, 93, 139),
    "POWER": (74, 122, 58),
    "STATUS": (74, 74, 74),
    "CURSE": (74, 47, 93),
}
RARITY_COLOR = {
    "BASIC": (180, 180, 180),
    "COMMON": (200, 200, 200),
    "UNCOMMON": (110, 200, 255),
    "RARE": (255, 205, 90),
    "SPECIAL": (200, 160, 255),
}
INK = (238, 238, 238)
MARK_COLOR = {
    "yellow": (240, 192, 0),
    "green": (79, 209, 79),
    "blue": (111, 179, 255),
    "red": (255, 107, 107),
    "purple": (199, 139, 255),
    "white": (255, 255, 255),
}
ICON_GLYPH = {
    "E": ("\u25cf", (255, 160, 80)),   # generic energy
    "R": ("\u25cf", (255, 107, 107)),  # red energy
    "G": ("\u25cf", (79, 209, 79)),
    "B": ("\u25cf", (111, 179, 255)),
    "P": ("\u25cf", (199, 139, 255)),
}


def load(assets):
    with open(os.path.join(assets, "data", "cards.json"), encoding="utf-8") as f:
        cards = json.load(f)
    pages = {}
    for name in os.listdir(os.path.join(assets, "atlas")):
        if name.endswith(".png"):
            pages[name] = Image.open(os.path.join(assets, "atlas", name)).convert("RGBA")
    font_path = os.path.join(assets, "fonts", "SourceHanSerifSC-Medium.otf")
    if not os.path.exists(font_path):
        raise SystemExit(f"missing font: {font_path}")
    return cards, pages, font_path


def art_image(card, pages):
    art = card.get("art")
    if not art:
        return None
    px0, py0, w, h = art["px"]
    return pages[art["page"]].crop((px0, py0, px0 + w, py0 + h))


def atoms(card):
    """Token stream -> flat runs of (text, colour, is_icon)."""
    values = {
        "damage": card.get("damage"),
        "block": card.get("block"),
        "magic": card.get("magic"),
    }
    colour = INK
    out = []
    for tok in card["text"]:
        kind = tok[0]
        if kind == "t":
            out.append((tok[1], colour, False))
        elif kind == "v":
            v = values.get(tok[1])
            out.append(("?" if v is None else str(v), colour, False))
        elif kind == "c":
            colour = MARK_COLOR.get(tok[1], INK)
        elif kind == "n":
            out.append(("\n", colour, False))
        elif kind == "i":
            glyph, col = ICON_GLYPH.get(tok[1], ("[%s]" % tok[1], colour))
            out.append((glyph, col, True))
    return out


def split_atoms(runs):
    """Break runs into wrap units: CJK per char, ASCII per word."""
    units = []
    for text, colour, is_icon in runs:
        if text == "\n":
            units.append(("\n", colour, is_icon))
            continue
        if is_icon:
            units.append((text, colour, True))
            continue
        buf = ""
        for ch in text:
            if ch == " " or ch.isascii():
                buf += ch
                continue
            if buf:
                units.append((buf, colour, False))
                buf = ""
            units.append((ch, colour, False))
        if buf:
            units.append((buf, colour, False))
    return units


def wrap(units, font, max_w):
    lines = [[]]
    cur = 0.0
    for text, colour, is_icon in units:
        if text == "\n":
            lines.append([])
            cur = 0.0
            continue
        w = font.getlength(text)
        if cur + w > max_w and lines[-1]:
            lines.append([])
            cur = 0.0
            if text == " ":
                continue
        lines[-1].append((text, colour, is_icon))
        cur += font.getlength(text)
    return [ln for ln in lines if ln]


def draw_card(card, art, font_path, size=(250, 350)):
    w, h = size
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    base = TYPE_COLOR.get(card["type"], (60, 60, 60))
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=14, fill=base + (255,))
    d.rounded_rectangle((3, 3, w - 4, h - 4), radius=12, outline=(20, 20, 20, 255), width=2)

    if art is not None:
        box_w, box_h = w - 28, 150
        aw, ah = art.size
        s = min(box_w / aw, box_h / ah)
        nw, nh = max(1, int(aw * s)), max(1, int(ah * s))
        a = art.resize((nw, nh), Image.LANCZOS)
        ax = (w - nw) // 2
        ay = 12 + (box_h - nh) // 2
        img.alpha_composite(a, (ax, ay))

    f_big = ImageFont.truetype(font_path, 22)
    f_body = ImageFont.truetype(font_path, 15)
    f_small = ImageFont.truetype(font_path, 12)

    # cost orb, top left
    cx = cy = 30
    d.ellipse((cx - 19, cy - 19, cx + 19, cy + 19), fill=(24, 24, 32, 255),
              outline=(200, 200, 200, 255), width=2)
    cost = card["cost"]
    label = "X" if cost == -1 else ("-" if cost < 0 else str(cost))
    tw = f_big.getlength(label)
    d.text((cx - tw / 2, cy - 14), label, font=f_big, fill=(255, 255, 255))

    # name
    name = card["name"]
    nw = f_big.getlength(name)
    d.text(((w - nw) / 2, 168), name, font=f_big, fill=(255, 250, 235))
    d.line((18, 199, w - 18, 199), fill=(0, 0, 0, 120), width=2)

    # description
    lines = wrap(split_atoms(atoms(card)), f_body, w - 40)
    y = 208
    for line in lines[:6]:
        x = 20
        for text, colour, _is_icon in line:
            d.text((x, y), text, font=f_body, fill=colour)
            x += f_body.getlength(text)
        y += 20

    # footer
    foot = f"{card['type']} / {card['rarity']}"
    fw = f_small.getlength(foot)
    d.text(((w - fw) / 2, h - 26), foot, font=f_small,
           fill=RARITY_COLOR.get(card["rarity"], (200, 200, 200)))
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render extracted cards as PNG.")
    ap.add_argument("--assets", default="assets")
    ap.add_argument("--sheet", type=int, default=12, help="cards in the sheet")
    ap.add_argument("--ids", default="", help="comma separated card ids")
    ap.add_argument("--out", default="", help="output png path")
    args = ap.parse_args(argv)

    cards, pages, font_path = load(args.assets)
    by_id = {c["id"]: c for c in cards}
    if args.ids:
        picked = [by_id[i] for i in args.ids.split(",") if i in by_id]
    else:
        picked = cards[:args.sheet]

    tiles = [draw_card(c, art_image(c, pages), font_path) for c in picked]

    cols = min(4, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    tw, th, gap, top = 250, 350, 12, 44
    out = Image.new("RGBA", (gap + cols * (tw + gap), top + rows * (th + gap)),
                    (18, 18, 22, 255))
    d = ImageDraw.Draw(out)
    f_title = ImageFont.truetype(font_path, 18)
    d.text((gap, 12), f"Slay the Spire -> 3DS  \u00b7  {len(picked)} cards  \u00b7  "
                      f"zhs", font=f_title, fill=(230, 230, 230))
    for i, tile in enumerate(tiles):
        cx = gap + (i % cols) * (tw + gap)
        cy = top + (i // cols) * (th + gap)
        out.alpha_composite(tile, (cx, cy))

    dst = args.out or os.path.join(args.assets, "preview", "cards_sheet.png")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    out.convert("RGB").save(dst)
    print(f"[preview] {len(picked)} cards -> {dst} ({out.width}x{out.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
