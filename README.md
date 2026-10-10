# Super Mario 64 DS — Nintendo 3DS native port

<p>
  <img src="https://img.shields.io/badge/platform-Nintendo%203DS-1f6feb?style=for-the-badge" alt="Nintendo 3DS">
  <img src="https://img.shields.io/badge/output-.3dsx-6f42c1?style=for-the-badge" alt="3DSX">
  <img src="https://img.shields.io/badge/status-work%20in%20progress-orange?style=for-the-badge" alt="Work in progress">
</p>

A **native** port of *Super Mario 64 DS* (Europe) to the Nintendo 3DS, built on top of the
[sm64ds-decomp](https://github.com/tangosdev/sm64ds-decomp) decompilation. The game code runs
natively on the 3DS CPU; the DS 2D/3D hardware is reimplemented on top of the 3DS GPU
(PICA200, citro3d). This is **not** an emulator.

> **No game data is included.** You need your own legally obtained copy of the game.
> The build extracts everything it needs from your dump, locally.

## 🔗 Related repositories

- [tangosdev/sm64ds-decomp](https://github.com/tangosdev/sm64ds-decomp) — SM64DS decompilation (MIT). Included as the git
  submodule `sm64ds-decomp/`, pointing at branch [`3ds-port`](https://github.com/SebastienBreton/sm64ds-decomp/tree/3ds-port)
  of the fork [SebastienBreton/sm64ds-decomp](https://github.com/SebastienBreton/sm64ds-decomp)
  (upstream commit `b3de552` + a few host-compiler fixes)
- [sm64-port/sm64_3ds](https://github.com/sm64-port/sm64_3ds) / [Super Mario 64 3DS Port Ultimate](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate) — inspiration for the 3DS presentation
- [melonDS](https://github.com/melonDS-emu/melonDS) — DS reference used to compare rendering, physics and sound
- [Azahar](https://github.com/azahar-emu/azahar) — 3DS emulator used for automated testing

## ⚙️ Features

- Whole game playable: menus, castle, courses, bosses, the 36 Rec Room mini-games
- DS rendering on the 3DS GPU: geometry, lighting, toon shading, 2D layers, display capture
- Full-width blue sky on the 3DS top screen (4:3 game image, no stretching)
- Stereoscopic 3D in the title menu (layered depth, the star flies in front of / behind the logo) and in game
  (both eyes in one GPU pass, HUD at screen depth, automatic mono fallback on heavy frames)
- New 3DS C-Stick camera: "DS arrows" (same as the touch camera arrows) or a smooth "Free" camera
- OPTIONS screen (SELECT+START): HD title logo, texture filtering, Circle Pad / C-Stick modes, fps counter
- Analog Circle Pad movement (slight tilt = walk, full tilt = run) — DS digital mode still available
- Sound: original SDAT sequences, sample-exact mixer
- Real-time pacing with frame skipping when the GPU falls behind

## 🧭 Status

Work in progress. Tested mainly in Azahar (Old 3DS model) and on a New 3DS.
Known issues and ongoing work are tracked in the GitHub issues. Highlights:

- New 3DS recommended. Menu ~40–60 fps, castle/courses ~30 fps on hardware (optimisation ongoing)
- Some translucency effects (shadows, translucent boxes) are wrong on real hardware
- Stereoscopic 3D, OPTIONS and C-Stick camera are validated in Azahar, not yet on hardware

## 🎮 Controls and settings

DS controls are kept (D-pad to walk, `Y` to run, `A` jump, `B` punch, touch screen as on DS).
The Circle Pad is analog by default, the C-Stick (New 3DS) turns the camera. Press **SELECT+START**
to open the OPTIONS screen; settings are saved in `sdmc:/sm64ds/settings.ini`:

```ini
[input]
circle_pad = analog     ; or: ds  (Circle Pad acts as the D-pad, like the original)
camera_cstick = ds      ; or: free / off
```

## 🏗️ Building

Requirements:

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM, `3ds-dev` (libctru, citro3d, picasso)
  (expected in `/opt/devkitpro`, the default install location)
- CMake ≥ 3.16, Python ≥ 3.10, `pip install ndspy capstone pyelftools`
  (a virtualenv is fine: activate it, or run `PYTHON=/path/to/venv/bin/python ./build.sh`)
- Your own dump of **Super Mario 64 DS (Europe) (En,Fr,De,Es,It)**,
  SHA-1 `1367529f2cb23e76ef295cb1727333ae8f0a6cd7`

Then:

```sh
git clone --recursive https://github.com/SebastienBreton/sm64ds-3ds.git
cd sm64ds-3ds
cp /path/to/your/dump.nds baserom.eu.nds
./build.sh
```

Already cloned without `--recursive`, or pulled a change that moves the submodule?
Run `git submodule update --init --recursive`.

No dsd toolkit and no Metrowerks compiler are needed: the port builds the decomp sources with
devkitARM GCC, and the ROM is unpacked with `ndspy`.

The script extracts the ROM (into the git-ignored `sm64ds-decomp/extracted/` and
`sm64ds-decomp/build/`), configures and builds the port, and prepares an SD card layout in `out/sd/`:

```
out/sd/3ds/sm64ds/sm64ds.3dsx                        # the homebrew
out/sd/sm64ds/repo/build/assets/...                  # file catalogs generated from YOUR dump
out/sd/sm64ds/repo/extracted/dsd/{files,archives}/   # game data extracted from YOUR dump
```

Copy the contents of `out/sd/` to the root of your SD card and launch **sm64ds** from the
Homebrew Launcher. Saves go to `sdmc:/sm64ds/user-test.sav`, logs to `sdmc:/sm64ds/user-test.log`.

## 🗂️ Repository layout

```
port/            the 3DS port: platform layer (platform/3ds), DS hardware reimplementation (ntr/),
                 HAL seams (hal/), menus, audio, generators (tools/) and the CMake build
sm64ds-decomp/   git submodule: the decompilation (fork branch 3ds-port), built from source
build3ds/        build helpers (build.sh, build_game.py, ROM-free audits)
build.sh         end-to-end build: extract -> configure -> build -> out/sd
```

`port/CMakeLists.txt` finds the decomp through the cache variable `SM64DS_DECOMP_DIR`
(default `../sm64ds-decomp`); the generators in `port/tools/` honour the `SM64DS_DECOMP_DIR`
environment variable. The submodule's own `port/` directory is upstream's original smoke-test
port and is **not** used.

## 🤝 Contributing

Contributions are welcome — fork the repository and open a pull request.
Please read [CONTRIBUTING.md](CONTRIBUTING.md) first (most importantly: never commit the ROM or
anything extracted from it). Port code (`port/`, `build3ds/`, docs) → pull request here;
changes to the decompilation sources (`sm64ds-decomp/src`, `include`, …) → pull request against the
`3ds-port` branch of [SebastienBreton/sm64ds-decomp](https://github.com/SebastienBreton/sm64ds-decomp).

## ⚖️ Legal

This project contains no Nintendo code or assets and is not affiliated with or endorsed by
Nintendo. *Super Mario 64 DS* is a trademark of Nintendo. See [LICENSE](LICENSE).
