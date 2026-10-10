#!/usr/bin/env python3
"""Import reviewed cutscene descriptors and shared-resource pointer lists."""
import argparse
import re
import struct
from pathlib import Path

ROOTS = [0x210b600,0x210b608,0x210b60c,0x210b610,0x210b6a8,
         0x210b97c,0x210b988,0x210b998,0x210bb7c,0x210bba0,0x210bcf0,
         0x210bd24,0x210bddc,0x210be1c]

def generate(root):
    blob=(root/'extracted/overlays/overlay_0002.bin').read_bytes()
    # ABI bridge tables in demo_models.cpp must keep the ROM slot order.
    reviewed_vtables = {
        0x210bae4:(0x20f6a00,0x20f69a8,0x2016bf8,0x2016c98,0x2016bb8,0x2016b78),
        0x210bcc4:(0x20f6870,0x20f6778,0x2016bf8,0x201686c,0x201682c,0x20167f8,0x20167c4),
        0x210bce8:(0x20f8848,0x20f8838),
    }
    for address,expected in reviewed_vtables.items():
        if struct.unpack_from('<'+'I'*len(expected),blob,address-0x20ad660)!=expected:
            raise ValueError('cutscene model vtable changed '+hex(address))
    symbols={}
    for line in (root/'config/arm9/overlays/ov002/symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?',line)
        if m:symbols[int(m[3],16)]=(m[1],m[2])
    addresses=sorted(symbols)
    relocs={}
    for line in (root/'config/arm9/overlays/ov002/relocs.txt').read_text().splitlines():
        m=re.fullmatch(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',line)
        if m:relocs[int(m[1],16)]=(m[2],int(m[3],16),m[4])
    # Already-live resource handles; their overlay initializers retain ownership.
    external={a:n for a,(n,k) in symbols.items() if k=='bss'}
    external.update({a:symbols[a][0] for a in (0x21001b0,0x2100200,0x2100250)})
    external.update({0x213073c:'data_ov085_0213073c',0x2130744:'data_ov085_02130744'})
    pending=list(ROOTS); selected={}; pointers={}
    while pending:
        start=pending.pop()
        if start in selected:continue
        name,kind=symbols[start]
        if not kind.startswith('data') or name.startswith('_ZT'):
            raise ValueError('non-descriptor '+name)
        end=0x210bd58 if start==0x210bd24 else addresses[addresses.index(start)+1]
        selected[start]=end
        for address,(kind,target,module) in relocs.items():
            if not start<=address<end:continue
            if kind!='load' or address%4 or address+4>end:
                raise ValueError('bad relocation '+hex(address))
            if struct.unpack_from('<I',blob,address-0x20ad660)[0]!=target:
                raise ValueError('pointer mismatch '+hex(address))
            if target in external:
                if module not in ('overlay(2)','overlay(85)'):raise ValueError('foreign resource')
                pointers[address]=external[target]
            elif module=='overlay(2)' and target in symbols:
                pointers[address]=symbols[target][0];pending.append(target)
            else:raise ValueError('unreviewed target '+hex(target))
    lines=['// Local ROM descriptors; no executable code or RTTI imported.','asm(R"DEMO(']
    for start,end in sorted(selected.items()):
        name=symbols[start][0]
        lines += [f'.pushsection .data.demo_{start:x},"aw",%progbits','.balign 4',f'.global {name}',name+':']
        cursor=start
        while cursor<end:
            if cursor in pointers:
                lines.append('.word '+pointers[cursor]);cursor+=4
            else:
                stop=min(cursor+16,end)
                stop=min([stop]+[a for a in pointers if cursor<a<stop])
                raw=blob[cursor-0x20ad660:stop-0x20ad660]
                if len(raw)!=stop-cursor:raise ValueError('short ROM')
                lines.append('.byte '+','.join(map(str,raw)));cursor=stop
        lines.append('.popsection')
    return '\n'.join(lines+[')DEMO");',''])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
