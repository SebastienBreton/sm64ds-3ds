#!/usr/bin/env python3
"""Extract message tables and rebind the three ordinary text callbacks."""
from pathlib import Path
import argparse, re, struct
START, END, BASE = 0x0208ee44, 0x0208f274, 0x02004000
CALLBACKS = {0x0208ee80: 0x0201aca4, 0x0208ee84: 0x0201ab6c, 0x0208ee88: 0x0201adac}
def generate(root):
    cfg = root / "config/arm9"
    blob = (root / "extracted/arm9_dec.bin").read_bytes()
    names, functions, relocs = {}, {}, {}
    for line in (cfg / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)", line)
        if not m: continue
        name,kind,addr=m[1],m[2],int(m[3],16)
        if kind.startswith("function"): functions[addr]=name
        if START <= addr < END:
            if not kind.startswith("data"): raise ValueError(line)
            names.setdefault(addr,[]).append(name)
    for line in (cfg / "relocs.txt").read_text().splitlines():
        m = re.match(r"from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)",line)
        if not m or not START <= int(m[1],16) < END: continue
        addr,target=int(m[1],16),int(m[3],16)
        if m[2] != "load" or m[4] != "main" or CALLBACKS.get(addr) != target:
            raise ValueError(f"unreviewed relocation {line}")
        if struct.unpack_from("<I",blob,addr-BASE)[0] != target: raise ValueError("ROM pointer mismatch")
        relocs[addr]=functions[target]
    if set(relocs)!=set(CALLBACKS): raise ValueError("missing text callback")
    lines=['// Generated local ROM message tables with native function relocations.',
           'asm(R"DATA(', '.pushsection .data.message_tables,"aw",%progbits', '.balign 4']
    cursor=START
    while cursor<END:
        for name in names.get(cursor,[]): lines.extend([f'.global {name}',f'.type {name}, %object',f'{name}:'])
        if cursor in relocs:
            lines.append(f'.word {relocs[cursor]}');cursor+=4
        else:
            stop=min(cursor+16,END)
            for boundary in range(cursor+1,stop):
                if boundary in names or boundary in relocs: stop=boundary;break
            raw=blob[cursor-BASE:stop-BASE]
            if len(raw)!=stop-cursor: raise ValueError("short ROM")
            lines.append('.byte '+','.join(str(b) for b in raw));cursor=stop
    lines.extend(['.popsection', ')DATA");',''])
    return "\n".join(lines)
if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("root",type=Path);ap.add_argument("output",type=Path)
    a=ap.parse_args();a.output.write_text(generate(a.root))
