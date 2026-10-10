#!/usr/bin/env python3
"""ROM-checked Mole, block and painting overlay numeric tables, profiles, PMFs and unified BSS.

Compiler-owned vtables/RTTI are excluded. Every imported pointer must resolve
inside a reviewed data island, BSS, or original function symbol in this overlay.
"""
import argparse,re,struct
from pathlib import Path
BASE=0x02123740
SPANS=[(0x0212766c,0x021278c0),(0x02127f80,0x02127fb0),
       (0x02127fec,0x02128024),(0x0212812c,0x0212815c),
       (0x02128174,0x02128190),(0x02128214,0x02128274),
       (0x02128290,0x021282ac)]
from native_overlay_data import generate as generate_native
def generate(root):
    return generate_native(root,80,BASE,SPANS,0x021283c0,0x02128860,4,[(0x02127f80,0x02127fb0),(0x0212812c,0x0212815c),(0x02128214,0x02128274)])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.write_text(generate(a.root))
