#!/usr/bin/env bash
# Build spike/fontdemo: citro2d + romfs-embedded .t3x glyph atlases.
# No make here (see docs/02-toolchain.md); two compiler calls and one 3dsxtool call.
set -euo pipefail

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
TARGET=fontdemo

CFLAGS="-g -Wall -Wextra -O2 -mword-relocations -ffunction-sections -fdata-sections \
    -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -D__3DS__ -I$DEVKITPRO/libctru/include -I$ROOT/assets/font"
LDFLAGS="-specs=3dsx.specs -g -march=armv6k -mfloat-abi=hard -mtp=soft \
    -L$DEVKITPRO/libctru/lib -Wl,--gc-sections"
LIBS="-lcitro2d -lcitro3d -lctru -lm"

mkdir -p build/romfs
# romfs only carries what the binary asks for; keep it to the three font pages.
cp "$ROOT"/assets/t3x/font_12.t3x "$ROOT"/assets/t3x/font_16.t3x "$ROOT"/assets/t3x/font_20.t3x build/romfs/

# 3dsxtool refuses --romfs unless it can also embed an SMDH, so build one first.
if [ ! -f build/$TARGET.smdh ]; then
    smdhtool --create "STS 3DS" "Slay the Spire, rewired for New 3DS" "local build" \
        "$ROOT/assets/icon48.png" build/$TARGET.smdh "$ROOT/assets/icon24.png"
fi

arm-none-eabi-gcc $CFLAGS -c source/main.c -o build/main.o
arm-none-eabi-g++ $LDFLAGS -o build/$TARGET.elf build/main.o $LIBS
3dsxtool build/$TARGET.elf build/$TARGET.3dsx \
    --smdh=build/$TARGET.smdh --romfs=build/romfs

ls -l build/$TARGET.3dsx
head -c 4 build/$TARGET.3dsx; echo " <- magic"
