#!/usr/bin/env python3
"""Preserve native storage for reviewed DS input records and VRAM-bank state."""
from pathlib import Path
import argparse,re
SPANS=[(0x020a1040,0x020a1064),(0x020a1154,0x020a11e4),(0x020a6088,0x020a60a8)]
def generate(config):
    names={}
    for line in (config/'symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:bss addr:0x([0-9a-f]+)',line)
        if m:names.setdefault(int(m[2],16),[]).append(m[1])
    lines=['// Generated BSS only: native input polling and GX init remain required.','asm(R"BSS(']
    for start,end in SPANS:
        if start not in names or end not in names:raise ValueError('unexpected layout')
        lines += [f'.pushsection .bss.platform_{start:08x},"aw",%nobits','.balign 4']
        cursor=start
        for a,syms in sorted(names.items()):
            if not start<=a<end:continue
            if cursor<a:lines.append(f'.zero {a-cursor}')
            for n in syms:lines += [f'.global {n}',f'.type {n}, %object',f'{n}:']
            cursor=a
        lines += [f'.zero {end-cursor}', '.popsection']
    lines += [')BSS");','']
    return '\n'.join(lines)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('config',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.config))
