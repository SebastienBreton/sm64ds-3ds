#!/usr/bin/env python3
"""Report actual unresolved ARM link symbols and their recovered source owners."""
import json
import pathlib
import re
import shlex
import subprocess
import sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = ROOT / "sm64ds-decomp"
BUILD = ROOT / "build3ds/cmake"
sys.path.insert(0, str(REPO / "tools"))
import srcpath
srcpath.set_root(REPO)
target = sys.argv[1] if len(sys.argv) > 1 else "smoke_actor"
command = shlex.split((BUILD / f"CMakeFiles/{target}.dir/link.txt").read_text())
run = subprocess.run(command + ["-Wl,--no-demangle"], cwd=BUILD, capture_output=True, text=True)
(BUILD.parent / f"{target}-link.log").write_text(run.stdout + run.stderr)
names = sorted(set(re.findall(r"undefined reference to [`']([^'`]+)['`]", run.stderr)))
report = []
for name in names:
    owner = srcpath.path_for(name)
    report.append({"symbol": name, "source": str(owner.relative_to(REPO)) if owner else None})
(BUILD.parent / f"{target}-closure.json").write_text(json.dumps(report, indent=2) + "\n")
for row in report:
    print(row["symbol"], "=>", row["source"] or "DATA/ABI/HAL")
print(f"{len(names)} unresolved; linker exit {run.returncode}")

if run.returncode and not names:
    print("Link failed before a usable unresolved-symbol report; inspect the link log.", file=sys.stderr)
    print(run.stderr[-3000:], file=sys.stderr)
    sys.exit(run.returncode)
