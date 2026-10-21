#!/usr/bin/env python3
"""Render the 48x48 homebrew icon (hbmenu / SMDH) from the extracted card art.

3dsxtool refuses `--romfs` without an SMDH, and an SMDH needs an icon, so this
closes the loop without adding binary files to the repository.
"""

from __future__ import annotations

import argparse
import json
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=HERE)
    ap.add_argument("--card", default="Bash")
    ap.add_argument("--out", default=os.path.join(HERE, "assets/icon48.png"))
    ap.add_argument("--size", type=int, default=48)
    args = ap.parse_args(argv)

    with open(os.path.join(args.root, "assets/data/cards.json"), encoding="utf-8") as fp:
        cards = {card["id"]: card for card in json.load(fp)}
    card = cards[args.card]
    small = args.size // 2

    icon = Image.new("RGBA", (args.size, args.size), (22, 18, 16, 255))
    art = None
    if card.get("art"):
        x, y, w, h = card["art"]["px"]
        page = Image.open(os.path.join(args.root, "assets/atlas", card["art"]["page"]))
        art = page.crop((x, y, x + w, y + h)).convert("RGBA")

    if art is not None:
        side = min(art.size)
        left = (art.width - side) // 2
        top = (art.height - side) // 2
        art = art.crop((left, top, left + side, top + side)).resize((args.size, args.size), Image.LANCZOS)
        icon.alpha_composite(art)

    draw = ImageDraw.Draw(icon)
    draw.rounded_rectangle((0, 0, args.size - 1, args.size - 1), radius=6, outline=(214, 178, 92, 255), width=2)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    icon.save(args.out)
    icon.resize((small, small), Image.LANCZOS).save(
        os.path.join(os.path.dirname(args.out), "icon24.png")
    )
    print(f"wrote {args.out} ({args.size}x{args.size}) and icon{small}.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
