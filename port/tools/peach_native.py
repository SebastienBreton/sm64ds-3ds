#!/usr/bin/env python3
"""Import Peach native PMFs, resource aliases and the original actor profile."""
from pathlib import Path
import argparse,struct
from actor_pmfs import generate as pmfs

def generate(root):
    blob=(root/'extracted/overlays/overlay_0085.bin').read_bytes();base=0x02129020
    handles=(0x021304d4,0x021304c4,0x021304e4,0x021304ec,0x021304cc,0x021304dc,0x021304bc)
    if struct.unpack_from('<7I',blob,0x0212f280-base)!=handles:raise ValueError('Peach animation handle order changed')
    if struct.unpack_from('<7I',blob,0x0212ff9c-base)!=(0x0212a684,0x00ac00ba,3,0x64000,0x64000,0x1000000,0x1000000):raise ValueError('Peach profile changed')
    text=pmfs(root,85,base,'src/__sinit_ov085_0212f3a0.c')
    text+='''
#include "daPeach_c.h"
static_assert(sizeof(daPeach_c)==0x36c,"native Peach actor layout");
extern "C" {
daPeach_c* daPeach_c_classInit();
'''
    for h in handles:text+=f'extern unsigned char data_ov085_{h:08x}[];\n'
    text+='void* data_ov085_0212f280[7]={'+','.join(f'data_ov085_{h:08x}' for h in handles)+'};\n'
    text+='''struct PeachProfile {daPeach_c*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(PeachProfile)==28,"Peach profile ABI");
PeachProfile g_profile_PEACH_PRINCESS={daPeach_c_classInit,186,172,3,0x64000,0x64000,0x1000000,0x1000000};
}
'''
    return text
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
