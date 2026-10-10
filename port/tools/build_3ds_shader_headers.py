#!/usr/bin/env python3
"""Verify embedded 3DS shader headers, or regenerate them with --write.

Run after editing native_mesh.v.pica or unified.v.pica. The default checks the
assembled bytes without changing the headers consumed by the C++ renderer.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--picasso", type=Path)
    parser.add_argument("--shader", choices=["native_mesh", "unified"], action="append")
    args = parser.parse_args()
    compiler = args.picasso or shutil.which("picasso") or Path(os.environ.get("DEVKITPRO", "/opt/devkitpro")) / "tools/bin/picasso"
    if not Path(compiler).is_file():
        parser.error("picasso not found; use --picasso or set DEVKITPRO")
    folder = Path(__file__).resolve().parents[1] / "platform/3ds"
    failed = False
    with tempfile.TemporaryDirectory(prefix="sm64ds-shaders-") as temp:
        for name in args.shader or ["native_mesh", "unified"]:
            binary = Path(temp) / (name + ".shbin")
            subprocess.run([str(compiler), "-o", str(binary), str(folder / (name + ".v.pica"))], check=True)
            data = binary.read_bytes()
            header = folder / (name + "_shader.h")
            if args.write:
                header.write_text("#pragma once\nalignas(4) static const unsigned char " + name + "_shader[]={" + ",".join(map(str, data)) + "};\n")
            match = re.search(r"unsigned char " + name + r"_shader\[\]\s*=\s*\{([^}]*)\}", header.read_text())
            embedded = bytes(int(token.strip(), 0) for token in match[1].split(",") if token.strip()) if match else None
            same = embedded == data
            failed |= not same
            print(f"{name}: {'PASS' if same else 'STALE'} {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
