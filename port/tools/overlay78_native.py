#!/usr/bin/env python3
"""Private ROM-checked King Bob-omb PMFs, offset vector and unified BSS.

The original C++ TU owns profile/RTTI/vtable. No dispatcher support is implied.
"""
import argparse,struct
from pathlib import Path
from native_overlay_data import generate as generate_native

BASE=0x02123740
SPANS=[(0x02126ce0,0x02126e0c),(0x02126ec8,0x02126ee0)]
PMFS=[(0x02126ce0,0x02126e00)]


def generate(root):
    blob=(root/'extracted/overlays/overlay_0078.bin').read_bytes()
    # The linked original C++ SpawnInfo already provides these exact scalars.
    assert struct.unpack_from('<7I',blob,0x02126e28-BASE)==(
        0x021265fc,0x002c00bd,0x10000003,0,0x00190000,0x01fa0000,0x01fa0000)
    return generate_native(root,78,BASE,SPANS,0x02126ee0,0x02127140,1,PMFS,
        extra_lines=['#include "daBombking_c.h"',
            'static_assert(sizeof(void*)==4,"native pointer word ABI");',
            'static_assert(sizeof(daBombking_c::StateFunction)==8,"native nonvirtual PMF ABI");'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();assert not a.output.exists();a.output.write_text(generate(a.root))
