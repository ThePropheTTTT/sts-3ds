#!/usr/bin/env bash
# Build the v0.1 battle prototype.
set -euo pipefail

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
TARGET=sts3ds

# Generated data tables are intentionally not committed.  Fail early with a
# useful message instead of letting gcc report a missing include.
for h in cards_gen.h l10n_gen.h visuals_gen.h combat_gen.h ui_gen.h map_paper_gen.h hud_gen.h; do
    if [ ! -f "source/$h" ]; then
        echo "missing source/$h" >&2
        echo "run the extraction steps in docs/01-data-pipeline.md first" >&2
        exit 1
    fi
done
if [ ! -f "$ROOT/assets/t3x/font_12.t3x" ] || \
   [ ! -f "$ROOT/assets/icon48.png" ] || [ ! -f "$ROOT/assets/icon24.png" ]; then
    echo "missing assets/t3x or icons; run the extraction and tools/to_t3x.sh first" >&2
    exit 1
fi

CFLAGS="-g -Wall -Wextra -O2 -mword-relocations -ffunction-sections -fdata-sections \
    -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -D__3DS__ -I$DEVKITPRO/libctru/include -I$ROOT/assets/font -Isource"
LDFLAGS="-specs=3dsx.specs -g -march=armv6k -mfloat-abi=hard -mtp=soft \
    -L$DEVKITPRO/libctru/lib -Wl,--gc-sections"
LIBS="-lcitro2d -lcitro3d -lctru -lm"

# Self-test flavour: cardtest.c asserts card effects at boot and draws the
# report on the top screen.  Kept out of the shipping build.
EXTRA_OBJS=""
if [ "${CARD_TEST:-0}" = "1" ]; then
    CFLAGS="$CFLAGS -DCARD_TEST"
    if [ -n "${UI_DEMO:-}" ]; then
        CFLAGS="$CFLAGS -DUI_DEMO=$UI_DEMO"
    fi
    TARGET=sts3ds_test
    EXTRA_OBJS="build/cardtest.o"
fi

rm -rf build/romfs
mkdir -p build/romfs
cp "$ROOT"/assets/t3x/font_12.t3x "$ROOT"/assets/t3x/font_16.t3x "$ROOT"/assets/t3x/font_20.t3x build/romfs/
cp "$ROOT"/assets/t3x/cards_*.t3x "$ROOT"/assets/t3x/relic_icons.t3x \
   "$ROOT"/assets/t3x/visuals.t3x "$ROOT"/assets/t3x/act1.t3x \
   "$ROOT"/assets/t3x/act2.t3x "$ROOT"/assets/t3x/act3.t3x \
   "$ROOT"/assets/t3x/monsters.t3x "$ROOT"/assets/t3x/combat.t3x \
   "$ROOT"/assets/t3x/ui.t3x "$ROOT"/assets/t3x/map_paper.t3x \
   "$ROOT"/assets/t3x/hud.t3x build/romfs/

# The extracted PCM is optional at build time: when it is present it goes into
# the 3dsx romfs, so a stock 3dsx carries its own music and effects.  The SD
# card copy remains a fallback (and is still useful for quick audio swaps).
if [ -f "$ROOT/assets/audio/act1.pcm" ]; then
    mkdir -p build/romfs/audio/sfx
    cp "$ROOT"/assets/audio/act1.pcm "$ROOT"/assets/audio/act2.pcm \
       "$ROOT"/assets/audio/act3.pcm build/romfs/audio/
    for f in "$ROOT"/assets/audio/sfx/*.pcm; do
        [ -e "$f" ] || continue
        cp "$f" build/romfs/audio/sfx/
    done
    echo "romfs audio: $(du -sh build/romfs/audio | cut -f1)"
else
    echo "romfs audio: missing assets/audio; building a silent 3dsx" >&2
fi

if [ ! -f build/$TARGET.smdh ]; then
    smdhtool --create "STS 3DS" "Slay the Spire, rewired for New 3DS" "local build" \
        "$ROOT/assets/icon48.png" build/$TARGET.smdh "$ROOT/assets/icon24.png"
fi

OBJS="build/font.o build/battle.o build/run.o build/save.o build/audio.o build/diag.o build/main.o"

arm-none-eabi-gcc $CFLAGS -c source/diag.c -o build/diag.o
arm-none-eabi-gcc $CFLAGS -c source/font.c -o build/font.o
arm-none-eabi-gcc $CFLAGS -c source/battle.c -o build/battle.o
arm-none-eabi-gcc $CFLAGS -c source/run.c -o build/run.o
arm-none-eabi-gcc $CFLAGS -c source/save.c -o build/save.o
arm-none-eabi-gcc $CFLAGS -c source/audio.c -o build/audio.o
arm-none-eabi-gcc $CFLAGS -c source/main.c -o build/main.o
if [ -n "$EXTRA_OBJS" ]; then
    arm-none-eabi-gcc $CFLAGS -c source/cardtest.c -o build/cardtest.o
fi
arm-none-eabi-g++ $LDFLAGS -o build/$TARGET.elf $OBJS $EXTRA_OBJS $LIBS
3dsxtool build/$TARGET.elf build/$TARGET.3dsx --smdh=build/$TARGET.smdh --romfs=build/romfs

ls -l build/$TARGET.3dsx
