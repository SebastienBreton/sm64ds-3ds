#!/usr/bin/env python3
"""ROM-checked Boo overlay numeric tables, profiles, PMFs and unified BSS.

Compiler-owned vtables/RTTI are excluded. Every imported pointer must resolve
inside a reviewed data island, BSS, or original function symbol in this overlay.
"""
import argparse,re,struct
from pathlib import Path
BASE=0x02115ee0
SPANS=[(0x0211e1c0,0x0211e29c),(0x0211e770,0x0211e824),
       (0x0211e9ac,0x0211e9cc),(0x0211e9e8,0x0211ea80),
       (0x0211eb10,0x0211eb1c),(0x0211eb34,0x0211eb50),
       (0x0211ebd8,0x0211ecf8),(0x0211ed10,0x0211ed2c)]
from native_overlay_data import generate as generate_native
def generate(root):
    extra=[]
    # Shared cap resource table used by the original dCapEnemy implementation.
    # Preserve the first adjacent word as well: retail AddCap failure reads [6].
    shared=(root/'extracted/overlays/overlay_0002.bin').read_bytes()
    cap=struct.unpack_from('<7I',shared,0x020ff028-0x020ad660)
    if cap!=(0x0210da40,0x0210d9a0,0x0210d9c0,0,0,0,0x01010000):raise ValueError('cap resource table changed')
    extra += ['asm(R"CAP(','.pushsection .data.cap_enemy_table,"aw",%progbits','.balign 4',
            '.global data_ov002_020ff028','data_ov002_020ff028:',
            '.word data_ov002_0210da40,data_ov002_0210d9a0,data_ov002_0210d9c0,0,0,0,0x01010000',
            '.popsection',')CAP");']
    return generate_native(root,63,BASE,SPANS,0x0211edc0,0x0211efe0,10,[(0x0211e9ac,0x0211e9cc),(0x0211ecd8,0x0211ecf8)],extra)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.write_text(generate(a.root))
