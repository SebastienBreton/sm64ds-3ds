#!/usr/bin/env bash
# Boot a .3dsx in Azahar with its GDB stub and let gdb run it to the first
# fault/stop, then print registers + backtrace. usage: debug.sh <file.3dsx> [secs]
set -u
APP="${AZAHAR_APP:-$HOME/Downloads/azahar-macos-universal-2126.1.2/Azahar.app}"
PAT="Azahar.app/Contents/MacOS/azahar"
SD="$HOME/Library/Application Support/Azahar/sdmc/sm64ds"
F="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
ELF="${F%.3dsx}.elf"
mkdir -p "$SD"; touch "$SD/autoexit"; rm -f "${SD:?}/log.txt"
BEFORE=$(pgrep -f "$PAT" | sort)
open -n -g -a "$APP" --args -g 24689 "$F"
perl -e 'select(undef,undef,undef,3)'
timeout_s=${2:-30}
/opt/devkitpro/devkitARM/bin/arm-none-eabi-gdb -q -batch "$ELF" \
  -ex "set pagination off" -ex "set confirm off" \
  -ex "target remote localhost:24689" \
  ${GDB_PRE:+-x "$GDB_PRE"} \
  -ex "continue" \
  -ex "info registers pc lr sp r0 r1 r2 r3" \
  -ex "bt 15" \
  -ex "x/4i \$pc" 2>&1 &
G=$!
( perl -e "select(undef,undef,undef,$timeout_s)"; kill -INT $G 2>/dev/null ) &
W=$!
wait $G 2>/dev/null; kill $W 2>/dev/null
for p in $(comm -13 <(echo "$BEFORE") <(pgrep -f "$PAT" | sort)); do kill -9 "$p" 2>/dev/null; done
rm -f "${SD:?}/autoexit"
echo "=== app log ==="; cat "$SD/log.txt" 2>/dev/null
