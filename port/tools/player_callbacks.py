#!/usr/bin/env python3
"""Bind Player initializer PMFs to native callbacks; never copy DS code pointers."""
import argparse
from pathlib import Path
import re
import struct


def generate(root):
    src = (root / "src/__sinit_ov002_021019d0.c").read_text()
    config = root / "config/arm9/overlays/ov002/symbols.txt"
    functions = {}
    for line in config.read_text().splitlines():
        m = re.match(r"(\w+) kind:function.*addr:0x([0-9a-f]+)", line)
        if m:
            functions[int(m[2], 16)] = m[1]
    blob = (root / "extracted/overlays/overlay_0002.bin").read_bytes()
    entries = []
    names = re.findall(r"extern struct P2 (data_ov002_\w+);", src)
    # Additional Player action callbacks used outside the static initializer.
    names += ['data_ov002_0210a054', 'data_ov002_0210a064', 'data_ov002_0210a094', 'data_ov002_0210a0b4', 'data_ov002_0210a0dc', 'data_ov002_0210a124', 'data_ov002_0210a14c', 'data_ov002_0210a36c', 'data_ov002_0210a3c4', 'data_ov002_0210a3fc', 'data_ov002_0210a40c', 'data_ov002_0210a44c', 'data_ov002_0210a474', 'data_ov002_0210a534']
    for name in names:
        address = int(name[-8:], 16)
        code, adjustment = struct.unpack_from("<II", blob, address - 0x020ad660)
        # Currently all230 entries are nonvirtual, with no this adjustment.
        # Reject a different ROM/layout instead of silently misencoding a PMF.
        if adjustment != 0 or code not in functions:
            raise ValueError(f"unsupported callback {name}: {code:#x}/{adjustment:#x}")
        entries.append((name, functions[code]))
    if len(entries) != 230:
        raise ValueError(f"unexpected Player callback count: {len(entries)}")
    targets = sorted({target for _, target in entries})
    lines = ["// Generated native Player callbacks from the local ROM symbol mapping.",
             '#include "Player.h"', 'struct NativePlayerCallbacks {']
    for index, target in enumerate(targets):
        # Direct symbol binding, no wrapper: callback arguments in r1-r3 must
        # survive even for state tables with signatures other than int().
        lines.append(f'    int call_{index}() asm("{target}");')
    lines += ['};', 'using NativePlayerCallback = int (NativePlayerCallbacks::*)();',
              'static_assert(sizeof(NativePlayerCallback) == 8, "Player PMF ABI");',
              'static_assert(sizeof(Player::State) == 24, "Player state ABI");',
              'extern "C" {']
    for name, target in entries:
        lines.append(f'NativePlayerCallback {name} = &NativePlayerCallbacks::call_{targets.index(target)};')
    lines += ['}', '']
    return "\n".join(lines)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    args.output.write_text(generate(args.root))
