#!/usr/bin/env python3
"""Native Toad state/profile constants checked against overlay85 ROM records."""
from pathlib import Path
import argparse,struct

def generate(root):
    blob=(root/'extracted/overlays/overlay_0085.bin').read_bytes();base=0x02129020
    def words(at,n):return struct.unpack_from('<'+'I'*n,blob,at-base)
    expected=[0x0212943c,0,0x021291ac,0,0x021294f0,0,0x02129470,0]
    if words(0x0212fe30,8)!=tuple(expected):raise ValueError('Toad PMFs changed')
    if words(0x0212fe88,10)!=(0,0,0,0,0x0212fe28,0,0,0,0,0x0212fe20):raise ValueError('Toad state layout changed')
    if words(0x0212fe6c,7)!=(0x02129cd0,0x00ab00b9,3,0x40000,0x80000,0x1000000,0x1000000):raise ValueError('Toad profile changed')
    if blob[0x0212fe20-base:0x0212fe30-base]!=b'TALK\0\0\0\0WAIT\0\0\0\0':raise ValueError('Toad state names changed')
    requirements=blob[0x0212f27c-base:0x0212f280-base]
    return '''// Generated with ROM-checked member pointers and profile fields.
#include "daKinopio_c.h"
#include <cstdint>
static_assert(sizeof(daKinopio_c)==0x210,"native Toad layout");
static_assert(sizeof(daKinopio_c::State)==0x14,"native Toad state layout");
extern "C" {
daKinopio_c* daKinopio_c_classInit();
using StateFunction=daKinopio_c::StateFunc;
StateFunction data_ov085_0212fe30=&daKinopio_c::St_Talk_Init;
StateFunction data_ov085_0212fe38=&daKinopio_c::St_Talk_Main;
StateFunction data_ov085_0212fe40=&daKinopio_c::St_Idle_Init;
StateFunction data_ov085_0212fe48=&daKinopio_c::St_Idle_Main;
char data_ov085_0212fe20[8]="TALK",data_ov085_0212fe28[8]="WAIT";
daKinopio_c::State data_ov085_0212fe88[2]={
    {nullptr,nullptr,(u32)(uintptr_t)data_ov085_0212fe28},
    {nullptr,nullptr,(u32)(uintptr_t)data_ov085_0212fe20}};
struct ToadProfile {daKinopio_c*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(ToadProfile)==28,"Toad profile ABI");
ToadProfile g_profile_KINOPIO={daKinopio_c_classInit,185,171,3,0x40000,0x80000,0x1000000,0x1000000};
unsigned char data_ov085_0212f27c[4]={'''+','.join(str(x) for x in requirements)+'''};
}
'''
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
