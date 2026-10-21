#!/usr/bin/env python3
"""Extract the real map art (DungeonMap/MapRoomNode) into one 1024 page.

Everything the port needs to draw the act map exactly like the game:

* images/ui/map/<kind>.png        -- the 128x128 room icons (white art)
* images/ui/map/<kind>Outline.png -- the paper-coloured halo drawn behind them
* images/ui/map/circle5.png       -- the 192x192 ring around visited/current nodes
* images/ui/map/dot1.png          -- the MapDot particle that draws the paths
* images/ui/map/boss/slime.png    -- act 1's boss portrait (+ bossOutline/)
* images/ui/map/mapMid.png        -- the parchment; the port crops its middle

The event screen (GenericEventDialog) and its option bars come from the same
jar:

* images/ui/event/panel.png       -- the 1763x806 dialog plate
* images/ui/event/imgFrame.png    -- the 610x610 frame around the picture
* images/ui/event/enabledButton.png -- the 890x77 option bar
* images/events/*.jpg             -- the 600x600 event pictures

The game tints these with sb.setColor() (MapRoomNode.render, DungeonMap.renderBossIcon),
and citro2d has no tint argument, so the colours are baked here instead:

* icon        AVAILABLE_COLOR (0.09, 0.13, 0.17)  -- visited, current and reachable
* icon faded  NOT_TAKEN_COLOR (0.34, 0.34, 0.34)  -- not reachable yet
* outline     OUTLINE_COLOR   (0.55, 0.55, 0.50)  -- the paper colour, a halo
* ring        AVAILABLE_COLOR behind MAP_CIRCLE_5
* dot         NOT_TAKEN_COLOR (MapEdge.color)
* boss        NOT_TAKEN_COLOR over the bossOutline's own grey shading

    python tools/extract_ui.py   -> assets/ui.png, port/source/ui_gen.h
"""

from __future__ import annotations

import argparse
import os
import re

from PIL import Image

from sts_assets import Jar

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")

PAGE = 1024
ICON = 128
RING = 192
BOSS = 256
PAPER_W, PAPER_H = 400, 240
# mapMid.png is 1920x1080 with the parchment only in x 320..1601; the port shows
# that sheet filling the top screen, so everything else is scaled by
# TOP_W / PAPER_SRC_W (1281 design px wide).
PAPER_SRC = (320, 156, 1601, 924)

# Order must match STS_MAP_ICON_* in port/source/ui_gen.h, which follows the
# NODE_* enum in port/source/run.h (monster, elite, rest, shop, event, chest).
KINDS = ["monster", "elite", "rest", "shop", "event", "chest"]

AVAILABLE = (23, 33, 43)
NOT_TAKEN = (87, 87, 87)
OUTLINE = (140, 140, 128)
WHITE = (255, 255, 255)

# Event screen.  The game puts the 600x600 picture inside the plate and the
# 890x77 option bars under it (GenericEventDialog.render :352-:367,
# LargeDialogOptionButton.render); at 320x240 the plate only has 152 px for a
# 12 px CJK font, so the picture moves to the top screen and the bars below the
# plate.
EVENT_PANEL = (320, 152)      # 1763x806 fitted to the screen's width
EVENT_FRAME = 154             # imgFrame.png (610x610)
EVENT_ILL = 152               # images/events/*.jpg (600x600)
EVENT_BTN = (300, 26)         # enabledButton.png (890x77)
# LargeDialogOptionButton.render draws the bar tinted with boxColor (0.4 when
# idle, white when hovered) and then adds a 15% white pass (blend 770,1), so
# the two looks are baked here instead of tinted at run time.
EVENT_BTN_IDLE = (0.4, 0.15)
EVENT_BTN_HOT = (1.0, 0.15)
# Event name -> picture, in the game's own words (BigFish, Cleric, GoopPuddle
# are the three events gen_l10n.py ships).
L10N_HEADER = os.path.join(HERE, "port/source/l10n_gen.h")
EVENT_PICTURES = {
    "大鱼": "fishing.jpg",
    "牧师": "cleric.jpg",
    "黏液世界": "goopPuddle.jpg",
}

