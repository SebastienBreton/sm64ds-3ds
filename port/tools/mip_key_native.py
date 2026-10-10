#!/usr/bin/env python3
"""Native rabbit-key callbacks, state storage and ROM-checked profile."""
from pathlib import Path
import argparse,struct
from actor_pmfs import generate as pmfs

def generate(root):
    blob=(root/'extracted/overlays/overlay_0085.bin').read_bytes()
    if struct.unpack_from('<7I',blob,0x021301b4-0x02129020)!=(0x0212d4e8,0x005400e5,2,0x64000,0x64000,0x1000000,0x1000000):raise ValueError('Rabbit-key profile changed')
    text=pmfs(root,85,0x02129020,'src/__sinit_ov085_0212f9bc.c')
    return text+'''
#include "daObj_Mip_Key_c.h"
static_assert(sizeof(daObj_Mip_Key_c)==0x1a0,"native rabbit-key layout");
extern "C" {
daObj_Mip_Key_c* daObj_Mip_Key_c_classInit();
struct MipKeyProfile {daObj_Mip_Key_c*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(MipKeyProfile)==28,"rabbit-key profile ABI");
MipKeyProfile g_profile_OBJ_MIP_KEY={daObj_Mip_Key_c_classInit,229,84,2,0x64000,0x64000,0x1000000,0x1000000};
}
'''
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
