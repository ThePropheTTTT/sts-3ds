#!/usr/bin/env python3
"""Bake the monster sprites the port uses into one 1024x512 page.

Some monsters ship as a single PNG; the rest (every Act 1 small monster) only
exist as Spine skeletons, so they are flattened to a static pose first by
tools/bake_spine.py and then packed here.  Eight 96px columns x four rows
hold 32 sprites; the free cells leave room for the next act's monsters.

    python tools/extract_monsters.py    -> assets/monsters.png (1024x512, 8x4 cells)
"""

from __future__ import annotations

import io
import os
import sys
import zipfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_spine import bake  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")
CELL = 96

# Order is baked into port/source/main.c; keep both in sync.
IMAGES = [
    ("球体守卫", "images/monsters/theCity/sphere/sphericGuardian.png"),
    ("史莱姆老大", "images/monsters/theBottom/boss/slimeKing/slime.png"),
]

SPINE = [
    # Champ also ships a Champ.png, but that file is the page of its Spine atlas
    # (same folder holds skeleton.json/skeleton.atlas), not a standalone sprite.
    ("勇者", "images/monsters/theCity/champ"),
    ("酸液史莱姆", "images/monsters/theBottom/slimeM"),
    ("尖刺史莱姆", "images/monsters/theBottom/slimeAltM"),
    ("颚虫", "images/monsters/theBottom/jawWorm"),
    ("地精大块头", "images/monsters/theBottom/nobGremlin"),
    ("红虱虫", "images/monsters/theBottom/louseRed"),
    ("真菌怪", "images/monsters/theBottom/fungi"),
    ("邪教徒", "images/monsters/theBottom/cultist"),
    ("寄生兽", "images/monsters/theCity/shellMonster"),
    ("异鸟", "images/monsters/theCity/byrd"),
    ("天选者", "images/monsters/theCity/chosen"),
    ("大书怪", "images/monsters/theCity/stabBook"),
    ("牧师", "images/monsters/theCity/healer"),
    ("坦克", "images/monsters/theCity/tank"),
    # Both Slavers are localised "奴隶贩子"; the internal keys pick the sprite.
    ("蓝奴隶贩子", "images/monsters/theBottom/blueSlaver"),
    ("红奴隶贩子", "images/monsters/theBottom/redSlaver"),
    ("奴隶头子", "images/monsters/theCity/slaverMaster"),
    ("铜制机械人偶", "images/monsters/theCity/automaton"),
    ("收藏家", "images/monsters/theCity/collector"),
    ("火炬头", "images/monsters/theCity/torchHead"),
    ("地精首领", "images/monsters/theCity/gremlinleader"),
    ("火大地精", "images/monsters/theBottom/angryGremlin"),
    ("卑鄙地精", "images/monsters/theBottom/thiefGremlin"),
    ("胖地精", "images/monsters/theBottom/fatGremlin"),
    ("持盾地精", "images/monsters/theBottom/femaleGremlin"),
    ("地精法师", "images/monsters/theBottom/wizardGremlin"),
]

# Kept separate and appended after the Spine loop so the earlier sprite indexes
# do not move when a new single-image monster is added.
EXTRA_IMAGES = [
    ("铜球", "images/monsters/theCity/automaton/orb.png"),
]

# Spine bakes appended after the single-image ones so those sprite indexes do
# not move when a new skeleton joins the page.  LouseNormal is the green louse;
# LouseDefensive (baked above as 红虱虫) is the red one.
SPINE_EXTRA = [
    ("虱虫", "images/monsters/theBottom/louseGreen"),
]


def main() -> int:
    page = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))  # power-of-two: 3DS textures need it
    index = 0

    with zipfile.ZipFile(JAR) as zf:
        for key, path in IMAGES:
            im = Image.open(io.BytesIO(zf.read(path))).convert("RGBA")
            bbox = im.getbbox()
            if bbox:
                im = im.crop(bbox)
            scale = min((CELL - 6) / im.width, (CELL - 6) / im.height)
            im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)
            page.alpha_composite(im, ((index % 8) * CELL + (CELL - im.width) // 2,
                                      (index // 8) * CELL + (CELL - im.height) // 2))
            print(f"{key:8s} image {path} {im.width}x{im.height}")
            index += 1

    pages = {}
    for key, folder in SPINE:
        im = bake(folder, pages)
        if im is None:
            print(f"{key}: spine bake failed ({folder})")
            continue
        page.alpha_composite(im, ((index % 8) * CELL + (CELL - im.width) // 2,
                                  (index // 8) * CELL + (CELL - im.height) // 2))
        print(f"{key:8s} spine {folder} {im.width}x{im.height}")
        index += 1

    with zipfile.ZipFile(JAR) as zf:
        for key, path in EXTRA_IMAGES:
            im = Image.open(io.BytesIO(zf.read(path))).convert("RGBA")
            bbox = im.getbbox()
            if bbox:
                im = im.crop(bbox)
            scale = min((CELL - 6) / im.width, (CELL - 6) / im.height)
            im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)
            page.alpha_composite(im, ((index % 8) * CELL + (CELL - im.width) // 2,
                                      (index // 8) * CELL + (CELL - im.height) // 2))
            print(f"{key:8s} image {path} {im.width}x{im.height}")
            index += 1

    for key, folder in SPINE_EXTRA:
        im = bake(folder, pages)
        if im is None:
            print(f"{key}: spine bake failed ({folder})")
            continue
        page.alpha_composite(im, ((index % 8) * CELL + (CELL - im.width) // 2,
                                  (index // 8) * CELL + (CELL - im.height) // 2))
        print(f"{key:8s} spine {folder} {im.width}x{im.height}")
        index += 1

    out = os.path.join(HERE, "assets", "monsters.png")
    page.save(out)
    print(f"-> {out} ({index} monsters)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())