ICON_BOX = [(i * (ICON + 2), 256) for i in range(6)]
ICON_FADED_BOX = [(i * (ICON + 2), 392) for i in range(6)]
RING_BOX = (800, 256)
RING_SEL_BOX = (800, 256 + RING + 8)
DOT_BOX = (800, 256 + 2 * RING + 16)
DOT_NAVY_BOX = (DOT_BOX[0] + dot_gap, DOT_BOX[1]) if False else (DOT_BOX[0] + 24, DOT_BOX[1])
BOSS_BOX = (0, 530)
PAPER_BOX = (0, 0)
# The event art is packed into the free band under the boss portrait: the page
# is 1024 rows and the boss ends at 786.
# Shop screen.  ShopScreen.render (:1110-:1127) stretches the rug over the
# whole screen, then draws the cards, relics, potions and the card-removal
# service; the port draws that scene on the top screen at 400/1920 scale, so
# the rug is baked straight at the screen size.  The purge card and the sale
# tag are bbox crops (purge/zhs.png is a 512 sheet with the card in the middle,
# sale_tag/zhs.png a 128 px tag rotated in the art itself).
SHOP_RUG = (400, 240)          # images/npcs/rug/zhs.png (1920x1136, stretched)
SHOP_PURGE = (94, 132)         # 2x its 47x66 screen size, for the detail screen
SHOP_TAG = (20, 18)            # sale_tag/zhs.png, 20 px in the scene
SHOP_GOLD = (26, 22)           # gold.png (64x64 box, coin 44x37), drawn at 13x11

EVENT_PANEL_BOX = (0, 790)
EVENT_FRAME_BOX = (324, 790)
EVENT_ILL_BOX = [(488 + i * 156, 790) for i in range(3)]
EVENT_BTN_BOX = [(0, 956), (0, 988)]
# Right of the parchment, above the room icons (which start at y 256).
SHOP_RUG_BOX = (404, 8)
SHOP_PURGE_BOX = (808, 8)
SHOP_SOLD_BOX = (908, 8)
SHOP_TAG_BOX = (808, 144)
SHOP_GOLD_BOX = (836, 144)


def tinted(im: Image.Image, color) -> Image.Image:
    """Give a white sprite a flat colour, keeping its alpha."""
    out = Image.new("RGBA", im.size, color + (255,))
    out.putalpha(im.getchannel("A"))
    return out


def scaled(im: Image.Image, k: float, lift: float = 0.0) -> Image.Image:
    """Multiply the colour by k, add a flat white lift, keep the alpha.

    LargeDialogOptionButton.render tints the bar with boxColor and then blends a
    15% white pass over it (sb.setBlendFunction(770, 1)); citro2d cannot tint, so
    the two resulting looks are baked instead."""
    r, g, b, a = im.split()
    f = lambda v: max(0, min(255, int(round(v * k + lift * 255.0))))
    return Image.merge("RGBA", (r.point(f), g.point(f), b.point(f), a))


def cropped(im: Image.Image) -> Image.Image:
    """Trim the transparent border off a sheet (the art is not centred in it)."""
    box = im.getchannel("A").getbbox()
    return im.crop(box) if box else im


