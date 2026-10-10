#!/usr/bin/env python3
"""Import door resource variants, state PMFs and native BSS from local ROM."""
from pathlib import Path
import argparse
import re
import struct

# Door variant records only; executable code and actor vtables are linked natively.
SPANS = [(0x021480d0,0x021480d4),(0x02148204,0x02148334)]



def generate(root):
    cfg = root / "config/arm9/overlays/ov100"
    blob = (root / "extracted/overlays/overlay_0100.bin").read_bytes()
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
        if m[2] != "load" or m[4] != "overlay(100)" or target not in targets:
            raise ValueError(f"unreviewed relocation: {line}")
        name,kind = targets[target]
        if not (kind == "bss" and 0x02148704 <= target < 0x02148934):
            raise ValueError(f"unreviewed target: {line}")
        if struct.unpack_from("<I",blob,addr-0x02140d80)[0] != target or addr % 4:
            raise ValueError(f"unexpected pointer encoding: {line}")
        relocs[addr] = name
    if len(relocs) != 32:
        raise ValueError(f"unexpected pointer count {len(relocs)}")
    lines = ['// Generated from local ROM; door variants.', 'asm(R"DATA(']
    for start,end in SPANS:
        lines += [f'.pushsection .data.door_{start:08x},"aw",%progbits','.balign 4']
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
                raw=blob[cursor-0x02140d80:stop-0x02140d80]
                if len(raw)!=stop-cursor: raise ValueError("short ROM")
                lines.append('.byte '+','.join(str(v) for v in raw))
                cursor=stop
        lines.append('.popsection')
    lines += [')DATA");','']
    # GCC ARM PMFs are two words: native function pointer and zero adjustment.
    src=(root/'src/__sinit_ov100_02147698.c').read_text()
    pmfs=re.findall(r'extern struct P2 (data_ov100_\w+);',src)
    if len(pmfs)!=15: raise ValueError("unexpected door PMF count")
    callbacks=[]
    for name in pmfs:
        code,adjust=struct.unpack_from('<II',blob,int(name[-8:],16)-0x02140d80)
        if adjust or code not in targets or not targets[code][1].startswith('function'):
            raise ValueError("unsupported door callback")
        callbacks.append((name,targets[code][0]))
    lines += ['struct DoorCallbacks {']
    for i,(_,target) in enumerate(callbacks): lines.append(f'    int call_{i}(int) asm("{target}");')
    lines += ['};','using DoorCallback = int (DoorCallbacks::*)(int);',
              'static_assert(sizeof(DoorCallback)==8,"door PMF ABI");','extern "C" {']
    for i,(name,_) in enumerate(callbacks): lines.append(f'DoorCallback {name} = &DoorCallbacks::call_{i};')
    lines += ['}']
    return "\n".join(lines)


if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root",type=Path)
    ap.add_argument("output",type=Path)
    args=ap.parse_args()
    args.output.write_text(generate(args.root))
