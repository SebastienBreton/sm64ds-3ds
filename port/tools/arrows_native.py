#!/usr/bin/env python3
"""Native arrow signs: ROM-checked profiles, resource columns and defined GCC returns."""
from pathlib import Path
import argparse, struct, re

def generate(root):
    b=(root/'extracted/overlays/overlay_0098.bin').read_bytes();base=0x2137be0
    pointers=(0x213c810,0x213c800,0x210d914,0x213c808,0x213c818,0x210d734)
    if struct.unpack_from('<6I',b,0x213c380-base)!=pointers:raise ValueError('Arrow resource table changed')
    for a,factory,ident,priority in [(0x213c398,0x2138008,299,188),(0x213c3b4,0x2137fd0,300,189)]:
        if struct.unpack_from('<7I',b,a-base)!=(factory,ident+(priority<<16),2,0xc0000,0xe0000,0x1000000,0x1000000):raise ValueError('Arrow profile changed')
    # Original no-op preserves r0; Bob-omb path forwards virtual slot31 result.
    attack=(0xe92d4000,0xe24dd004,0xe1d110bc,0xe35100ce,0x03a01001,0x13a01000,
            0xe3510000,0x028dd004,0x08bd4000,0x012fff1e,0xe5901000,0xe591107c,
            0xe12fff31,0xe28dd004,0xe8bd4000,0xe12fff1e)
    if struct.unpack_from('<16I',b,0x2137d40-base)!=attack:raise ValueError('Arrow attack ARM body changed')
    source=(root/'src/actors/daObjYajirusi_c.cpp').read_text()
    old="""    unsigned r = (other.actorID == 0xce) ? 1u : 0u;
    if (r == 0)
        return;
    Kill();"""
    new="""    // Original ARM returns this for the ignored no-op result. Explicit return
    // prevents GCC undefined behavior. Native virtual dispatch keeps Kill's ABI.
    if (other.actorID == 0xce) {
        typedef int (*KillRegisterCall)(void*);
        return (*(KillRegisterCall**)this)[31](this);
    }
    return (int)(unsigned long)this;"""
    if source.count(old)!=1:raise ValueError('Arrow attack callback changed')
    source=source.replace(old,new)
    lines=[source,'static_assert(sizeof(daObjYajirusi_c)==0x380,"arrow actor ABI");','extern "C" {']
    for a in pointers:
        ov=2 if a<0x2130000 else 98
        lines.append(f'extern unsigned char data_ov{ov:03d}_{a:08x}[];')
    lines+=['struct ArrowProfile {int*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};','static_assert(sizeof(ArrowProfile)==28,"arrow profile ABI");']
    for suffix,ident,priority in [('L',299,188),('R',300,189)]:lines.append(f'ArrowProfile g_profile_YAJIRUSI_{suffix}={{daObjYajirusi_c_classInit_YAJIRUSI_{suffix},{ident},{priority},2,0xc0000,0xe0000,0x1000000,0x1000000}};')
    lines+=['}', 'asm(R"ARROWS(','.pushsection .data.arrow_resources,"aw",%progbits','.balign 4']
    for i,a in enumerate(pointers):
        if i<3:
            name=f'data_ov098_{0x213c380+i*4:08x}'
            lines += [f'.global {name}',f'{name}:']
        ov=2 if a<0x2130000 else 98
        lines.append(f'.word data_ov{ov:03d}_{a:08x}')
    lines += ['.popsection', ')ARROWS\");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
