#!/usr/bin/env python3
"""Extract built-in cuboid/cylinder shadow BMDs with native data pointers."""
from pathlib import Path
import argparse
import re
import struct

# Shadow numeric data only; no overlay1 actor vtables/code are imported.
SPANS = [(0x020ab5f8,0x020ab7e4),(0x020ad49c,0x020ad620)]



def generate(root):
    cfg = root / "config/arm9/overlays/ov001"
    blob = (root / "extracted/overlays/overlay_0001.bin").read_bytes()
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
        if m[2] != "load" or m[4] != "overlay(1)" or target not in targets:
            raise ValueError(f"unreviewed relocation: {line}")
        name,kind = targets[target]
        if kind != "bss" and not (kind.startswith("data") and inside(target)):
            raise ValueError(f"unreviewed target: {line}")
        if struct.unpack_from("<I",blob,addr-0x020aa420)[0] != target or addr % 4:
            raise ValueError(f"unexpected pointer encoding: {line}")
        relocs[addr] = name
    if len(relocs) != 22:
        raise ValueError(f"unexpected pointer count {len(relocs)}")
    lines = ['// Generated from local ROM; built-in shadow models.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.shadow_{start:08x},"aw",%progbits','.balign 4']
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
                raw=blob[cursor-0x020aa420:stop-0x020aa420]
                if len(raw)!=stop-cursor: raise ValueError("short ROM")
                lines.append('.byte '+','.join(str(v) for v in raw))
                cursor=stop
        lines.append('.popsection')
    lines += [')DATA");','']
    lines += ['asm(R"BSS(', '.pushsection .bss.shadow_models,"aw",%nobits', '.balign 4',
              '.global data_ov001_020ad640','data_ov001_020ad640:', '.zero 4',
              '.global data_ov001_020ad644','data_ov001_020ad644:', '.zero 4', '.popsection',
              '.global data_020ad524', '.set data_020ad524, data_ov001_020ad524',
              '.global data_020ad560', '.set data_020ad560, data_ov001_020ad560', ')BSS");','']
    return "\n".join(lines)


if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root",type=Path)
    ap.add_argument("output",type=Path)
    args=ap.parse_args()
    args.output.write_text(generate(args.root))
