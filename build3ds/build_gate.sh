#!/usr/bin/env bash
# Cross-compile one port gate slice for 3DS (ARM11 MPCore) with devkitARM.
# usage: build_gate.sh <gate-name> <extra port files...>
set -u
export PATH=/opt/devkitpro/devkitARM/bin:$PATH
PORT="$(cd "$(dirname "$0")/.." && pwd)/port"
GATE=$1; shift
OUT="$(cd "$(dirname "$0")" && pwd)/$GATE"
rm -rf "${OUT:?}"; mkdir -p "$OUT"
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -D__3DS__"
FLAGS="$ARCH -O2 -g -DSM64DS_PLATFORM_PC -I$PORT -I$PORT/../include -I/opt/devkitpro/libctru/include -fno-strict-aliasing -Wno-narrowing"
cd "$PORT"
SRCS=$(grep -vE '^\s*(#|$)' "slice_${GATE}.txt" | sed 's|^|../|')
fails=0
for f in "$@" $SRCS; do
  b=$(echo "$f" | tr '/.' '__')
  case $f in
    *.c) if head -1 "$f" | grep -qx '//cpp'; then CC=(arm-none-eabi-g++ -std=gnu++17 -x c++); else CC=(arm-none-eabi-gcc -std=gnu11); fi;;
    *) CC=(arm-none-eabi-g++ -std=gnu++17);;
  esac
  "${CC[@]}" $FLAGS -c "$f" -o "$OUT/$b.o" 2>>"$OUT/err.log" || { fails=$((fails+1)); echo "FAIL $f"; }
done
echo "objects=$(ls $OUT/*.o 2>/dev/null | wc -l) fails=$fails"
