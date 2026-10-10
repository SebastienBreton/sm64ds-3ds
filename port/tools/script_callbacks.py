#!/usr/bin/env python3
"""Bind script dispatcher PMFs to native callbacks; never copy DS code pointers."""
import argparse
from pathlib import Path
import re
import struct


def generate(root):
    src = (root / "src/func_ov002_020f7d74.cpp").read_text()
    config = root / "config/arm9/overlays/ov002/symbols.txt"
    functions = {}
    for line in config.read_text().splitlines():
        m = re.match(r"(\w+) kind:function.*addr:0x([0-9a-f]+)", line)
        if m:
            functions[int(m[2], 16)] = m[1]
    blob = (root / "extracted/overlays/overlay_0002.bin").read_bytes()
    entries = []
    names = re.findall(r"extern PMF (data_ov002_\w+);", src)
    for name in names:
        address = int(name[-8:], 16)
        code, adjustment = struct.unpack_from("<II", blob, address - 0x020ad660)
        # Currently all24 entries are nonvirtual, with no this adjustment.
        # Reject a different ROM/layout instead of silently misencoding a PMF.
        if adjustment != 0 or code not in functions:
            raise ValueError(f"unsupported callback {name}: {code:#x}/{adjustment:#x}")
        entries.append((name, functions[code]))
    if len(entries) != 24:
        raise ValueError(f"unexpected script callback count: {len(entries)}")
    targets = sorted({target for _, target in entries})
    lines = ["// Generated native script callbacks from the local ROM symbol mapping.",
             'struct NativeScriptCallbacks {']
    for index, target in enumerate(targets):
        # Direct symbol binding, no wrapper: callback arguments in r1-r3 must
        # survive even for state tables with signatures other than int().
        lines.append(f'    void call_{index}(unsigned char*, int, int) asm("{target}");')
    lines += ['};', 'using NativeScriptCallback = void (NativeScriptCallbacks::*)(unsigned char*, int, int);',
              'static_assert(sizeof(NativeScriptCallback) == 8, "Script PMF ABI");',
              'extern "C" {']
    for name, target in entries:
        lines.append(f'NativeScriptCallback {name} = &NativeScriptCallbacks::call_{targets.index(target)};')
    lines += ['}', '']
    return "\n".join(lines)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    args.output.write_text(generate(args.root))
