#!/usr/bin/env python3
"""Render a design mockup of the two 3DS screens from real extracted data.

This is not the engine: it is a cheap way to answer layout questions ("does a
12px card name fit in a 62px hand card?") before writing any C. It uses the
baked bitmap font from tools/bake_font.py and the card art from tools/extract.py,
so what you see is what the 3DS can actually draw.

    python tools/mockup.py            -> assets/preview/screens.png

Panels: idle state (left) and aim state while a card is being dragged (right).
"""

from __future__ import annotations

import argparse
import json
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TOP_W, TOP_H = 400, 240
BOT_W, BOT_H = 320, 240

# Cards that cannot begin a line (Chinese line-breaking 禁则).
NO_LINE_START = "。，、；：？！）》」』”’…·%"
NO_LINE_END = "（《「『“‘"

TYPE_COLORS = {
    "ATTACK": (0x8B, 0x2F, 0x2F),
    "SKILL": (0x2F, 0x5D, 0x8B),
    "POWER": (0x4A, 0x7A, 0x3A),
    "STATUS": (0x4A, 0x4A, 0x4A),
    "CURSE": (0x4A, 0x2F, 0x5D),
}

INK = (238, 228, 208)
DIM = (168, 160, 142)
GOLD = (230, 196, 110)


class BitmapFont:
    """Minimal renderer for the baked atlas: enough to lay out a mockup."""

    def __init__(self, directory: str):
        with open(os.path.join(directory, "font_zhs.json"), encoding="utf-8") as fp:
            self.manifest = json.load(fp)
        self.pages = {}
        for size, data in self.manifest["sizes"].items():
            self.pages[size] = [
                Image.open(os.path.join(directory, name)).convert("RGBA")
                for name in data["pages"]
            ]

    def info(self, size: str) -> dict:
        return self.manifest["sizes"][size]

    def advance(self, size: str, text: str) -> int:
        glyphs = self.info(size)["glyphs"]
        total = 0
        for ch in text:
            entry = glyphs.get(str(ord(ch)))
            total += entry[7] if entry else self.info(size)["line_height"] // 2
        return total

    def draw(self, canvas: Image.Image, size: str, text: str, x: int, y: int, color=INK) -> int:
        glyphs = self.info(size)["glyphs"]
        pen = x
        for ch in text:
            entry = glyphs.get(str(ord(ch)))
            if entry is None:
                pen += self.info(size)["line_height"] // 2
                continue
            page, px, py, w, h, bx, by, adv = entry
            if ch != " ":
                ink = self.pages[size][page].crop((px, py, px + w, py + h))
                solid = Image.new("RGBA", ink.size, tuple(color) + (255,))
                solid.putalpha(ink.getchannel("A"))
                canvas.alpha_composite(solid, (pen + bx, y + by))
            pen += adv
        return pen - x

    def wrap(self, size: str, text: str, max_width: int) -> list[str]:
        """Wrap per character (CJK) and honour 禁则 punctuation placement."""
        lines: list[str] = []
        current = ""
        for ch in text:
            if ch == "\n":
                lines.append(current)
                current = ""
                continue
            candidate = current + ch
            if self.advance(size, candidate) > max_width and current:
                if ch in NO_LINE_START or current[-1] in NO_LINE_END:
                    carry = current[-1]
                    lines.append(current[:-1])
                    current = carry + ch
                else:
                    lines.append(current)
                    current = ch
            else:
                current = candidate
        if current:
            lines.append(current)
        return lines


def load_assets(root: str):
    with open(os.path.join(root, "assets/data/cards.json"), encoding="utf-8") as fp:
        cards = {card["id"]: card for card in json.load(fp)}
    pages = {}
    atlas_dir = os.path.join(root, "assets/atlas")
    for name in os.listdir(atlas_dir):
        if name.endswith(".png"):
            pages[name] = Image.open(os.path.join(atlas_dir, name)).convert("RGBA")
    return cards, pages


def card_art(cards, pages, card_id: str) -> Image.Image | None:
    card = cards.get(card_id)
    if not card or not card.get("art"):
        return None
    x, y, w, h = card["art"]["px"]
    return pages[card["art"]["page"]].crop((x, y, x + w, y + h))


