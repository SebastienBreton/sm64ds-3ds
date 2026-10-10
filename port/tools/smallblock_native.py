#!/usr/bin/env python3
"""Relocate original small-block states, profile and numeric tables."""
from pathlib import Path
import argparse,struct
from actor_pmfs import generate as pmfs

def generate(root):
    b=(root/'extracted/overlays/overlay_0098.bin').read_bytes();base=0x2137be0
    if struct.unpack_from('<7I',b,0x213c510-base)!=(0x2139f04,0x2500c2,0x82,0x32000,0x64000,0x1000000,0x1000000):raise ValueError('Block profile changed')
    if struct.unpack_from('<3I',b,0x213c4c8-base)!=(0x213c850,0x213c858,0x210d934):raise ValueError('Block resource table changed')
    text=pmfs(root,98,base,'src/__sinit_ov098_0213c058.c')
    text+='''
#include "common.h"
#include "daObjBlockS_c.h"
static_assert(sizeof(daObjBlockS_c)==0x608,"small block ABI");
extern "C" {
daObjBlockS_c* daObjBlockS_c_classInit();
extern unsigned char data_ov098_0213c850[],data_ov098_0213c858[],data_ov002_0210d934[];
void* data_ov098_0213c4c8[]={data_ov098_0213c850,data_ov098_0213c858,data_ov002_0210d934};
struct BlockProfile {daObjBlockS_c*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(BlockProfile)==28,"small block profile ABI");
BlockProfile g_profile_BLOCK_S={daObjBlockS_c_classInit,194,37,0x82,0x32000,0x64000,0x1000000,0x1000000};
}
'''
    lines=['asm(R"BLOCK(','.pushsection .data.smallblock_numeric,"aw",%progbits','.balign 4']
    for start,end,aliases in [(0x213bf60,0x213bf90,[0x213bf60,0x213bf64,0x213bf68]),(0x213c4e0,0x213c500,[0x213c4e0,0x213c4f0])]:
        for a in range(start,end,4):
            if a in aliases:
                name=f'data_ov098_{a:08x}';lines += [f'.global {name}',f'{name}:']
            lines.append(f'.word {struct.unpack_from("<I",b,a-base)[0]}')
    lines += ['.popsection',')BLOCK");']
    return text+'\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
