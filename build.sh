#!/usr/bin/env bash
# Build Super Mario 64 DS for Nintendo 3DS from your own cartridge dump.
#
#   ./build.sh            full pipeline (extract -> configure -> build -> SD package)
#   ./build.sh package    only refresh out/sd from an existing build
#   ./build.sh cia        installable out/sm64ds.cia (runs the full build first if needed)
#   ./build.sh tools      build makerom + bannertool from source into build3ds/tools-bin
#
# Docker is the default backend; use ./build.sh --native for the local toolchain.
# Native requires: devkitPro (devkitARM, libctru, citro3d, picasso), CMake >= 3.16,
# Python >= 3.10 with `pip install ndspy capstone pyelftools`, and the
# sm64ds-decomp submodule (`git submodule update --init`).
# The CIA also needs `pip install pillow numpy`, makerom and bannertool (PATH or
# build3ds/tools-bin, see `./build.sh tools`).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BACKEND="${SM64DS_BUILD_BACKEND:-docker}"
case "${1:-}" in
    --native) BACKEND=native; shift ;;
    --docker) BACKEND=docker; shift ;;
    --help|-h)
        echo "usage: $0 [--docker|--native] [all|package|cia|tools]"
        echo "Docker is the default. Set SM64DS_ROM=/path/to/dump.nds to use an external ROM."
        exit 0 ;;
esac
[ "$#" -le 1 ] || { echo "error: expected one build command" >&2; exit 2; }
case "${1:-all}" in
    all|package|cia|tools) ;;
    *) echo "usage: $0 [--docker|--native] [all|package|cia|tools]" >&2; exit 2 ;;
esac
case "$BACKEND" in
    docker) exec "$ROOT/docker/build.sh" "${1:-all}" ;;
    native) ;;
    *) echo "error: SM64DS_BUILD_BACKEND must be docker or native" >&2; exit 2 ;;
esac
ROM="${SM64DS_ROM:-$ROOT/baserom.eu.nds}"
DECOMP="$ROOT/sm64ds-decomp"
PORT="$ROOT/port"
CMAKE_DIR="$ROOT/build3ds/cmake"
OUT="$ROOT/out/sd"
PYTHON="${PYTHON:-python3}"
EXPECTED_SHA1="1367529f2cb23e76ef295cb1727333ae8f0a6cd7"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export SM64DS_DECOMP_DIR="$DECOMP"

die() { echo "error: $*" >&2; exit 1; }

check_tools() {
    [ -f "$DECOMP/tools/unpack.py" ] ||
        die "the sm64ds-decomp submodule is missing. Run: git submodule update --init --recursive"
    [ -x "$DEVKITARM/bin/arm-none-eabi-g++" ] ||
        die "devkitARM not found at $DEVKITARM (install devkitPro with the 3ds-dev group, or set DEVKITPRO)."
    [ -f "$DEVKITPRO/cmake/3DS.cmake" ] || die "$DEVKITPRO/cmake/3DS.cmake not found (devkitPro 3ds-cmake)."
    [ -x "$DEVKITPRO/tools/bin/3dsxtool" ] || die "3dsxtool not found in $DEVKITPRO/tools/bin (devkitPro 3dstools)."
    [ -x "$DEVKITPRO/tools/bin/picasso" ] || die "picasso not found in $DEVKITPRO/tools/bin (devkitPro picasso)."
    [ -f "$DEVKITPRO/libctru/lib/libcitro3d.a" ] || die "citro3d not found in $DEVKITPRO/libctru (devkitPro citro3d)."
    command -v cmake > /dev/null || die "cmake not found (CMake >= 3.16)."
    command -v "$PYTHON" > /dev/null || die "$PYTHON not found (set PYTHON=/path/to/python3)."
    "$PYTHON" - <<'EOF' || die "Python >= 3.10 with ndspy, capstone and pyelftools is required: $PYTHON -m pip install ndspy capstone pyelftools"
import sys
if sys.version_info < (3, 10):
    sys.exit(f"Python {sys.version.split()[0]} is too old")
import ndspy, capstone, elftools  # noqa: F401
EOF
}

