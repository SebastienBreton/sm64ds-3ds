#!/usr/bin/env python3
"""Relocate reviewed nonvirtual actor state PMFs from a local overlay."""
import argparse
import re
import struct
from pathlib import Path

def generate(root, overlay, base, initializer, bss_start=None, bss_end=None):
    config=root/f"config/arm9/overlays/ov{overlay:03d}/symbols.txt"
    targets={int(m[2],16):m[1] for m in re.finditer(
        r"^(\w+) kind:function\S* addr:0x([0-9a-f]+)",config.read_text(),re.M)}
    blob=(root/f"extracted/overlays/overlay_{overlay:04d}.bin").read_bytes()
    source=(root/initializer).read_text()
    # Some original initializers spell the two-word record with a typedef
    # (coin uses Pair), others use struct P2 or S8. All candidate records still
    # require an exact ROM function target and zero this-adjustment below.
    aliases = re.findall(r"typedef\s+struct\s*(?:\w+\s*)?\{[^}]*\}\s*(\w+)\s*;", source)
    types = r"struct \w+|S8" + ''.join('|' + re.escape(name) for name in aliases)
    names=[]
    for declaration in re.findall(r"extern (?:" + types + r") ([^;]+);",source):
        if "[" not in declaration:
            names.extend(re.findall(r"\bdata_ov\d+_[0-9a-f]+\b",declaration))
    callbacks=[]
    for name in names:
        address=int(name[-8:],16)
        # Initializer destination may be declared without an array extent.
        if address-base>=len(blob):
            continue
        code,adjust=struct.unpack_from('<II',blob,address-base)
        if adjust or code not in targets:
            raise ValueError(f"unsupported member pointer {name}: {code:x}, {adjust:x}")
        callbacks.append((name,targets[code]))
    if not callbacks:
        raise ValueError("no member pointers found")
    lines=['// Generated native PMFs; no DS executable addresses.',
           'struct NativeActorStates {']
    for i,(_,target) in enumerate(callbacks):
        lines.append(f' void state{i}() asm("{target}");')
    lines+=['};','using State=void (NativeActorStates::*)();',
            'static_assert(sizeof(State)==8,"ARM PMF ABI");','extern "C" {']
    for i,(name,_) in enumerate(callbacks):
        lines.append(f'State {name}=&NativeActorStates::state{i};')
    lines+=['}']
    if bss_start is not None:
        assert bss_end is not None and bss_start<bss_end
        labels={int(m[2],16):m[1] for m in re.finditer(
            r'^(\w+) kind:bss addr:0x([0-9a-f]+)',config.read_text(),re.M)
            if bss_start<=int(m[2],16)<bss_end}
        assert bss_start in labels
        lines+=['asm(R"BSS(',f'.pushsection .bss.actor_states_{overlay},"aw",%nobits','.balign 4']
        cursor=bss_start
        for address,name in sorted(labels.items()):
            if address>cursor: lines.append(f'.zero {address-cursor}')
            lines += [f'.global {name}',f'{name}:'];cursor=address
        lines += [f'.zero {bss_end-cursor}', '.popsection', ')BSS");']
    return '\n'.join(lines)+'\n'

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('overlay',type=int)
    p.add_argument('base',type=lambda v:int(v,0))
    p.add_argument('initializer')
    p.add_argument('output',type=Path)
    p.add_argument('--bss-start',type=lambda v:int(v,0))
    p.add_argument('--bss-end',type=lambda v:int(v,0))
    a=p.parse_args()
    a.output.write_text(generate(a.root,a.overlay,a.base,a.initializer,a.bss_start,a.bss_end))
