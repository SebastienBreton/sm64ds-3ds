#!/usr/bin/env bash
# Build the CIA packaging tools from source into build3ds/tools-bin (git-ignored):
#   makerom + ctrtool  https://github.com/3DSGuy/Project_CTR      (MIT)
#   bannertool         https://github.com/diasurgical/bannertool  (MIT, maintained fork)
# Sources are fetched at pinned commits into build3ds/tools-src (git-ignored).
# Needs git, make and a C/C++ compiler (Xcode command line tools / build-essential).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/build3ds/tools-src"
BIN="$ROOT/build3ds/tools-bin"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
JOBS="${JOBS:-4}"

PROJECT_CTR_URL=https://github.com/3DSGuy/Project_CTR
PROJECT_CTR_REV=d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8   # makerom v0.19.0
BANNERTOOL_URL=https://github.com/diasurgical/bannertool
BANNERTOOL_REV=16d8c5a0ce02a5e06e64ab42275132fca57c04a2    # bannertool 1.2.2

die() { echo "error: $*" >&2; exit 1; }
for t in git make "$CC" "$CXX"; do command -v "$t" > /dev/null || die "$t not found"; done

fetch() { # url rev dir
    if [ ! -d "$3/.git" ]; then
        mkdir -p "$3"
        git -C "$3" init -q
        git -C "$3" remote add origin "$1"
    fi
    if [ "$(git -C "$3" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$2" ]; then
        git -C "$3" fetch -q --depth 1 origin "$2"
        git -C "$3" checkout -q --detach FETCH_HEAD
    fi
}

mkdir -p "$SRC" "$BIN"

echo "== makerom / ctrtool ($PROJECT_CTR_REV)"
fetch "$PROJECT_CTR_URL" "$PROJECT_CTR_REV" "$SRC/Project_CTR"
for tool in makerom ctrtool; do
    make -C "$SRC/Project_CTR/$tool" deps -j"$JOBS" > "$SRC/$tool-build.log" 2>&1 ||
        die "$tool dependencies failed, see $SRC/$tool-build.log"
    make -C "$SRC/Project_CTR/$tool" -j"$JOBS" >> "$SRC/$tool-build.log" 2>&1 ||
        die "$tool build failed, see $SRC/$tool-build.log"
    cp "$SRC/Project_CTR/$tool/bin/$tool" "$BIN/"
done

echo "== bannertool ($BANNERTOOL_REV)"
# Its own makefile needs the buildtools submodule, which rejects arm64 hosts:
# the sources are plain C/C++, compile them directly.
fetch "$BANNERTOOL_URL" "$BANNERTOOL_REV" "$SRC/bannertool"
BT="$SRC/bannertool"
OBJ="$BT/out-manual"
mkdir -p "$OBJ"
for f in "$BT"/source/pc/*.c; do
    "$CC" -O2 -w -I"$BT/source" -c "$f" -o "$OBJ/$(basename "$f").o"
done
"$CXX" -std=c++11 -O2 -w -DVERSION_MAJOR=1 -DVERSION_MINOR=2 -DVERSION_MICRO=2 -I"$BT/source" \
    "$BT"/source/*.cpp "$BT"/source/3ds/*.cpp "$BT"/source/pc/*.cpp "$OBJ"/*.o -o "$BIN/bannertool"

echo "Tools ready in $BIN:"
ls -1 "$BIN"