check_rom() {
    [ -f "$ROM" ] || die "put your own Super Mario 64 DS (Europe) dump at $ROM (or set SM64DS_ROM)."
    local sha
    sha="$(shasum -a 1 "$ROM" 2>/dev/null || sha1sum "$ROM")"
    sha="${sha%% *}"
    if [ "$sha" != "$EXPECTED_SHA1" ]; then
        echo "warning: ROM SHA-1 is $sha, expected $EXPECTED_SHA1 (Europe, En/Fr/De/Es/It)." >&2
        echo "         Other revisions are not supported yet." >&2
    fi
}

extract() {
    # Everything lands in the (git-ignored) sm64ds-decomp/extracted and sm64ds-decomp/build.
    "$PYTHON" "$DECOMP/tools/unpack.py" "$ROM"
    "$PYTHON" "$ROOT/build3ds/extract_files.py" "$ROM" "$DECOMP/extracted/dsd/files"
    (cd "$DECOMP" && "$PYTHON" tools/asset_catalog.py generate "$ROM")
    "$PYTHON" "$PORT/tools/archive_catalog.py"
    "$PYTHON" "$PORT/tools/ntr_manifest.py"   # extracted/dsd/files/files.txt (id size path)
}

configure() {
    cmake -S "$PORT" -B "$CMAKE_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSM64DS_EXPERIMENTAL_GPU_COMPOSITE=ON \
        -DSM64DS_DECOMP_DIR="$DECOMP" \
        -DPython3_EXECUTABLE="$(command -v "$PYTHON")" \
        -DSM64DS_SOURCE_ROM="$ROM"
}

compile() {
    "$ROOT/build3ds/build.sh" smoke_player_3dsx
    "$PYTHON" "$ROOT/build3ds/build_game.py" --user-build
}

package() {
    rm -rf "${OUT:?}"
    mkdir -p "$OUT/3ds/sm64ds" "$OUT/sm64ds/repo/build" "$OUT/sm64ds/repo/extracted/dsd"
    cp "$ROOT/build3ds/user-build/sm64ds.3dsx" "$OUT/3ds/sm64ds/"
    cp -R "$DECOMP/build/assets" "$OUT/sm64ds/repo/build/"
    cp -R "$DECOMP/extracted/dsd/files" "$DECOMP/extracted/dsd/archives" "$OUT/sm64ds/repo/extracted/dsd/"
    echo "SD card layout ready in $OUT — copy its contents to the root of your SD card."
}

check_cia_tools() {
    local t
    for t in makerom bannertool; do
        command -v "$t" > /dev/null || [ -x "$ROOT/build3ds/tools-bin/$t" ] ||
            die "$t not found. Run ./build.sh tools (builds makerom and bannertool from source into build3ds/tools-bin) or put it on PATH."
    done
    "$PYTHON" -c "import PIL, numpy, ndspy" 2> /dev/null ||
        die "the CIA banner needs Pillow and numpy: $PYTHON -m pip install pillow numpy ndspy"
}

cia() {
    # Banner (title logo) and icon are generated from YOUR ROM, never stored in the repo.
    "$PYTHON" "$ROOT/build3ds/build_cia.py" --rom "$ROM" --elf "$ROOT/build3ds/user-build/sm64ds.elf" \
        --output "$ROOT/out/sm64ds.cia"
}

case "${1:-all}" in
    package) package ;;
    all) check_tools; check_rom; extract; configure; compile; package ;;
    tools) "$ROOT/build3ds/build_cia_tools.sh" ;;
    cia)
        check_cia_tools; check_rom
        if [ ! -f "$ROOT/build3ds/user-build/sm64ds.elf" ]; then
            check_tools; extract; configure; compile; package
        fi
        cia ;;
    *) echo "usage: $0 [all|package|cia|tools]" >&2; exit 2 ;;
esac
