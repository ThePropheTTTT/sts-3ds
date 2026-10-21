#!/usr/bin/env python3
"""Turn a Slay the Spire install into assets a 3DS homebrew can consume.

    python tools/extract.py --jar /path/to/desktop-1.0.jar --out assets

What comes out (see docs/01-data-pipeline.md):

  assets/atlas/cards_NNN.png    card art repacked into <=1024px pages
  assets/atlas/cards.atlas      matching libGDX-style index (debug aid)
  assets/data/cards.json        id -> names, tokenized text, stats, UV rects
  assets/data/cards.csv         the same table, flattened, for eyeballing
  assets/data/l10n/*.json       ui / keywords / relics / monsters strings
  assets/fonts/*.otf            the CJK font the game ships
  assets/manifest.json          build provenance + coverage numbers
  assets/report.txt             anything that did not extract cleanly

Nothing here touches audio, shaders or the Java logic; the C port owns the
rules and uses this table only for numbers, names, text tokens and atlas rects.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image  # noqa: E402

from sts_assets import (Jar, card_class_paths, card_group, parse_atlas,  # noqa: E402
                        read_card_meta, tokenize_desc)

DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")
CARD_GROUPS = ("red", "green", "blue", "purple", "colorless", "curse", "curses", "status")
L10N_FILES = ("ui", "keywords", "relics", "monsters", "powers", "card_types")

# colour name used by the CardColor enum -> short key used in our json
COLOR_KEY = {
    "RED": "red", "GREEN": "green", "BLUE": "blue",
    "PURPLE": "purple", "COLORLESS": "colorless", "CURSE": "curse",
    "STATUS": "status",
}


# --------------------------------------------------------------------------
# page packing
# --------------------------------------------------------------------------


class ShelfPacker:
    """Greedy shelf packer: enough for a one-shot asset bake, and debuggable."""

    def __init__(self, size=1024, pad=2):
        self.size = size
        self.pad = pad
        self.pages = []      # list of dicts: {"rows": [...], "y": int}
        self.placed = {}     # key -> (page_index, x, y)

    def add(self, key, w, h):
        pad = self.pad
        for idx, page in enumerate(self.pages):
            for row in page["rows"]:
                if row["h"] >= h and row["used"] + w + pad <= self.size:
                    self.placed[key] = (idx, row["used"], row["y"])
                    row["used"] += w + pad
                    return self.placed[key]
            if page["y"] + h + pad <= self.size:
                self.placed[key] = (idx, pad, page["y"])
                page["rows"].append({"y": page["y"], "h": h, "used": pad + w + pad})
                page["y"] += h + pad
                return self.placed[key]
        self.pages.append({"rows": [], "y": pad})
        return self.add(key, w, h)

    def page_of(self, key):
        return self.placed[key]


# --------------------------------------------------------------------------
# extraction
# --------------------------------------------------------------------------


def file_sha1(path, chunk=1 << 20):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def build_cards(jar, groups, lang, log):
    atlas_text = jar.text("cards/cards.atlas")
    pages, regions = parse_atlas(atlas_text)
    log(f"atlas: {len(pages)} pages, {len(regions)} regions")

    l10n = jar.json(f"localization/{lang}/cards.json")
    log(f"localization/{lang}/cards.json: {len(l10n)} entries")

    metas = []
    failures = []
    for path in card_class_paths(jar, set(groups)):
        cls = path.rsplit("/", 1)[-1]
        if cls.startswith("DEPRECATED"):
            continue  # cut content, never shown in game
        try:
            metas.append((card_group(path), read_card_meta(jar, path)))
        except Exception as e:  # noqa: BLE001 - report, do not abort the bake
            failures.append((cls, f"{type(e).__name__}: {e}"))
    log(f"card classes: {len(metas)} ok, {len(failures)} unreadable")

    cards = []
    missing_text = []
    missing_art = []
    for group, m in metas:
        entry = l10n.get(m.card_id)
        if entry is None:
            missing_text.append(m.card_id)
            continue
        region = regions.get(m.img)
        if region is None:
            missing_art.append((m.card_id, m.img))
        cards.append({
            "id": m.card_id,
            "class": m.class_name.rsplit("/", 1)[-1],
            "group": group,
            "color": COLOR_KEY.get(m.color, m.color),
            "type": m.type,
            "rarity": m.rarity,
            "target": m.target,
            "cost": m.cost,
            "damage": m.damage,
            "block": m.block,
            "magic": m.magic,
            "name": entry.get("NAME", ""),
            "text": tokenize_desc(entry.get("DESCRIPTION", "")),
            "art_region": m.img,
            "_region": region,
        })
    return cards, regions, pages, failures, missing_text, missing_art


def card_group_from(meta, groups):
    for g in groups:
        if meta.img.startswith(g + "/"):
            return g
    return ""


def bake_art(jar, cards, regions, out_dir, scale, page_size):
    """Crop each card's art, downscale it, and repack into new atlas pages."""
    packer = ShelfPacker(size=page_size, pad=2)
    wanted = []
    for card in cards:
        region = card.pop("_region", None)
        if region is None:
            card["art"] = None
            continue
        w = max(1, round(region.w * scale))
        h = max(1, round(region.h * scale))
        card["art_size"] = [w, h]
        # The crop is the trimmed art, so remember where it sits inside the
        # 250x190 portrait canvas AbstractCard draws it into: (left, top, w, h)
        # with top measured downwards from the canvas top.
        card["art_trim"] = [
            region.offset[0],
            region.orig[1] - region.offset[1] - region.h,
            region.w,
            region.h,
        ]
        wanted.append((card["id"], region, w, h))

    # tallest first keeps the shelves tight
    wanted.sort(key=lambda t: (-t[3], -t[2]))
    for cid, _region, w, h in wanted:
        packer.add(cid, w, h)

    out_pages = [Image.new("RGBA", (page_size, page_size), (0, 0, 0, 0))
                 for _ in packer.pages]
    source_cache = {}
    for cid, region, w, h in wanted:
        img = source_cache.get(region.page)
        if img is None:
            img = jar.image("cards/" + region.page)
            source_cache[region.page] = img
        x, y, rw, rh = region.box()
        crop = img.crop((x, y, x + rw, y + rh)).resize((w, h), Image.LANCZOS)
        page_i, px, py = packer.page_of(cid)
        out_pages[page_i].paste(crop, (px, py))

    os.makedirs(out_dir, exist_ok=True)
    names = []
    for i, img in enumerate(out_pages):
        name = f"cards_{i:03d}.png"
        img.save(os.path.join(out_dir, name))
        names.append(name)

    atlas_lines = []
    for i, name in enumerate(names):
        atlas_lines += [name, f"size: {page_size},{page_size}",
                        "format: RGBA8888", "filter: Linear,Linear",
                        "repeat: none"]
        for cid, _region, w, h in wanted:
            page_i, px, py = packer.page_of(cid)
            if page_i != i:
                continue
            atlas_lines += [cid, "  rotate: false",
                            f"  xy: {px}, {py}", f"  size: {w}, {h}",
                            f"  orig: {w}, {h}", "  offset: 0, 0", "  index: -1"]
    with open(os.path.join(out_dir, "cards.atlas"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(atlas_lines) + "\n")

    for card in cards:
        if card.get("art") is None and card.get("art_size"):
            card["art"] = None
    size = page_size
    for card in cards:
        cid = card["id"]
        if cid not in packer.placed:
            card["art"] = None
            continue
        page_i, px, py = packer.page_of(cid)
        w, h = card["art_size"]
        card["art"] = {
            "page": names[page_i],
            "uv": [round(px / size, 5), round(py / size, 5),
                   round((px + w) / size, 5), round((py + h) / size, 5)],
            "px": [px, py, w, h],
            "trim": card.pop("art_trim", [0, 0, 0, 0]),
        }
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Extract Slay the Spire assets for a 3DS homebrew port.")
    ap.add_argument("--jar", default=DEFAULT_JAR, help="path to desktop-1.0.jar")
    ap.add_argument("--out", default="assets", help="output directory")
    ap.add_argument("--lang", default="zhs", help="localization language code")
    ap.add_argument("--scale", type=float, default=0.5,
                    help="card art scale factor (default 0.5)")
    ap.add_argument("--page", type=int, default=1024,
                    help="output page size in px (PICA max is 1024)")
    ap.add_argument("--groups", default="red,colorless,curse,curses,status",
                    help="card colour packages to include, or 'all'")
    ap.add_argument("--fonts", default="SourceHanSerifSC-Medium.otf,SourceHanSerifSC-Bold.otf,04b03.ttf",
                    help="comma separated font file names to copy out of the jar")
    ap.add_argument("--limit", type=int, default=0, help="dev: stop after N cards")
    args = ap.parse_args(argv)

    t0 = time.time()

    def log(msg):
        print(f"[extract] {msg}", flush=True)

    jar = Jar(args.jar)
    log(f"jar: {args.jar} ({os.path.getsize(args.jar) / 1e6:.1f} MB)")

    groups = list(CARD_GROUPS) if args.groups == "all" else args.groups.split(",")
    unknown = [g for g in groups if g not in CARD_GROUPS]
    if unknown:
        ap.error(f"unknown card groups: {unknown}")

    cards, regions, pages, failures, missing_text, missing_art = build_cards(
        jar, groups, args.lang, log)
    if args.limit:
        cards = cards[:args.limit]

    atlas_dir = os.path.join(args.out, "atlas")
    page_names = bake_art(jar, cards, regions, atlas_dir, args.scale, args.page)
    log(f"packed {sum(1 for c in cards if c.get('art'))} art crops into "
        f"{len(page_names)} page(s) of {args.page}px")

    data_dir = os.path.join(args.out, "data")
    os.makedirs(data_dir, exist_ok=True)
    cards_out = os.path.join(data_dir, "cards.json")
    with open(cards_out, "w", encoding="utf-8", newline="\n") as f:
        # one card per line: diffable, and much smaller than indent=1
        f.write("[\n")
        f.write(",\n".join(json.dumps(c, ensure_ascii=False, sort_keys=True)
                           for c in cards))
        f.write("\n]\n")
    log(f"wrote {cards_out} ({len(cards)} cards)")

    csv_out = os.path.join(data_dir, "cards.csv")
    cols = ["id", "color", "type", "rarity", "cost", "damage", "block",
            "magic", "name", "text"]
    with open(csv_out, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for c in cards:
            row = dict(c)
            row["text"] = "".join(t[1] if t[0] != "n" else "\\n"
                                  for t in c["text"])
            w.writerow([row.get(k, "") for k in cols])

    l10n_dir = os.path.join(data_dir, "l10n")
    os.makedirs(l10n_dir, exist_ok=True)
    for name in L10N_FILES:
        src = f"localization/{args.lang}/{name}.json"
        if not jar.has(src):
            continue
        with open(os.path.join(l10n_dir, f"{name}.json"), "w",
                  encoding="utf-8", newline="\n") as f:
            json.dump(jar.json(src), f, ensure_ascii=False, indent=1, sort_keys=True)
            f.write("\n")

    font_dir = os.path.join(args.out, "fonts")
    os.makedirs(font_dir, exist_ok=True)
    wanted_fonts = [n.strip() for n in args.fonts.split(",") if n.strip()]
    copied = []
    for match in wanted_fonts:
        hit = next((n for n in jar.names() if os.path.basename(n) == match), None)
        if hit is None:
            log(f"font not found in jar: {match}")
            continue
        with open(os.path.join(font_dir, match), "wb") as f:
            f.write(jar.read(hit))
        copied.append(match)
    if copied:
        log(f"fonts: {', '.join(copied)}")

    report = os.path.join(args.out, "report.txt")
    with open(report, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"groups: {','.join(groups)}\n")
        f.write(f"cards: {len(cards)}\n")
        f.write(f"unreadable classes: {len(failures)}\n")
        for cls, err in failures:
            f.write(f"  {cls}: {err}\n")
        f.write(f"no localized text: {len(missing_text)}\n")
        for cid in missing_text:
            f.write(f"  {cid}\n")
        f.write(f"no art region: {len(missing_art)}\n")
        for cid, img in missing_art:
            f.write(f"  {cid} -> {img}\n")

    manifest = {
        "tool": "tools/extract.py",
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "jar": {"path": os.path.abspath(args.jar),
                "sha1": file_sha1(args.jar),
                "size": os.path.getsize(args.jar)},
        "lang": args.lang,
        "groups": groups,
        "art_scale": args.scale,
        "page_size": args.page,
        "pages": page_names,
        "cards": len(cards),
        "counts": {
            "unreadable_classes": len(failures),
            "no_localized_text": len(missing_text),
            "no_art": len(missing_art),
        },
        "seconds": round(time.time() - t0, 1),
    }
    with open(os.path.join(args.out, "manifest.json"), "w",
              encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
        f.write("\n")

    log(f"coverage: {len(cards)} cards, {len(missing_art)} without art, "
        f"{len(missing_text)} without text")
    log(f"done in {manifest['seconds']}s -> {os.path.abspath(args.out)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
