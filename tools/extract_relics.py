#!/usr/bin/env python3
"""Extract the relic icons the port uses into one small atlas page.

The game ships one PNG per relic under images/relics/. We take the seven relics
implemented in battle.c, scale each to 32x32 and lay them out 4 per row on a
128x64 page, so the runtime can compute UVs from a fixed grid.

    python tools/extract_relics.py      -> assets/relic_icons.png
"""

from __future__ import annotations

import argparse
import io
import os
import zipfile

from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")

# Order must match the RELIC_* enum in port/source/battle.c.
RELICS = [
    ("burningBlood", ["images/relics/burningBlood.png"]),
    ("vajra", ["images/relics/vajra.png"]),
    ("anchor", ["images/relics/anchor.png"]),
    ("bagOfPreparation", ["images/relics/bagOfPreparation.png", "images/relics/bag_of_prep.png"]),
    ("bloodVial", ["images/relics/bloodVial.png", "images/relics/blood_vial.png"]),
    ("lantern", ["images/relics/lantern.png"]),
    ("marbles", ["images/relics/marbles.png"]),
    ("strawberry", ["images/relics/strawberry.png"]),
    ("orichalcum", ["images/relics/orichalcum.png"]),
    ("redSkull", ["images/relics/red_skull.png"]),
    ("penNib", ["images/relics/penNib.png"]),
    ("nunchaku", ["images/relics/nunchaku.png"]),
    ("artOfWar", ["images/relics/artOfWar.png"]),
    ("ornithopter", ["images/relics/ornithopter.png"]),
    ("goldenIdol", ["images/relics/goldenIdolRelic.png"]),
    ("bronzeScales", ["images/relics/bronzeScales.png"]),
]

# The game ships 128 px relic art (ImageMaster.RELIC_IMG_SIZE); 64 keeps the
# shop's 72 px detail icon and the 128 px page it used to bake at sharp without
# the 512 KB a full 128 px page would cost in VRAM.
ICON = 64
COLS = 4
ROWS = 4


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--out", default=os.path.join(HERE, "assets/relic_icons.png"))
    args = ap.parse_args(argv)

    page = Image.new("RGBA", (COLS * ICON, ROWS * ICON), (0, 0, 0, 0))
    with zipfile.ZipFile(args.jar) as zf:
        names = set(zf.namelist())
        for index, (relic, candidates) in enumerate(RELICS):
            path = next((c for c in candidates if c in names), None)
            if path is None:
                print(f"missing icon for {relic}")
                continue
            icon = Image.open(io.BytesIO(zf.read(path))).convert("RGBA")
            icon = icon.resize((ICON, ICON), Image.LANCZOS)
            x = (index % COLS) * ICON
            y = (index // COLS) * ICON
            page.alpha_composite(icon, (x, y))
            print(f"{relic:18s} {path}")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    page.save(args.out)
    print(f"-> {args.out} ({page.width}x{page.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
