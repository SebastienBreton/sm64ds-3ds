#!/usr/bin/env python3
"""ROM-checked native wall-sign profile, collision offset and resource storage."""
from pathlib import Path
import argparse,struct

def generate(root):
    blob=(root/'extracted/overlays/overlay_0085.bin').read_bytes();base=0x02129020
    if struct.unpack_from('<7I',blob,0x021303dc-base)!=(0x0212f244,0x00a600b7,2,0x60000,0x300000,0x1000000,0):raise ValueError('Kanban profile changed')
    if struct.unpack_from('<3i',blob,0x0212f29c-base)!=(0,-0x28000,0):raise ValueError('Kanban collision offset changed')
    return '''// Generated from reviewed overlay85 records.
#include "daObjKanban_c.h"
static_assert(sizeof(daObjKanban_c)==0x368,"native wall-sign layout");
extern "C" {
daObjKanban_c* daObjKanban_c_classInit();
struct KanbanProfile {daObjKanban_c*(*create)();u16 behavior,render;u32 flags;int range,height,draw,secondary;};
static_assert(sizeof(KanbanProfile)==28,"Kanban profile ABI");
KanbanProfile g_profile_KANBAN={daObjKanban_c_classInit,183,166,2,0x60000,0x300000,0x1000000,0};
int data_ov085_0212f29c[3]={0,-0x28000,0};
}
'''
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
