#!/usr/bin/env python3
"""Import original object category dispatch and actor-ID tables with native calls."""
from pathlib import Path
import argparse
import re
import struct

# Fifteen free-function loaders take an object subtable pointer, area and parameter.
SPANS = [(0x021089e0,0x021089ec),(0x02108ab0,0x02108ad4),(0x0210d794,0x0210d7b4),(0x0210d8f4,0x0210d914),(0x0210abb8,0x0210abc4),(0x0210b498,0x0210b4c4),(0x0210c3b0,0x0210c3e8),(0x0210cb5c,0x0210ce80),(0x0210d714,0x0210d734),(0x0210d954,0x0210d974),(0x0210d914,0x0210d934),(0x0210d734,0x0210d754),(0x0210d934,0x0210d954)]



def generate(root):
    cfg = root / "config/arm9/overlays/ov002"
    blob = (root / "extracted/overlays/overlay_0002.bin").read_bytes()
    names, targets, relocs = {}, {}, {}
    inside = lambda address: any(a <= address < b for a,b in SPANS)
    for line in (cfg / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?",line)
        if m:
            name, kind, addr = m[1],m[2],int(m[3],16)
            targets[addr] = name,kind
            if inside(addr):
                if not kind.startswith("data"):
                    raise ValueError(f"non-data symbol: {line}")
                names.setdefault(addr,[]).append(name)
    for line in (cfg / "relocs.txt").read_text().splitlines():
        m = re.match(r"from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)",line)
        if not m or not inside(int(m[1],16)):
            continue
        addr,target = int(m[1],16),int(m[3],16)
        if addr == 0x02108ad0:
            if m[2] != "load" or target != 0x021135f4 or m[4] != "overlays(9,14,15,16,18,19,20,22,24,25,26,27,29,32,36)":
                raise ValueError(f"unreviewed block cross-overlay relocation: {line}")
            if struct.unpack_from("<I",blob,addr-0x020ad660)[0] != target:
                raise ValueError("block CLPS pointer mismatch")
            relocs[addr] = "data_ov009_021135f4" # This target imports castle-level data.
            continue
        if m[2] != "load" or m[4] != "overlay(2)" or target not in targets:
            raise ValueError(f"unreviewed relocation: {line}")
        name,kind = targets[target]
        if not (((kind.startswith("data") or kind=="bss") and 0x02108ab0 <= addr < 0x02108ad0 and target in (0x0210dcc0,0x0210dcb8,0x0210d794,0x0210dce0,0x0210dcc8,0x0210d8f4,0x0210dcd0,0x0210dcd8)) or (kind.startswith("function") and 0x020fe3cc <= target < 0x020fea4c and 0x0210cbb8 <= addr < 0x0210cbf4) or (kind.startswith("data") and 0x0210c3d0 <= target < 0x0210c3e0 and 0x0210c3e0 <= addr < 0x0210c3e8)):
            raise ValueError(f"unreviewed target: {line}")
        if struct.unpack_from("<I",blob,addr-0x020ad660)[0] != target or addr % 4:
            raise ValueError(f"unexpected pointer encoding: {line}")
        relocs[addr] = name
    if len(relocs) != 26:
        raise ValueError(f"unexpected pointer count {len(relocs)}")
    lines = ['// Generated from local ROM; object loader dispatch.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.level_loader_{start:08x},"aw",%progbits','.balign 4']
        cursor = start
        while cursor < end:
            for name in names.get(cursor,[]):
                lines += [f'.global {name}',f'.type {name}, %object',f'{name}:']
            if cursor in relocs:
                if cursor+4>end or any(cursor+i in names for i in range(1,4)):
                    raise ValueError("split pointer")
                lines.append(f'.word {relocs[cursor]}')
                cursor += 4
            else:
                stop = min(cursor+16,end)
                for boundary in range(cursor+1,stop):
                    if boundary in names or boundary in relocs:
                        stop = boundary
                        break
                raw=blob[cursor-0x020ad660:stop-0x020ad660]
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
