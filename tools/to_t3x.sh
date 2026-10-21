#!/usr/bin/env bash
# Convert the baked glyph atlases and card art pages into .t3x for the runtime.
# tex3ds -a packs its own atlas from images, so we feed it one finished page at a time.
set -euo pipefail
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH=$DEVKITPRO/tools/bin:$PATH
cd "$(dirname "$0")/.."

out=assets/t3x
mkdir -p "$out"

for size in 12 16 20; do
    # rgba4444, not a4: the alpha-only formats need a custom texenv to tint,
    # while a 4444 page still compresses to ~170 KB because the RGB is constant.
    tex3ds -f rgba4444 -o "$out/font_${size}.t3x" "assets/font/zhs_${size}_0.png"
done

tex3ds -f rgba4444 -o "$out/relic_icons.t3x" assets/relic_icons.png
tex3ds -f rgba4444 -o "$out/visuals.t3x" assets/visuals.png
tex3ds -f rgba4444 -o "$out/monsters.t3x" assets/monsters.png
tex3ds -f rgba4444 -o "$out/combat.t3x" assets/combat.png
tex3ds -f rgba4444 -o "$out/ui.t3x" assets/ui.png
tex3ds -f rgba4444 -o "$out/map_paper.t3x" assets/map_paper.png
for act in act1 act2 act3; do
    tex3ds -f rgba4444 -o "$out/${act}.t3x" "assets/${act}.png"
done

for page in assets/atlas/cards_*.png; do
    name=$(basename "$page" .png)
    tex3ds -f rgba4444 -o "$out/${name}.t3x" "$page"
done

ls -l "$out"
