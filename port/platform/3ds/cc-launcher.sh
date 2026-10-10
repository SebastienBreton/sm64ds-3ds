#!/usr/bin/env bash
# Compiler launcher for the 3DS build. Runs the compile, then weakens every
# global in HAL objects: on MSVC those files forward C spellings of Itanium
# names (`extern "C" _ZN4cstd3absEi`) to the real C++ definitions, but GCC
# mangles with Itanium itself, so the forwarder and the src/ definition are the
# same symbol. Weak HAL symbols let src/ -- the byte-verified truth -- win.
"$@" || exit $?
out=""; src=""; prev=""
for a in "$@"; do
  [ "$prev" = "-o" ] && out=$a
  case $a in */port/hal/*.c|*/port/hal/*.cpp|*/port/ntr/runtime.cpp) src=$a;; esac
  prev=$a
done
if [ -n "$src" ] && [ -n "$out" ]; then
  # Only DEFINED globals: a weakened undefined reference would no longer pull
  # its libc archive member, and ld turns calls to unresolved weak symbols into
  # NOPs (that silently NOPed fopen/snprintf/fgets out of hal/fs.cpp).
  bin="${DEVKITARM:-/opt/devkitpro/devkitARM}/bin"
  syms=$("$bin/arm-none-eabi-nm" --defined-only -g "$out" | awk '$2 ~ /^[TDBRVW]$/ {print "--weaken-symbol=" $3}')
  [ -n "$syms" ] && "$bin/arm-none-eabi-objcopy" $syms "$out"
fi