def event_names(header: str):
    """Event names in sts_events order (port/source/l10n_gen.h)."""
    with open(header, encoding="utf-8") as fh:
        text = fh.read()
    start = text.index("sts_events[] = {")
    return [m.group(1) for m in re.finditer(r'\n\s*\{"([^"]+)"', text[start:])]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--out", default=os.path.join(HERE, "assets/ui.png"))
    ap.add_argument("--header", default=os.path.join(HERE, "port/source/ui_gen.h"))
    args = ap.parse_args(argv)

    page = Image.new("RGBA", (PAGE, PAGE), (0, 0, 0, 0))
    jar = Jar(args.jar)

    def load(name):
        return jar.image(name).convert("RGBA")

    for i, kind in enumerate(KINDS):
        icon = load(f"images/ui/map/{kind}.png")
        outline = load(f"images/ui/map/{kind}Outline.png")
        for box, color, tag in ((ICON_BOX[i], AVAILABLE, "navy"),
                                (ICON_FADED_BOX[i], NOT_TAKEN, "gray")):
            tile = Image.new("RGBA", icon.size, (0, 0, 0, 0))
            tile.alpha_composite(tinted(outline, OUTLINE))
            tile.alpha_composite(tinted(icon, color))
            page.alpha_composite(tile, box)
            print(f"map {kind:8s} {tag:4s} -> {box[0]},{box[1]} {ICON}x{ICON}")

    ring = load("images/ui/map/circle5.png")
    page.alpha_composite(tinted(ring, AVAILABLE), RING_BOX)
    page.alpha_composite(tinted(ring, (240, 190, 90)), RING_SEL_BOX)
    print(f"circle5 navy/selection  -> {RING_BOX[0]},{RING_BOX[1]} {RING}x{RING}")

    dot = load("images/ui/map/dot1.png")
    page.alpha_composite(tinted(dot, NOT_TAKEN), DOT_BOX)
    page.alpha_composite(tinted(dot, AVAILABLE), DOT_NAVY_BOX)
    print(f"dot1 gray/navy          -> {DOT_BOX[0]},{DOT_BOX[1]} {dot.width}x{dot.height}")

    boss = load("images/ui/map/boss/slime.png")
    boss_outline = load("images/ui/map/bossOutline/slime.png")
    tile = Image.new("RGBA", (BOSS, BOSS), (0, 0, 0, 0))
    # DungeonMap.renderBossIcon tints the outline white, which is a multiply:
    # the art keeps its own grey shading.  Flattening it to white made the
    # boss much brighter than the parchment around it.
    tile.alpha_composite(boss_outline.resize((BOSS, BOSS), Image.LANCZOS))
    tile.alpha_composite(tinted(boss, NOT_TAKEN).resize((BOSS, BOSS), Image.LANCZOS))
    page.alpha_composite(tile, BOSS_BOX)
    print(f"boss slime              -> {BOSS_BOX[0]},{BOSS_BOX[1]} {BOSS}x{BOSS}")

    # The parchment: crop a 5:3 slice out of mapMid so the torn side edges show,
    # then scale it to the top screen.
    mid = load("images/ui/map/mapMid.png")
    crop = mid.crop(PAPER_SRC).resize((PAPER_W, PAPER_H), Image.LANCZOS)
    page.alpha_composite(crop, PAPER_BOX)
    print(f"mapMid paper            -> {PAPER_BOX[0]},{PAPER_BOX[1]} {PAPER_W}x{PAPER_H}")

    # --- event screen -------------------------------------------------------
    panel = load("images/ui/event/panel.png")
    page.alpha_composite(panel.resize(EVENT_PANEL, Image.LANCZOS), EVENT_PANEL_BOX)
    print(f"event panel             -> {EVENT_PANEL_BOX} {EVENT_PANEL[0]}x{EVENT_PANEL[1]}")

    frame = load("images/ui/event/imgFrame.png")
    page.alpha_composite(frame.resize((EVENT_FRAME,) * 2, Image.LANCZOS), EVENT_FRAME_BOX)
    print(f"event frame             -> {EVENT_FRAME_BOX} {EVENT_FRAME}x{EVENT_FRAME}")

    # The pictures follow sts_events order, so read it from the generated table
    # rather than trusting the order they happen to be listed in here.
    names = event_names(L10N_HEADER)
    if len(names) != len(EVENT_ILL_BOX):
        raise SystemExit(f"{len(names)} events but {len(EVENT_ILL_BOX)} picture slots")
    for i, name in enumerate(names):
        picture = EVENT_PICTURES.get(name)
        if picture is None:
            raise SystemExit(f"no picture for event {name!r}: add it to EVENT_PICTURES")
        art = load(f"images/events/{picture}")
        page.alpha_composite(art.resize((EVENT_ILL,) * 2, Image.LANCZOS), EVENT_ILL_BOX[i])
        print(f"event art {name:8s}     -> {EVENT_ILL_BOX[i]} {EVENT_ILL}x{EVENT_ILL} ({picture})")

    bar = load("images/ui/event/enabledButton.png")
    for i, (k, lift) in enumerate((EVENT_BTN_IDLE, EVENT_BTN_HOT)):
        page.alpha_composite(scaled(bar, k, lift).resize(EVENT_BTN, Image.LANCZOS), EVENT_BTN_BOX[i])
    print(f"event bar idle/hot      -> {EVENT_BTN_BOX[0]}/{EVENT_BTN_BOX[1]} {EVENT_BTN[0]}x{EVENT_BTN[1]}")

    # --- shop screen --------------------------------------------------------
    rug = load("images/npcs/rug/zhs.png")
    page.alpha_composite(rug.resize(SHOP_RUG, Image.LANCZOS), SHOP_RUG_BOX)
    print(f"shop rug                -> {SHOP_RUG_BOX} {SHOP_RUG[0]}x{SHOP_RUG[1]}")

    for name, src, box in (("purge", "images/npcs/purge/zhs.png", SHOP_PURGE_BOX),
                           ("sold", "images/npcs/sold_out/zhs.png", SHOP_SOLD_BOX)):
        art = cropped(load(src)).resize(SHOP_PURGE, Image.LANCZOS)
        page.alpha_composite(art, box)
        print(f"shop {name:8s}          -> {box} {SHOP_PURGE[0]}x{SHOP_PURGE[1]}")

    tag = cropped(load("images/npcs/sale_tag/zhs.png")).resize(SHOP_TAG, Image.LANCZOS)
    page.alpha_composite(tag, SHOP_TAG_BOX)
    print(f"sale tag                -> {SHOP_TAG_BOX} {SHOP_TAG[0]}x{SHOP_TAG[1]}")

    gold = cropped(load("images/ui/topPanel/gold.png")).resize(SHOP_GOLD, Image.LANCZOS)
    page.alpha_composite(gold, SHOP_GOLD_BOX)
    print(f"gold coin               -> {SHOP_GOLD_BOX} {SHOP_GOLD[0]}x{SHOP_GOLD[1]}")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    page.save(args.out)
    print(f"-> {args.out} ({page.width}x{page.height})")

    header = [
        "/* Generated by tools/extract_ui.py -- do not edit. */",
        "#pragma once",
        "",
        f"#define STS_UI_PAGE {PAGE}",
        "",
        "typedef struct { short x, y; unsigned short w, h; } StsUiRect;",
        "",
        "/* Room icons, index = NODE_* - 1 (run.h): the game paints them",
        " * AVAILABLE_COLOR when reachable and NOT_TAKEN_COLOR otherwise. */",
        "enum { STS_MAP_ICON_MONSTER = 0, STS_MAP_ICON_ELITE, STS_MAP_ICON_REST,",
        "       STS_MAP_ICON_SHOP, STS_MAP_ICON_EVENT, STS_MAP_ICON_CHEST,",
        "       STS_MAP_ICON_COUNT };",
        "static const StsUiRect sts_ui_icon[STS_MAP_ICON_COUNT] = {",
    ]
    for i, kind in enumerate(KINDS):
        header.append("    {%d, %d, %d, %d}, /* %s */" % (ICON_BOX[i] + (ICON, ICON, kind)))
    header.append("};")
    header.append("static const StsUiRect sts_ui_icon_faded[STS_MAP_ICON_COUNT] = {")
    for i, kind in enumerate(KINDS):
        header.append("    {%d, %d, %d, %d}, /* %s */" % (ICON_FADED_BOX[i] + (ICON, ICON, kind)))
    header.append("};")
    header.append("")
    header.append("/* MAP_CIRCLE_5 (192x192): ring around the current/visited node. */")
    header.append("static const StsUiRect sts_ui_ring = {%d, %d, %d, %d};" % (RING_BOX + (RING, RING)))
    header.append("/* Same ring in the port's selection colour. */")
    header.append("static const StsUiRect sts_ui_ring_sel = {%d, %d, %d, %d};" % (RING_SEL_BOX + (RING, RING)))
    header.append("")
    header.append("/* dot1 (16x16): MapDot, the dotted path between rooms. */")
    header.append("static const StsUiRect sts_ui_dot = {%d, %d, %d, %d};" % (DOT_BOX + (dot.width, dot.height)))
    header.append("static const StsUiRect sts_ui_dot_navy = {%d, %d, %d, %d};" % (DOT_NAVY_BOX + (dot.width, dot.height)))
    header.append("")
    header.append("/* Act 1 boss portrait (512x512 art at %d). */" % BOSS)
    header.append("static const StsUiRect sts_ui_boss = {%d, %d, %d, %d};" % (BOSS_BOX + (BOSS, BOSS)))
    header.append("")
    header.append("/* mapMid.png, the parchment, cropped to the sheet and fitted to")
    header.append(" * the top screen.  PAPER_SRC_W is its width in the game's px. */")
    header.append("static const StsUiRect sts_ui_paper = {%d, %d, %d, %d};" % (PAPER_BOX + (PAPER_W, PAPER_H)))
    header.append("#define STS_UI_PAPER_SRC_W %d" % (PAPER_SRC[2] - PAPER_SRC[0]))
    header.append("")
    header.append("/* Event screen (GenericEventDialog): the 1763x806 plate, the 610x610")
    header.append(" * picture frame and the 890x77 option bar.  The bar is baked twice --")
    header.append(" * idle (boxColor 0.4 plus a 15% white pass) and highlighted (white")
    header.append(" * plus that pass) -- because citro2d cannot tint. */")
    header.append("static const StsUiRect sts_ui_event_panel = {%d, %d, %d, %d};" % (EVENT_PANEL_BOX + EVENT_PANEL))
    header.append("static const StsUiRect sts_ui_event_frame = {%d, %d, %d, %d};" % (EVENT_FRAME_BOX + (EVENT_FRAME, EVENT_FRAME)))
    header.append("#define STS_EVENT_ILL_COUNT %d" % len(EVENT_ILL_BOX))
    header.append("static const StsUiRect sts_ui_event_ill[STS_EVENT_ILL_COUNT] = {")
    for i, name in enumerate(names):
        header.append("    {%d, %d, %d, %d}, /* %s */" % (EVENT_ILL_BOX[i] + (EVENT_ILL, EVENT_ILL, name)))
    header.append("};")
    header.append("static const StsUiRect sts_ui_event_btn[2] = {")
    header.append("    {%d, %d, %d, %d}, /* idle */" % (EVENT_BTN_BOX[0] + EVENT_BTN))
    header.append("    {%d, %d, %d, %d}, /* highlighted */" % (EVENT_BTN_BOX[1] + EVENT_BTN))
    header.append("};")
    header.append("")
    header.append("/* Shop screen (ShopScreen.render): the rug stretched over the top")
    header.append(" * screen, the 512 px card-removal sheet cropped to the card, the")
    header.append(" * sold-out sheet in its place, the 128 px sale tag and the 64 px")
    header.append(" * gold coin (GOLD_IMG_WIDTH = 64, GOLD_IMG_OFFSET (-50,-215)). */")
    header.append("static const StsUiRect sts_ui_shop_rug = {%d, %d, %d, %d};" % (SHOP_RUG_BOX + SHOP_RUG))
    header.append("static const StsUiRect sts_ui_shop_purge = {%d, %d, %d, %d};" % (SHOP_PURGE_BOX + SHOP_PURGE))
    header.append("static const StsUiRect sts_ui_shop_sold = {%d, %d, %d, %d};" % (SHOP_SOLD_BOX + SHOP_PURGE))
    header.append("static const StsUiRect sts_ui_shop_tag = {%d, %d, %d, %d};" % (SHOP_TAG_BOX + SHOP_TAG))
    header.append("static const StsUiRect sts_ui_shop_gold = {%d, %d, %d, %d};" % (SHOP_GOLD_BOX + SHOP_GOLD))
    header.append("")

    os.makedirs(os.path.dirname(args.header), exist_ok=True)
    with open(args.header, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(header))
    print(f"-> {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())