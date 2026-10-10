# Contributing

Thanks for helping! This is a classic fork-and-pull-request project.

## Workflow

1. **Fork** the repository and clone your fork **with its submodule**:
   `git clone --recursive https://github.com/<you>/sm64ds-3ds.git`
   (or `git submodule update --init --recursive` in an existing clone).
2. Create a branch from `main`: `git checkout -b fix/short-description`.
3. Build with your own ROM (`./build.sh`, see the README) and test your change
   (Azahar and, if you can, real hardware).
4. Commit with clear messages, push to your fork, and open a **pull request** against `main`.
5. Fill in the PR template: what changed, why, how you tested it (emulator / New 3DS / Old 3DS),
   before/after screenshots or fps numbers when relevant.

Maintainers review every PR; at least one approval is required before merging.

### Where does my change go?

| You change… | Open the PR against… |
|---|---|
| `port/`, `build3ds/`, `build.sh`, docs, CI | this repository (`SebastienBreton/sm64ds-3ds`, branch `main`) |
| decomp files: `sm64ds-decomp/src/`, `include/`, `config/`, decomp `tools/` | the fork [`SebastienBreton/sm64ds-decomp`](https://github.com/SebastienBreton/sm64ds-decomp), branch **`3ds-port`** |

A decomp change needs two steps: once it is merged into `3ds-port`, a follow-up PR here bumps the
submodule (`git -C sm64ds-decomp fetch && git -C sm64ds-decomp checkout origin/3ds-port &&
git add sm64ds-decomp`). Keep decomp changes small and guarded so they can go upstream to
[tangosdev/sm64ds-decomp](https://github.com/tangosdev/sm64ds-decomp) later.
Keep PRs focused — one fix or feature per PR is much easier to review.

## The one hard rule: no Nintendo data

Never commit the ROM, files extracted from it (`sm64ds-decomp/extracted/`, `sm64ds-decomp/build/`, `out/`),
saves, screenshots of copyrighted assets as test fixtures, or generated files that embed ROM bytes
(textures, models, level data, sound banks, golden dumps). The `.gitignore` and the CI guard catch
the common cases, but you are responsible for what you push. PRs containing game data are closed.

## Code guidelines

- `sm64ds-decomp/src/` is the byte-verified decompilation and is **not edited for host
  convenience**. Host-specific changes go in `port/` (seam headers, `port/unmatched/`) or in the
  generators under `port/tools/`. When a decomp source really must change, keep the matching
  build untouched (`#ifdef SM64DS_PLATFORM_PC` / `__3DS__`) and send it to the fork's `3ds-port` branch.
- Gameplay must stay **identical to the DS**. If you change game logic, compare against melonDS.
- Real 3DS hardware is stricter than emulators. When touching GPU code:
  - every vertex-shader output register must be written in full (`xyzw`) — partial writes hang the GPU;
  - never submit a draw with 0 vertices;
  - flush CPU-written buffers (`GSPGPU_FlushDataCache`) before the GPU reads them, and never sample
    a texture that is the current render target;
  - don't rely on GX display-transfer channel order (Azahar and hardware differ).
- Performance matters: the target is ≥30 fps minimum (not average) everywhere. Mention fps impact
  in your PR when you touch rendering, audio or the main loop.
- Match the style of the surrounding code; keep comments short and factual.

## Reporting bugs

Open an issue with the bug template: hardware (New/Old 3DS, emulator + version), what you did,
what you expected, and the log `sdmc:/sm64ds/user-test.log`. Photos of the screen help a lot.

## Code of conduct

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
