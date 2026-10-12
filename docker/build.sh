#!/usr/bin/env bash
# Run the existing pipeline inside Docker with independent Linux build caches.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACTION="${1:-all}"
die() { echo "error: $*" >&2; exit 1; }
command -v docker > /dev/null || die "Docker not found. Install Docker Desktop or OrbStack, or use ./build.sh --native."
docker info > /dev/null 2>&1 || die "Docker is not running. Start Docker Desktop or OrbStack."

ROM_ARGS=()
if [ "$ACTION" = all ] || [ "$ACTION" = cia ]; then
    ROM="${SM64DS_ROM:-$ROOT/baserom.eu.nds}"
    [ -f "$ROM" ] || die "put your own Europe dump at $ROM (or set SM64DS_ROM)."
    ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
    ROM_ARGS=(--volume "$ROM:/rom/baserom.eu.nds:ro"
              --env SM64DS_ROM=/rom/baserom.eu.nds)
fi

# A changed image recipe gets a new tag; source edits reuse the same toolchain.
if command -v shasum > /dev/null; then HASH=(shasum -a 256); else HASH=(sha256sum); fi
FINGERPRINT="$(cat "$ROOT/docker/Dockerfile" "$ROOT/docker/requirements.txt" "$ROOT/build3ds/build_cia_tools.sh" | "${HASH[@]}")"
IMAGE="sm64ds-3ds-build:${FINGERPRINT:0:16}"
docker image inspect "$IMAGE" > /dev/null 2>&1 ||
    docker build --tag "$IMAGE" --file "$ROOT/docker/Dockerfile" "$ROOT"

RUN_ARGS=(--rm --init --user "$(id -u):$(id -g)"
          --volume "$ROOT:/work"
          --workdir /work --env PYTHON=python3 --env SM64DS_BUILD_BACKEND=native)
# The Python link helpers read the CMake Makefiles at these canonical paths.
# Mount isolated directories there so macOS objects/caches/tools are never reused.
for DIR in cmake user-build menu-compile tools-src tools-bin cia-build; do
    mkdir -p "$ROOT/build3ds/docker/$DIR"
    RUN_ARGS+=(--volume "$ROOT/build3ds/docker/$DIR:/work/build3ds/$DIR")
done
if [ -n "${JOBS:-}" ]; then RUN_ARGS+=(--env "JOBS=$JOBS"); fi
exec docker run "${RUN_ARGS[@]}" ${ROM_ARGS[@]+"${ROM_ARGS[@]}"} "$IMAGE" ./build.sh --native "$ACTION"
