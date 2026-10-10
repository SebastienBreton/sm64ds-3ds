#!/usr/bin/env python3
"""Import castle level records, CLPS and texture animation tables from local ROM."""
from pathlib import Path
import argparse
import re
import struct

# Numeric level data only: exclude overlay9 code and actor vtables.
SPANS = [(0x02112228,0x02112458),(0x02112b60,0x021138fc)]



def generate(root):
    cfg = root / "config/arm9/overlays/ov009"
    blob = (root / "extracted/overlays/overlay_0009.bin").read_bytes()
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
        if m[2] != "load" or m[4] != "overlay(9)" or target not in targets:
            raise ValueError(f"unreviewed relocation: {line}")
        name,kind = targets[target]
        if not (kind.startswith("data") and inside(target)):
            raise ValueError(f"unreviewed target: {line}")
        if struct.unpack_from("<I",blob,addr-0x021111a0)[0] != target or addr % 4:
            raise ValueError(f"unexpected pointer encoding: {line}")
        relocs[addr] = name
    if len(relocs) != 32:
        raise ValueError(f"unexpected pointer count {len(relocs)}")
    lines = ['// Generated from local ROM; castle level records.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.castle_{start:08x},"aw",%progbits','.balign 4']
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
                raw=blob[cursor-0x021111a0:stop-0x021111a0]
                if len(raw)!=stop-cursor: raise ValueError("short ROM")
                lines.append('.byte '+','.join(str(v) for v in raw))
                cursor=stop
        lines.append('.popsection')
    lines += [')DATA");','']
    # Water resource handles, destructor nodes and three contiguous spawn
    # vector arrays. Preserve every interior label used by its initializer.
    start,end=0x02113c20,0x02113ee0
    labels={address:name for address,(name,kind) in targets.items()
            if kind=='bss' and start<=address<end}
    lines += ['asm(R"BSS(','.pushsection .bss.castle_water,"aw",%nobits','.balign 4',
              '.global sm64ds_castle_bss_begin','sm64ds_castle_bss_begin:']
    cursor=start
    for address,name in sorted(labels.items()):
        if address>cursor: lines.append(f'.zero {address-cursor}')
        lines += [f'.global {name}',f'{name}:']
        cursor=address
    lines += [f'.zero {end-cursor}', '.global sm64ds_castle_bss_end',
              'sm64ds_castle_bss_end:', '.popsection', ')BSS");']
    return "\n".join(lines)


if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root",type=Path)
    ap.add_argument("output",type=Path)
    args=ap.parse_args()
    args.output.write_text(generate(args.root))
