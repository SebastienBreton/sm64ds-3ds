#!/usr/bin/env python3
"""Allocate the ARM9 scene flags, transition state and file storage with original aliases.

Storage only: scene/resource initialization still must run before drawing.
The span ends at the next configured object,0209d5b8.
"""
from pathlib import Path
import argparse
import re
START, END = 0x0209d454, 0x0209d5b8

def generate(config):
    symbols = {}
    for line in (config / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"(\w+) kind:bss addr:0x([0-9a-f]+)", line)
        if m and START <= int(m[2],16) < END:
            symbols.setdefault(int(m[2],16), []).append(m[1])
    if START not in symbols or 0x0209d4b0 not in symbols or 0x0209d574 not in symbols:
        raise ValueError("unexpected scene BSS layout")
    lines = ['// Generated scene BSS: flags, fader pointers and file state.',
             'asm(R"BSS(', '.pushsection .bss.scene_state,"aw",%nobits', '.balign 4']
    cursor = START
    for address,names in sorted(symbols.items()):
        if address > cursor: lines.append(f'.zero {address-cursor}')
        for name in names: lines.extend([f'.global {name}',f'.type {name}, %object',f'{name}:'])
        cursor = address
    lines.extend([f'.zero {END-cursor}', '.popsection', ')BSS");', ''])
    return "\n".join(lines)

if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("config",type=Path);ap.add_argument("output",type=Path)
    args=ap.parse_args();args.output.write_text(generate(args.config))
