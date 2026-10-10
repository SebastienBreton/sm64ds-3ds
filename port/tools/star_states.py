#!/usr/bin/env python3
"""Relocate the fourteen original power-star member-function pointers."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    cfg=(root/'config/arm9/overlays/ov002/symbols.txt').read_text()
    targets={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:function\S* addr:0x([0-9a-f]+)',cfg,re.M)}
    blob=(root/'extracted/overlays/overlay_0002.bin').read_bytes()
    lines=['// ROM-checked power-star state table.','struct StarStates {']
    for i,a in enumerate(range(0x0210a990,0x0210aa00,8)):
        code,adjust=struct.unpack_from('<II',blob,a-0x020ad660)
        if adjust or not targets.get(code,'').startswith('_ZN8daStar_c'):raise ValueError('unsupported star PMF')
        lines.append(f'void state{i}() asm("{targets[code]}");')
    lines+=['};','using State=void (StarStates::*)();','static_assert(sizeof(State)==8,"ARM PMF ABI");','extern "C" {']
    for i,a in enumerate(range(0x0210a990,0x0210aa00,8)):
        lines.append(f'State data_ov002_{a:08x}=&StarStates::state{i};')
    vector=struct.unpack_from('<3i',blob,0x0210aa0c-0x020ad660)
    lines.append('int data_ov002_0210aa0c[3]={'+','.join(map(str,vector))+'};')
    first,last=0x02082714,0x02082a1f
    for m in re.finditer(r'from:0x([0-9a-f]+)',(root/'config/arm9/relocs.txt').read_text()):
        if first<=int(m[1],16)<last:raise ValueError('numeric star tuning table contains relocation')
    arm9=(root/'extracted/arm9_dec.bin').read_bytes()
    data=arm9[first-0x02004000:last-0x02004000]
    if len(data)!=last-first:raise ValueError('short tuning table')
    lines.append('alignas(4) unsigned char data_02082714[]={'+','.join(map(str,data))+'};')
    return '\n'.join(lines+['}'])+'\n' 
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
