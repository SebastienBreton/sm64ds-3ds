#!/usr/bin/env python3
"""ROM-checked profiles, states and unified BSS for the owl overlay."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    base,start,end=0x02135700,0x02136ae0,0x02136b80
    blob=(root/'extracted/overlays/overlay_0094.bin').read_bytes()
    if base+len(blob)!=start:raise ValueError('overlay94 extent changed')
    cfg=(root/'config/arm9/overlays/ov094/symbols.txt').read_text()
    arm9=(root/'extracted/arm9_dec.bin').read_bytes()
    profiles=[('OWL',0x02136a34,(0x02136798,0x5900ea,0,0x64000,0xc8000,0x1000000,0x1000000))]
    lines=['// Generated from local ROM.','extern "C" {',
           'struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};',
           'static_assert(sizeof(Profile)==28,"profile ABI");']
    for name,address,expected in profiles:
        values=struct.unpack_from('<7I',blob,address-base)
        if values!=expected:raise ValueError('profile changed')
        if struct.unpack_from('<I',arm9,0x02090864-0x02004000+4*(values[1]&65535))[0]!=address:raise ValueError('profile index changed')
        factory='daOwl_c_classInit'
        lines += [f'void* {factory}();',f'Profile g_profile_{name}={{'+','.join([factory,str(values[1]&65535),str(values[1]>>16)]+[hex(v) for v in values[2:]])+'};']
    lines+=['}','asm(R"BSS(','.pushsection .bss.overlay94,"aw",%nobits','.balign 4',
            '.global sm64ds_overlay94_bss_begin','sm64ds_overlay94_bss_begin:']
    labels={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:bss addr:0x([0-9a-f]+)',cfg,re.M)}
    cursor=start
    for address,name in sorted(labels.items()):
        if not cursor<=address<end:raise ValueError('BSS label outside extent')
        if address>cursor:lines.append(f'.zero {address-cursor}')
        lines += [f'.global {name}',name+':'];cursor=address
    lines += [f'.zero {end-cursor}', '.global sm64ds_overlay94_bss_end','sm64ds_overlay94_bss_end:', '.popsection',')BSS");']
    targets={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:function\S* addr:0x([0-9a-f]+)',cfg,re.M)}
    lines+=['struct States {']
    for i,address in enumerate(range(0x021369c0,0x02136a10,8)):
        code,adjust=struct.unpack_from('<II',blob,address-base)
        if adjust or code not in targets:raise ValueError('unsupported state PMF')
        lines.append(f'void state{i}() asm("{targets[code]}");')
    lines+=['};','using State=void (States::*)();','static_assert(sizeof(State)==8,"ARM PMF ABI");','extern "C" {']
    for i,address in enumerate(range(0x021369c0,0x02136a10,8)):
        lines.append(f'State data_ov094_{address:08x}=&States::state{i};')
    vector=struct.unpack_from('<3i',blob,0x02136a1c-base)
    if vector!=(0,-131072,0):raise ValueError('owl offset changed')
    lines+=['int data_ov094_02136a1c[3]={'+','.join(map(str,vector))+'};','}']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