def round_rect(draw: ImageDraw.ImageDraw, box, radius, fill=None, outline=None, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def render_tokens(atoms, card) -> str:
    """Turn the extracted atom list back into plain text with numbers filled in."""
    values = {"damage": card.get("damage"), "block": card.get("block"), "magic": card.get("magic")}
    out = []
    for atom in atoms:
        if atom[0] == "t":
            out.append(atom[1])
        elif atom[0] == "v":
            value = values.get(atom[1])
            out.append(str(value if value is not None else "?"))
        elif atom[0] == "n":
            out.append("\n")
        elif atom[0] == "i":
            out.append("[E]")
    return "".join(out)


def draw_card(font, cards, pages, card_id, size, art_h, name_size, body_size=None):
    """Compose one card. Returns a new RGBA image."""
    card = cards.get(card_id)
    w, h = size
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    accent = TYPE_COLORS.get(card["type"], (0x55, 0x55, 0x55))
    round_rect(draw, (0, 0, w - 1, h - 1), 6, fill=(30, 26, 23, 255))
    round_rect(draw, (0, 0, w - 1, h - 1), 6, outline=accent + (255,), width=2)

    art = card_art(cards, pages, card_id)
    inner = w - 8
    if art is not None:
        img.alpha_composite(art.resize((inner, art_h), Image.LANCZOS), (4, 4))
        y = 4 + art_h
    else:
        y = 6

    name_w = font.advance(name_size, card["name"])
    font.draw(img, name_size, card["name"], (w - name_w) // 2, y + 2)
    y += font.info(name_size)["line_height"] + 1

    draw.line((6, y, w - 7, y), fill=accent + (170,))
    y += 2

    if body_size:
        text = render_tokens(card.get("text") or [], card)
        for line in font.wrap(body_size, text, w - 12):
            font.draw(img, body_size, line, 6, y, (228, 218, 198))
            y += font.info(body_size)["line_height"]

    if not body_size and h >= 80:
        kind = {"ATTACK": "攻击", "SKILL": "技能", "POWER": "能力"}.get(card["type"], "")
        if kind:
            kw = font.advance("12", kind)
            font.draw(img, "12", kind, (w - kw) // 2, h - font.info("12")["line_height"] - 4, DIM)

    cost = card.get("cost")
    label = "X" if cost == -1 else ("-" if cost in (-2, None) else str(cost))
    r = 9
    draw.ellipse((2, 2, 2 + 2 * r, 2 + 2 * r), fill=(20, 16, 14, 255), outline=accent + (255,), width=2)
    tw = font.advance("12", label)
    font.draw(img, "12", label, 2 + r - tw // 2, 2 + r - 8, INK)
    return img


def bezier(p0, p1, p2, steps=26):
    points = []
    for i in range(steps + 1):
        t = i / steps
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        points.append((x, y))
    return points


def draw_top_screen(font, cards, pages, target: int | None = None) -> Image.Image:
    img = Image.new("RGBA", (TOP_W, TOP_H), (18, 22, 20, 255))
    draw = ImageDraw.Draw(img)
    for y in range(TOP_H):
        t = y / TOP_H
        draw.line((0, y, TOP_W, y), fill=(int(18 + 8 * t), int(24 + 12 * t), int(22 + 7 * t)))
    draw.rectangle((0, 200, TOP_W, TOP_H), fill=(32, 30, 26, 255))
    draw.line((0, 200, TOP_W, 200), fill=(66, 60, 50, 255))

    # Top bar: HP / floor / gold / relics.
    draw.rectangle((0, 0, TOP_W, 20), fill=(14, 14, 14, 220))
    font.draw(img, "12", "80/80", 6, 1, (232, 120, 110))
    font.draw(img, "12", "第 1 层", 62, 1, DIM)
    font.draw(img, "12", "99", 126, 1, GOLD)
    for i in range(2):
        round_rect(draw, (TOP_W - 24 - i * 22, 3, TOP_W - 10 - i * 22, 17), 3,
                   fill=(40, 36, 30, 255), outline=(120, 104, 70, 255), width=1)

    def entity(x, w, name, hp, hp_max, lit=False):
        draw.rectangle((x, 104, x + w, 168), fill=(52, 58, 66, 255) if name == "铁甲战士" else (60, 56, 46, 255),
                       outline=(240, 190, 90, 255) if lit else (100, 96, 84, 255),
                       width=3 if lit else 1)
        font.draw(img, "12", name, x, 84, DIM)
        draw.rectangle((x, 170, x + w, 176), fill=(70, 22, 20, 255))
        draw.rectangle((x + 1, 171, x + 1 + int((w - 2) * hp / hp_max), 175), fill=(176, 56, 46, 255))
        font.draw(img, "12", f"{hp}/{hp_max}", x, 178, INK if not lit else (250, 220, 150))

    entity(36, 56, "铁甲战士", 80, 80)
    enemies = [("酸液史莱姆", 10, 12, "攻击 6"), ("酸液史莱姆", 12, 12, "防御")]
    for index, (name, hp, hp_max, intent) in enumerate(enemies):
        ex = 196 + index * 96
        entity(ex, 68, name, hp, hp_max, lit=target == index)
        font.draw(img, "12", intent, ex + 2, 64, (238, 176, 146) if index == 0 else (170, 200, 230))

    if target is not None:
        ex = 196 + target * 96
        end = (ex + 34, 132)
        for i, (x, y) in enumerate(bezier((200, 234), (250, 186), end)):
            if i % 2 == 0:
                draw.ellipse((x - 1, y - 1, x + 1, y + 1), fill=(250, 200, 110, 235))
    return img


def draw_bottom_screen(font, cards, pages, hand, focus: int, aiming: bool = False) -> Image.Image:
    img = Image.new("RGBA", (BOT_W, BOT_H), (24, 22, 20, 255))
    draw = ImageDraw.Draw(img)
    for y in range(BOT_H):
        t = y / BOT_H
        draw.line((0, y, BOT_W, y), fill=(int(24 + 6 * t), int(22 + 6 * t), int(20 + 5 * t)))

    # Energy orb.
    draw.ellipse((6, 8, 50, 52), fill=(26, 22, 18, 255), outline=(176, 146, 68, 255), width=2)
    font.draw(img, "20", "3/3", 12, 18, (246, 222, 142))
    # Piles.
    font.draw(img, "12", "抽 5", 62, 8, DIM)
    font.draw(img, "12", "弃 0", 62, 26, DIM)
    # End turn.
    round_rect(draw, (226, 8, 314, 44), 5, fill=(74, 40, 36, 255), outline=(170, 96, 84, 255), width=2)
    font.draw(img, "12", "结束回合", 232, 16)

    # Focused card text strip: name + up to 3 lines of 12px body.
    card = cards.get(hand[focus])
    strip_top, strip_bottom = 56, 140
    round_rect(draw, (6, strip_top, 314, strip_bottom), 5, fill=(16, 15, 14, 235),
               outline=(86, 78, 66, 255), width=1)
    font.draw(img, "16", card["name"], 12, strip_top + 2)
    if aiming:
        font.draw(img, "12", "目标：酸液史莱姆 10/12", 176, strip_top + 5, (250, 206, 120))
    text = render_tokens(card.get("text") or [], card)
    y = strip_top + 24
    for line in font.wrap("12", text, 296)[:3]:
        font.draw(img, "12", line, 12, y)
        y += font.info("12")["line_height"]

    # Hand row.
    card_w, card_h = 62, 88
    gap = -2
    total = len(hand) * (card_w + gap) - gap
    x0 = (BOT_W - total) // 2
    for index, card_id in enumerate(hand):
        lift = 0
        if aiming and index == focus:
            continue
        if index == focus:
            lift = 10
        img.alpha_composite(
            draw_card(font, cards, pages, card_id, (card_w, card_h), 38, "12"),
            (x0 + index * (card_w + gap), BOT_H - card_h - 4 - lift),
        )

    if aiming:
        # The raised card stays art+name only: the strip above carries the text.
        raised = draw_card(font, cards, pages, hand[focus], (84, 116), 62, "12")
        img.alpha_composite(raised, (BOT_W // 2 - 42, 118))
    return img


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=HERE)
    ap.add_argument("--font", default=os.path.join(HERE, "assets/font"))
    ap.add_argument("--out", default=os.path.join(HERE, "assets/preview/screens.png"))
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args(argv)

    font = BitmapFont(args.font)
    cards, pages = load_assets(args.root)

    hand = [cid for cid in ["Strike_R", "Defend_R", "Bash", "Anger", "Cleave"] if cid in cards]
    focus = 2

    panels = [
        ("TOP 400x240 · 战斗中", draw_top_screen(font, cards, pages)),
        ("BOTTOM 320x240 · 待机", draw_bottom_screen(font, cards, pages, hand, focus)),
        ("TOP · 拖牌瞄准", draw_top_screen(font, cards, pages, target=0)),
        ("BOTTOM · 拖牌瞄准", draw_bottom_screen(font, cards, pages, hand, focus, aiming=True)),
    ]

    pad, header = 12, 16
    col_w = [TOP_W, BOT_W]
    row_h = [TOP_H, BOT_H]
    width = pad + col_w[0] + pad + col_w[1] + pad
    height = pad + (header + row_h[0] + pad) + (header + row_h[1] + pad)
    canvas = Image.new("RGBA", (width, height), (10, 10, 12, 255))
    draw = ImageDraw.Draw(canvas)

    spots = [
        (pad, pad),
        (pad + col_w[0] + pad, pad),
        (pad, pad + header + row_h[0] + pad),
        (pad + col_w[0] + pad, pad + header + row_h[0] + pad),
    ]
    for (label, panel), (x, y) in zip(panels, spots):
        font.draw(canvas, "12", label, x, y - 14, (150, 150, 150))
        canvas.alpha_composite(panel, (x, y))

    if args.scale != 1:
        canvas = canvas.resize((canvas.width * args.scale, canvas.height * args.scale), Image.NEAREST)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    canvas.save(args.out)
    print(f"wrote {args.out} ({canvas.width}x{canvas.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
