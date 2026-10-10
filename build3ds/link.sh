#!/usr/bin/env bash
# Link a built gate into a .3dsx. usage: link.sh <gate> <smoke-object-basename>
set -eu
export PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH
G=$1; SMOKE=$2; D="$(cd "$(dirname "$0")" && pwd)"
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
cd $D/$G
# rename the smoke runner's main so the 3DS entry owns startup
arm-none-eabi-objcopy --redefine-sym main=smoke_main $SMOKE.o
arm-none-eabi-gcc $ARCH -O2 -I/opt/devkitpro/libctru/include -c $D/main3ds.c -o main3ds.o
arm-none-eabi-g++ -specs=3dsx.specs $ARCH -o $G.elf *.o -L/opt/devkitpro/libctru/lib -lctru -lm
3dsxtool $G.elf $G.3dsx
ls -la $G.3dsx
