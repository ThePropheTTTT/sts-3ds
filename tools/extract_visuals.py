#!/usr/bin/env python3
"""Bake the game's own card faces, act backgrounds and intent icons.

AbstractCard.renderImage() stacks a card like this:

    background (type + colour) -> portrait -> frame (type + rarity) -> banner (rarity)

The runtime replays that stack with separate sprites: one baked background, one
portrait drawn from the card art pages, one frame overlay and one banner.  The
energy badge is the game's own orb, kept at native 1024-atlas resolution and
placed from AbstractCard.renderEnergy's 512-atlas geometry: the orb overhangs
the top-left corner of the card, so it cannot be part of the card crop.

Every card sprite is cut from ONE shared rectangle, measured on the background
layer alone (the alpha > 8 union of the seven backgrounds the port uses).  The
frame and the banner overhang the card slightly, so letting them into the
measurement -- which is what a plain getbbox() on the composed image does --
inflates the rectangle and shrinks the visible card inside its slot.  That was
the "card narrower than its box" bug.

The frame and banner offsets in cardui.atlas are measured bottom-up, so they are
flipped into the 1024 card canvas before flattening.  Getting that flip wrong is
what made earlier card faces look shifted by one layer.

    python tools/extract_visuals.py   -> assets/visuals.png + assets/visuals.json
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import zipfile

from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))

from sts_assets import parse_atlas  # noqa: E402

DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")

PAGE = 1024
CARD = (64, 88)
ALPHA_FLOOR = 8  # ignore the soft drop shadow when measuring the card outline

ACT_BACKGROUNDS = [
    ("act1", "bottomScene/scene.png"),
    ("act2", "cityScene/scene.png"),
    ("act3", "beyondScene/scene.png"),
]

INTENTS = [
    ("intent_attack1", "images/ui/intent/attack/attack_intent_1.png"),
    ("intent_attack3", "images/ui/intent/attack/attack_intent_3.png"),
    ("intent_attack5", "images/ui/intent/attack/attack_intent_5.png"),
    ("intent_attack7", "images/ui/intent/attack/attack_intent_7.png"),
    ("intent_defend", "images/ui/intent/defend.png"),
    ("intent_buff", "images/ui/intent/buff1.png"),
    ("intent_debuff", "images/ui/intent/debuff1.png"),
]

# Backgrounds the port can show: red / colourless x attack, skill, power, plus
# the black skill background curses and statuses use in the game.
BASES = [
    ("red", "attack"),
    ("red", "skill"),
    ("red", "power"),
    ("colorless", "attack"),
    ("colorless", "skill"),
    ("colorless", "power"),
    ("black", "skill"),
]
FRAMES = [(t, r) for t in ("attack", "skill", "power") for r in ("common", "uncommon", "rare")]
BANNERS = ["common", "uncommon", "rare"]
# AbstractCard.renderEnergy picks the orb by CARD COLOUR (not rarity):
# red / green / blue / purple / colourless.  The port only ships red and
# colourless cards today, but all five cost the same to pack.
ORBS = ["red", "green", "blue", "purple", "colorless"]

# The 512 cardui layout the game actually renders: card body 300x420 at
# (106,46) in libGDX bottom-up coordinates.  The 1024 "_L" regions (which the
# port uses for detail) are the same art at 2x with the body at (212,92).
CARD_512 = (106, 46, 300, 420)
CARD_1024_XY = (212, 92)

def visible_bbox(img, floor=ALPHA_FLOOR):
    """Bounding box of the pixels that actually show, ignoring soft shadow."""
    alpha = img.getchannel("A")
    return alpha.point(lambda a: 255 if a > floor else 0).getbbox()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--out", default=os.path.join(HERE, "assets"))
    args = ap.parse_args(argv)

    page_img = Image.new("RGBA", (PAGE, PAGE), (0, 0, 0, 0))
    rects = {}
    base_order, frame_order, banner_order, intent_order = [], [], [], []
    orb_order = []
    banner_box = {}
    orb_place = {}

    # One shelf allocator for the square card sprites, so bases and frames pack
    # tightly without hand-written coordinates.
    slot = {"x": 0, "y": 496}

    def alloc(w, h):
        if slot["y"] + h > 950:
            slot["y"] = 0
            slot["x"] += w + 2
        pos = (slot["x"], slot["y"])
        slot["y"] += h + 2
        return pos

    with zipfile.ZipFile(args.jar) as z:
        # --- act backgrounds: one tall 2048 scene per act, squashed to 400 wide
        for key, path in ACT_BACKGROUNDS:
            im = Image.open(io.BytesIO(z.read(path))).convert("RGB")
            scale = 400.0 / im.width
            im = im.resize((400, max(240, int(im.height * scale))), Image.LANCZOS)
            top = max(0, (im.height - 240) // 2)
            im = im.crop((0, top, 400, top + 240))
            im.save(os.path.join(args.out, key + ".png"))
            print(f"{key}: {path} -> assets/{key}.png")

        # --- card layers, each flattened onto its own 1024 canvas
        pages, regions = parse_atlas(z.read("cardui/cardui.atlas").decode("utf-8"))
        cache = {}

        def layer(region_name, flip):
            region = regions[region_name]
            page = cache.get(region.page)
            if page is None:
                page = Image.open(io.BytesIO(z.read("cardui/" + region.page))).convert("RGBA")
                cache[region.page] = page
            piece = page.crop((region.x, region.y, region.x + region.w, region.y + region.h))
            canvas = Image.new("RGBA", (PAGE, PAGE), (0, 0, 0, 0))
            _, orig_h = region.orig
            top = orig_h - region.offset[1] - piece.height if flip else region.offset[1]
            canvas.alpha_composite(piece, (region.offset[0], top))
            return canvas

        bases = {(c, t): layer("1024/bg_%s_%s" % (t, c), False) for c, t in BASES}
        frames = {key: layer("1024/frame_%s_%s" % key, True) for key in FRAMES}
        banners = {r: layer("1024/banner_%s" % r, True) for r in BANNERS}

        # --- the one shared source rect, measured on the backgrounds alone
        boxes = {key: visible_bbox(img) for key, img in bases.items()}
        for key, box in sorted(boxes.items()):
            if box is None:
                raise SystemExit("background %s is empty" % (key,))
            print("  bg %-4s %-6s visible bbox %s" % (key[0], key[1], box))
        src_x = min(box[0] for box in boxes.values())
        src_y = min(box[1] for box in boxes.values())
        src_r = max(box[2] for box in boxes.values())
        src_b = max(box[3] for box in boxes.values())
        src = (src_x, src_y, src_r - src_x, src_b - src_y)
        print("card source rect (backgrounds only): %d,%d %dx%d" % src)
        # Frame and banner overhang this rect on purpose; they are separate sprites.

        def cut(img):
            return img.crop((src[0], src[1], src[0] + src[2], src[1] + src[3])).resize(
                CARD, Image.LANCZOS
            )

        for color, ctype in BASES:
            key = "base_%s_%s" % (color, ctype)
            pos = alloc(*CARD)
            page_img.alpha_composite(cut(bases[(color, ctype)]), pos)
            rects[key] = [pos[0], pos[1], CARD[0], CARD[1]]
            base_order.append(key)
        for key in FRAMES:
            name = "frame_%s_%s" % key
            pos = alloc(*CARD)
            page_img.alpha_composite(cut(frames[key]), pos)
            rects[name] = [pos[0], pos[1], CARD[0], CARD[1]]
            frame_order.append(name)
        # Banners keep their native 1024 scale and their own overlap with the
        # card, so they can be dropped on top of the portrait at the exact spot.
        for i, rarity in enumerate(BANNERS):
            box = visible_bbox(banners[rarity])
            if box is None:
                raise SystemExit("banner %s is empty" % rarity)
            piece = banners[rarity].crop(box)
            pos = (200, 496 + i * 160)
            page_img.alpha_composite(piece, pos)
            name = "banner_%s" % rarity
            rects[name] = [pos[0], pos[1], piece.width, piece.height]
            banner_box[name] = [box[0], box[1], piece.width, piece.height]
            banner_order.append(name)

        # --- energy orbs: the game's own badges at 1024-atlas resolution.
        # renderHelper (AbstractCard.java:1173) puts a packed region's bottom-left
        # at (offsetX, offsetY) of its original canvas, so in the bottom-up 512
        # card layout the orb spans (offsetX, offsetY)..(+w, +h) while the card
        # body starts at (106, 46) and is 300x420.  Converting that to top-down
        # card-local coordinates and doubling gives the orb rectangle in the 1024
        # canvas the port draws in.
        body_x, body_bottom, _body_w, body_h = CARD_512
        for i, color in enumerate(ORBS):
            reg = regions["1024/card_%s_orb" % color]
            piece = cache[reg.page].crop((reg.x, reg.y, reg.x + reg.w, reg.y + reg.h))
            small = regions["512/card_%s_orb" % color]
            ox = CARD_1024_XY[0] + 2 * (small.offset[0] - body_x)
            oy = CARD_1024_XY[1] + 2 * (body_bottom + body_h - (small.offset[1] + small.h))
            pos = (850, 30 + i * 160)
            page_img.alpha_composite(piece, pos)
            name = "orb_%s" % color
            rects[name] = [pos[0], pos[1], piece.width, piece.height]
            orb_place[name] = [ox, oy, piece.width, piece.height]
            orb_order.append(name)
            print("  orb %-9s canvas %d,%d %dx%d (from 512 offset %s)"
                  % (color, ox, oy, piece.width, piece.height, small.offset))
        # renderEnergy puts the cost text at (-132, 192) from the card centre.
        cost_x = CARD_1024_XY[0] + 2 * (300 // 2 - 132)
        cost_y = CARD_1024_XY[1] + 2 * (420 // 2 - 192)

        print("bases: %d, frames: %d, banners: %d, orbs: %d"
              % (len(base_order), len(frame_order), len(banner_order), len(orb_order)))

        # --- intent icons
        ix, iy = 840, 0
        for key, path in INTENTS:
            im = Image.open(io.BytesIO(z.read(path))).convert("RGBA")
            page_img.alpha_composite(im.resize((24, 24), Image.LANCZOS), (ix, iy))
            rects[key] = [ix, iy, 24, 24]
            intent_order.append(key)
            ix += 26
        print(f"intents: {len(INTENTS)}")

    out_png = os.path.join(args.out, "visuals.png")
    page_img.save(out_png)
    payload = {"regions": rects, "card_src": list(src), "banner_box": banner_box,
               "orb_canvas": orb_place}
    with open(os.path.join(args.out, "visuals.json"), "w", encoding="utf-8") as fp:
        json.dump(payload, fp, ensure_ascii=False, indent=1, sort_keys=True)
    print(f"-> {out_png} ({len(rects)} regions)")

    header = os.path.join(HERE, "port/source/visuals_gen.h")
    lines = [
        "/* Generated by tools/extract_visuals.py -- do not edit. */",
        "#ifndef STS_VISUALS_GEN_H",
        "#define STS_VISUALS_GEN_H",
        "",
        "typedef struct { unsigned short x, y, w, h; } StsRegion;",
        "/* x,y are 1024-card-canvas coordinates; px,py is where the sprite lives. */",
        "typedef struct { short x, y; unsigned short w, h; unsigned short px, py; } StsBanner;",
        "/* Energy orb: x,y,w,h is its card-canvas rectangle, px,py its sprite. */",
        "typedef struct { short x, y; unsigned short w, h; unsigned short px, py; } StsOrb;",
        "",
        "/* The card outline, measured on the background layer only.  Every card",
        " * sprite is a crop of this rectangle, so drawing it at the card box puts",
        " * the visible edge exactly on the box. */",
        "#define STS_CARD_SRC_X %d" % src[0],
        "#define STS_CARD_SRC_Y %d" % src[1],
        "#define STS_CARD_SRC_W %d" % src[2],
        "#define STS_CARD_SRC_H %d" % src[3],
        "#define STS_VIS_CARD_SPR_W %d  /* every card sprite is this size */" % CARD[0],
        "#define STS_VIS_CARD_SPR_H %d" % CARD[1],
        "",
    ]
    for name, keys in (("sts_vis_base", base_order), ("sts_vis_frame", frame_order),
                       ("sts_vis_banner", banner_order), ("sts_vis_intent", intent_order)):
        lines.append("static const StsRegion %s[%d] = {" % (name, max(1, len(keys))))
        for key in keys:
            r = rects[key]
            lines.append("    {%d, %d, %d, %d},  /* %s */" % (r[0], r[1], r[2], r[3], key))
        lines.append("};")
        lines.append("")
    lines += ["static const StsBanner sts_vis_banner_place[%d] = {" % max(1, len(banner_order))]
    for key in banner_order:
        b = banner_box[key]
        r = rects[key]
        lines.append("    {%d, %d, %d, %d, %d, %d},  /* %s */" % (b[0], b[1], b[2], b[3], r[0], r[1], key))
    lines += ["};",
              "",
              "/* renderEnergy (:1924) draws these at the card's top-left corner, overhanging",
              " * it by a few canvas pixels; px,py is the sprite, x,y its canvas origin. */",
              "static const StsOrb sts_vis_orb[%d] = {" % max(1, len(orb_order))]
    for key in orb_order:
        p = orb_place[key]
        r = rects[key]
        lines.append("    {%d, %d, %d, %d, %d, %d},  /* %s */"
                     % (p[0], p[1], p[2], p[3], r[0], r[1], key))
    lines += ["};",
              "/* Cost number centre: renderEnergy (-132, 192) from the card centre. */",
              "#define STS_COST_TXT_X %d" % cost_x,
              "#define STS_COST_TXT_Y %d" % cost_y,
              "#define STS_VIS_BASE_COUNT %d" % len(base_order),
              "#define STS_VIS_FRAME_COUNT %d" % len(frame_order),
              "#define STS_VIS_BANNER_COUNT %d" % len(banner_order),
              "#define STS_VIS_ORB_COUNT %d" % len(orb_order),
              "#define STS_VIS_INTENT_COUNT %d" % len(intent_order),
              "", "#endif", ""]
    with open(header, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(lines))
    print(f"-> {header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
