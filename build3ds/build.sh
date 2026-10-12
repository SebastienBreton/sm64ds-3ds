#!/usr/bin/env bash
# Build all gates by default; any failed target makes the build fail.
set -uo pipefail
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"; export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
D="$(cd "$(dirname "$0")" && pwd)"
args=()
if [ "$#" -gt 0 ]; then args=(--target "$@"); fi
cmake --build "$D/cmake" ${args[@]+"${args[@]}"} -j"${JOBS:-10}" > "$D/cmake-build.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    tail -80 "$D/cmake-build.log"
    exit "$rc"
fi
tail -8 "$D/cmake-build.log"
