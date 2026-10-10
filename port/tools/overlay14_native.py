#!/usr/bin/env python3
"""ROM-checked Bob level actor islands, profiles, PMFs and unified BSS.

Private closure preparation only until integrated and lifecycle/runtime gated.
Compiler-owned RTTI/vtables and existing numeric level islands are excluded.
"""
import argparse
from pathlib import Path
from native_overlay_data import generate as generate_native

BASE=0x021111a0
# Three collision descriptors, resource descriptor, three original profiles,
# state names/nonvirtual PMFs/offset vector, and the six writable 20-byte states.
SPANS=[(0x02114558,0x021145b8),(0x021145c4,0x021145d0),
       (0x021145e4,0x02114600),(0x02114688,0x02114718),
       (0x02114724,0x0211473c),(0x02114750,0x0211476c),
       (0x0211476c,0x021147e4),(0x0211488c,0x021148a8)]
PMFS=[(0x021146a0,0x02114700)]


def generate(root, level_data_output=None):
    native=generate_native(root,14,BASE,SPANS,0x02114940,0x021149e0,3,PMFS)
    if level_data_output is not None:
        # These collision bytes already belong to the live numeric level island.
        # Validate the full native import first, then relocate its three public
        # names onto that exact storage instead of allocating a second copy.
        from all_level_data import collect,generate as generate_levels
        level=next(row for row in collect(root) if row[0]==14)
        assert level[3]==[(0x02113380,0x021145b8)]
        code=generate_levels(root)
        label='level_14_02113380:\n'
        assert code.count(label)==1
        names=[f'data_ov014_{address:08x}' for address in range(0x02114558,0x021145b8,32)]
        aliases=''.join(f'.global {name}\n.set {name},level_14_02113380+{address-0x02113380}\n'
            for name,address in zip(names,range(0x02114558,0x021145b8,32)))
        code=code.replace(label,label+aliases,1)
        start=native.index('.pushsection .data.overlay14_2114558,')
        end=native.index('.popsection',start)+len('.popsection\n')
        block=native[start:end]
        assert block.count('.word ')==24
        assert all(f'.global {name}\n{name}:' in block for name in names)
        native=native[:start]+native[end:]
        level_data_output.write_text(code)
    return native


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--reuse-level-data',type=Path,required=True,
        help='Emit private numeric level data with shared CLPS aliases; remove duplicate actor storage')
    a=p.parse_args();a.output.write_text(generate(a.root,a.reuse_level_data))
