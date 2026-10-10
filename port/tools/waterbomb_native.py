#!/usr/bin/env python3
"""Relocate waterbomb states and validate profile/fragment angles against ROM."""
from pathlib import Path
import argparse, struct
from actor_pmfs import generate as pmfs

def generate(root):
    blob=(root/'extracted/overlays/overlay_0098.bin').read_bytes(); base=0x02137be0
    if struct.unpack_from('<7I',blob,0x0213c74c-base)!=(0x0213bf10,0x003000d0,2,0x64000,0x64000,0x1000000,0x640000):
        raise ValueError('Waterbomb profile changed')
    angles=struct.unpack_from('<6H',blob,0x0213bf90-base)
    if angles!=(0,12288,24576,36864,49152,0):raise ValueError('Fragment angles changed')
    text=pmfs(root,98,base,'src/__sinit_ov098_0213c2b4.c')
    text+='''
#include "common.h"
#include "daWbm_c.h"
static_assert(sizeof(daWbm_c)==0x3cc,"waterbomb actor ABI");
extern "C" {
int* daWbm_c_classInit();
struct WaterbombProfile {int*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(WaterbombProfile)==28,"waterbomb profile ABI");
WaterbombProfile g_profile_WATERBOMB={daWbm_c_classInit,208,48,2,0x64000,0x64000,0x1000000,0x640000};
u16 data_ov098_0213bf90[6]={0,12288,24576,36864,49152,0};
}
'''
    return text
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
