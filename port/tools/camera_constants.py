#!/usr/bin/env python3
"""Extract reviewed numeric camera spans, rejecting all pointer relocations."""
from pathlib import Path
import argparse
import re

# Numeric camera vectors/limits only. Exclude all RTTI, vtable headers,
# vtables and the pointer-bearing level-camera entries at0208751c onward.
SPANS = [(0x02086e84,0x02086ecc), (0x02086ed8,0x02086ee4),
         (0x02086ef0,0x02086f7c), (0x02086fcc,0x0208751c)]


def generate(root):
    cfg = root / "config/arm9"
    blob = (root / "extracted/arm9_dec.bin").read_bytes()
    names = {}
    inside = lambda address: any(a <= address < b for a,b in SPANS)
    for line in (cfg / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)",line)
        if m:
            name, kind, addr = m[1],m[2],int(m[3],16)
            if inside(addr):
                if not kind.startswith("data"):
                    raise ValueError(f"non-data symbol: {line}")
                names.setdefault(addr,[]).append(name)
    for line in (cfg / "relocs.txt").read_text().splitlines():
        m = re.match(r"from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)",line)
        if not m or not inside(int(m[1],16)):
            continue
        raise ValueError(f"unexpected relocation in numeric camera data: {line}")
    lines = ['// Generated from local ROM; reviewed numeric camera constants.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.camera_{start:08x},"aw",%progbits','.balign 4']
        cursor = start
        while cursor < end:
            for name in names.get(cursor,[]):
                lines += [f'.global {name}',f'.type {name}, %object',f'{name}:']
            stop = min(cursor+16,end)
            for boundary in range(cursor+1,stop):
                if boundary in names:
                    stop = boundary
                    break
            raw=blob[cursor-0x02004000:stop-0x02004000]
            if len(raw)!=stop-cursor: raise ValueError("short ROM")
            lines.append('.byte '+','.join(str(v) for v in raw))
            cursor=stop
        lines.append('.popsection')
    lines += [')DATA");','']
    return "\n".join(lines)


if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root",type=Path)
    ap.add_argument("output",type=Path)
    args=ap.parse_args()
    args.output.write_text(generate(args.root))
