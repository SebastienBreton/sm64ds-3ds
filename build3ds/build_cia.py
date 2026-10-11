#!/usr/bin/env python3
"""Package the user build (build3ds/user-build/sm64ds.elf) as an installable CIA.

  1. make_cia_assets.py: banner.png / icon.png / banner.wav generated from YOUR
     ROM into build3ds/cia-build/assets (git-ignored, never committed)
  2. bannertool makebanner + makesmdh -> banner.bnr, icon.smdh
  3. makerom -f cia with build3ds/cia/sm64ds.rsf -> out/sm64ds.cia

makerom and bannertool are looked up on PATH, then in build3ds/tools-bin
(`./build.sh tools` builds them from source there).

Usage: build_cia.py --rom ROM [--elf ELF] [--output out/sm64ds.cia] [--fallback-banner]
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS_BIN = ROOT / "build3ds/tools-bin"
RSF = ROOT / "build3ds/cia/sm64ds.rsf"

TITLE = "Super Mario 64 DS"
DESCRIPTION = "Super Mario 64 DS - native Nintendo 3DS port"
PUBLISHER = "SM64DS 3DS port"


def tool(name):
    found = shutil.which(name) or shutil.which(name, path=str(TOOLS_BIN))
    if not found:
        sys.exit(f"error: {name} not found on PATH or in {TOOLS_BIN}.\n"
                 f"       Run `./build.sh tools` to build makerom and bannertool from source,\n"
                 f"       or install them yourself (makerom: github.com/3DSGuy/Project_CTR,\n"
                 f"       bannertool: github.com/diasurgical/bannertool) and put them on PATH.")
    return found


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--elf", type=Path, default=ROOT / "build3ds/user-build/sm64ds.elf")
    ap.add_argument("--output", type=Path, default=ROOT / "out/sm64ds.cia")
    ap.add_argument("--work", type=Path, default=ROOT / "build3ds/cia-build")
    ap.add_argument("--fallback-banner", action="store_true", help="generated text banner instead of the ROM title logo")
    a = ap.parse_args()

    makerom, bannertool = tool("makerom"), tool("bannertool")
    if not a.elf.is_file():
        sys.exit(f"error: {a.elf} not found - build the game first (./build.sh)")
    if not a.rom.is_file():
        sys.exit(f"error: ROM {a.rom} not found")
    assets = a.work / "assets"
    cmd = [sys.executable, ROOT / "build3ds/make_cia_assets.py", "--rom", a.rom, "--out", assets]
    if a.fallback_banner:
        cmd.append("--fallback")
    run(cmd)

    bnr, smdh = a.work / "banner.bnr", a.work / "icon.smdh"
    run([bannertool, "makebanner", "-i", assets / "banner.png", "-a", assets / "banner.wav", "-o", bnr])
    run([bannertool, "makesmdh", "-s", TITLE, "-l", DESCRIPTION, "-p", PUBLISHER,
         "-i", assets / "icon.png", "-f", "visible,allow3d,recordusage,nosavebackups", "-o", smdh])

    a.output.parent.mkdir(parents=True, exist_ok=True)
    run([makerom, "-f", "cia", "-o", a.output, "-target", "t", "-exefslogo",
         "-elf", a.elf, "-rsf", RSF, "-icon", smdh, "-banner", bnr])

    info = {
        "cia": str(a.output),
        "title_id": "000400000F536400",
        "elf_sha256": hashlib.sha256(a.elf.read_bytes()).hexdigest(),
        "cia_sha256": hashlib.sha256(a.output.read_bytes()).hexdigest(),
        "cia_bytes": a.output.stat().st_size,
        "assets": json.loads((assets / "assets.json").read_text()),
    }
    (a.work / "cia.json").write_text(json.dumps(info, indent=2) + "\n")
    print(f"CIA ready: {a.output} ({info['cia_bytes'] / 1e6:.1f} MB) - install it with FBI; "
          f"the game data must still be on the SD card (sdmc:/sm64ds/repo).")


if __name__ == "__main__":
    main()
