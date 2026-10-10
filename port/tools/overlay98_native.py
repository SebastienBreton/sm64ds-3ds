#!/usr/bin/env python3
"""Unified overlay98 BSS and the remaining native actor profiles."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    blob=(root/'extracted/overlays/overlay_0098.bin').read_bytes();base=0x02137be0
    start,end=0x0213c800,0x0213c960
    if base+len(blob)!=start:raise ValueError('overlay98 BSS origin changed')
    profiles=[('CANNON','daCnn_c_classInit',0x0213c67c,(0x0213b43c,0x2f00cf,3,0xc8000,0xc8000,0x1000000,0))]
    lines=['// Generated overlay98 storage and ROM-checked native profiles.',
           'struct Overlay98Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};',
           'static_assert(sizeof(Overlay98Profile)==28,"native profile ABI");','extern "C" {']
    for name,factory,address,expected in profiles:
        values=struct.unpack_from('<7I',blob,address-base)
        if values!=expected:raise ValueError('profile changed: '+name)
        lines += [f'void* {factory}();',f'Overlay98Profile g_profile_{name}={{'+','.join([factory,str(values[1]&65535),str(values[1]>>16)]+[hex(v) for v in values[2:]])+'};']
    lines+=['}','asm(R"BSS(','.pushsection .bss.overlay98,"aw",%nobits','.balign 4',
            '.global sm64ds_overlay98_bss_begin','sm64ds_overlay98_bss_begin:']
    labels={}
    for line in (root/'config/arm9/overlays/ov098/symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:bss addr:0x([0-9a-f]+)(?: ambiguous)?',line)
        if m:
            address=int(m[2],16)
            if not start<=address<end:raise ValueError('BSS symbol outside known overlay')
            labels.setdefault(address,[]).append(m[1])
    cursor=start
    for address,names in sorted(labels.items()):
        if address>cursor:lines.append(f'.zero {address-cursor}')
        for name in names:lines += [f'.global {name}',f'{name}:']
        cursor=address
    lines += [f'.zero {end-cursor}', '.global sm64ds_overlay98_bss_end',
              'sm64ds_overlay98_bss_end:', '.popsection',')BSS");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
