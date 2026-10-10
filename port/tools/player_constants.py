#!/usr/bin/env python3
"""Extract reviewed Player data spans, with native data-pointer relocations."""
from pathlib import Path
import argparse
import re
import struct

# Preserve complete numeric/resource tables. Exclude PMFs, g_profile_PLAYER,
# the raw vtable and DS RTTI (0210a56c..578): those need native ABI bindings.
# The 19 animation IDs at 0210a7e8 end before the vtable header at 0210a834.
SPANS = [
    (0x02109db8,0x02109dd4), (0x02109fe4,0x02109fec),
    (0x02109ff4,0x02109ffc), (0x0210a07c,0x0210a08c),
    (0x0210a1d4,0x0210a1dc), (0x0210a34c,0x0210a354),
    (0x0210a3dc,0x0210a3e4), (0x0210a424,0x0210a42c),
    (0x0210a434,0x0210a43c), (0x0210a4bc,0x0210a4c4),
    (0x0210a554,0x0210a56c), (0x0210a578,0x0210a704),
    (0x0210a720,0x0210a834), (0x0210a8b8,0x0210a990),
    (0x0210c390,0x0210c3b0),
    (0x0210b614,0x0210b618), (0x0210b958,0x0210b97c),
    (0x0210bc88,0x0210bcc0), # cutscene animation/texture-index byte records
]


def generate(root):
    cfg = root / "config/arm9/overlays/ov002"
    blob = (root / "extracted/overlays/overlay_0002.bin").read_bytes()
    names, targets, relocs = {}, {}, {}
    inside = lambda address: any(a <= address < b for a,b in SPANS)
    for line in (cfg / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)",line)
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
        if m[2] != "load" or m[4] != "overlay(2)" or target not in targets:
            raise ValueError(f"unreviewed relocation: {line}")
        name,kind = targets[target]
        if kind != "bss" and not (kind.startswith("data") and inside(target)):
            raise ValueError(f"unreviewed target: {line}")
        if struct.unpack_from("<I",blob,addr-0x020ad660)[0] != target or addr % 4:
            raise ValueError(f"unexpected pointer encoding: {line}")
        relocs[addr] = name
    if len(relocs) != 32:
        raise ValueError(f"unexpected pointer count {len(relocs)}")
    lines = ['// Generated from local ROM; reviewed Player constants/resource tables.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.player_{start:08x},"aw",%progbits','.balign 4']
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
