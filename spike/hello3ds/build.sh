#!/bin/bash
# Build spike/hello3ds into a .3dsx.
#
# Deliberately make-less: the MSYS2 base image used to bootstrap devkitPro
# has no `make`, and the two compiler invocations are simple enough that
# spelling them out beats installing a whole toolchain dependency.  Once the
# real port starts, this gets replaced by the stock devkitPro Makefile.
#
# Run it from an MSYS2 login shell:
#     bash spike/hello3ds/build.sh

set -euo pipefail

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"

cd "$(dirname "$0")"

TARGET=hello3ds
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
CFLAGS="-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $ARCH -D__3DS__ -I$DEVKITPRO/libctru/include"
LDFLAGS="-specs=3dsx.specs -g $ARCH -L$DEVKITPRO/libctru/lib -Wl,--gc-sections"
LIBS="-lcitro2d -lcitro3d -lctru -lm"

mkdir -p build
arm-none-eabi-gcc $CFLAGS -c source/main.c -o build/main.o
arm-none-eabi-g++ $LDFLAGS -o "build/$TARGET.elf" build/main.o $LIBS
3dsxtool "build/$TARGET.elf" "build/$TARGET.3dsx"

echo "--- built ---"
ls -l "build/$TARGET.3dsx"
printf 'magic: '
head -c 4 "build/$TARGET.3dsx"
echo
