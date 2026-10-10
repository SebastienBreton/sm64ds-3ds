#!/usr/bin/env python3
"""Unified overlay62 BSS and the remaining native actor profiles."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    blob=(root/'extracted/overlays/overlay_0062.bin').read_bytes();base=0x02115ee0
    start,end=0x0211dde0,0x0211e1a0
    if base+len(blob)!=start:raise ValueError('overlay62 BSS origin changed')
    profiles=[
        ('HOLHEI','daHolhei_c_classInit',0x0211d9bc,(0x02117478,0x6400be,0x10000003,0xc8000,0x320000,0x1000000,0x1000000)),
        ('NOKONOKO','daNknk_c_classInit_NOKONOKO',0x0211da74,(0x0211970c,0x1b00cb,0x10000006,0x64000,0x64000,0x1000000,0x640000)),
        ('NOKONOKO_S','daNknk_c_classInit_NOKONOKO_S',0x0211da90,(0x021196bc,0x1c00cc,0x10000006,0x32000,0x32000,0x6a4000,0x800000)),
        ('RACE_NOKO','daRNk_c_classInit',0x0211db78,(0x0211aee0,0x1d00bc,0x800002,0xc8000,0xc8000,0x17d0000,0x17d0000)),
        ('RACE_FLAG','daRFlag_c_classInit',0x0211dc30,(0x0211b208,0x1e00cd,2,0x190000,0x190000,0x13e8000,0x13e8000)),
        ('JANGO','daJango_c_classInit',0x0211dd38,(0x0211ce80,0x5e00ef,0x10000002,0x64000,0x258000,0x1f40000,0x1f40000))]

    arm9=(root/'extracted/arm9_dec.bin').read_bytes()
    for name,factory,address,expected in profiles:
        actor_id=expected[1]&65535
        if struct.unpack_from('<I',arm9,0x02090864-0x02004000+actor_id*4)[0]!=address:
            raise ValueError('actor spawn-table index changed: '+name)
    lines=['// Generated overlay62 storage and ROM-checked native profiles.',
           'struct Overlay62Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};',
           'static_assert(sizeof(Overlay62Profile)==28,"native profile ABI");','extern "C" {']
    for name,factory,address,expected in profiles:
        values=struct.unpack_from('<7I',blob,address-base)
        if values!=expected:raise ValueError('profile changed: '+name)
        lines += [f'void* {factory}();',f'Overlay62Profile g_profile_{name}={{'+','.join([factory,str(values[1]&65535),str(values[1]>>16)]+[hex(v) for v in values[2:]])+'};']
    lines+=['}','asm(R"BSS(','.pushsection .bss.overlay62,"aw",%nobits','.balign 4',
            '.global sm64ds_overlay62_bss_begin','sm64ds_overlay62_bss_begin:']
    labels={}
    for line in (root/'config/arm9/overlays/ov062/symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:bss addr:0x([0-9a-f]+)(?: ambiguous)?',line)
        if m:
            address=int(m[2],16)
            if not start<=address<end:raise ValueError('BSS symbol outside known overlay')
            labels.setdefault(address,[]).append(m[1])
    cursor=start
    for address,names in sorted(labels.items()):
        if address>cursor:lines.append(f'.zero {address-cursor}')
        for name in names:lines += [f'.global {name}',f'{name}:']
        cursor=address
    lines += [f'.zero {end-cursor}', '.global sm64ds_overlay62_bss_end',
              'sm64ds_overlay62_bss_end:', '.popsection',')BSS");']
    targets={int(m[2],16):m[1] for m in re.finditer(
        r"^(\w+) kind:function\S* addr:0x([0-9a-f]+)",
        (root/'config/arm9/overlays/ov062/symbols.txt').read_text(),re.M)}
    callbacks=[]
    for first,last in [(0x0211d900,0x0211d9a0),(0x0211db30,0x0211db60),(0x0211dcd0,0x0211dd20)]:
        for address in range(first,last,8):
            code,adjust=struct.unpack_from('<II',blob,address-base)
            if adjust or code not in targets:raise ValueError(f'unsupported PMF at{address:x}')
            callbacks.append((address,targets[code]))
    lines+=['struct Overlay62States {']
    for i,(_,target) in enumerate(callbacks):lines.append(f' void state{i}() asm("{target}");')
    lines+=['};','using Overlay62State=void (Overlay62States::*)();',
            'static_assert(sizeof(Overlay62State)==8,"ARM PMF ABI");','extern "C" {']
    for i,(address,_) in enumerate(callbacks):
        lines.append(f'Overlay62State data_ov062_{address:08x}=&Overlay62States::state{i};')
    lines+=['}']
    # Koopa resources and per-animation flags; every pointer is reviewed.
    expected=[0x0211df40,0x0211df48,0x0210da18,0x0210da20,0x0211df70,
              0x0211df38,0x0211df50,0x0211df60,0x0211df78,0x0211df68,
              0x0211df28,0x0211df30,0x0211df58]
    if list(struct.unpack_from('<13I',blob,0x0211ced8-base))!=expected:
        raise ValueError('Koopa resource table changed')
    relocs={int(m[1],16):(int(m[2],16),int(m[3])) for m in re.finditer(
        r'from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:overlay\((\d+)\)',
        (root/'config/arm9/overlays/ov062/relocs.txt').read_text())
        if 0x0211ced8<=int(m[1],16)<0x0211cf30}
    if len(relocs)!=13:raise ValueError('Koopa resource relocation count changed')
    lines+=['asm(R"KOOPA(','.pushsection .data.overlay62_tables,"aw",%progbits','.balign 4']
    for address in range(0x0211ced8,0x0211cf30,4):
        if address in (0x0211ced8,0x0211cee0,0x0211cee8,0x0211cf0c):
            lines += [f'.global data_ov062_{address:08x}',f'data_ov062_{address:08x}:']
        value=struct.unpack_from('<I',blob,address-base)[0]
        if address in relocs:
            target,overlay=relocs[address]
            if target!=value or overlay!=(2 if address in (0x0211cee0,0x0211cee4) else 62):
                raise ValueError('Koopa resource relocation mismatch')
            lines.append(f'.word data_ov{overlay:03}_{target:08x}')
        else:lines.append(f'.word 0x{value:08x}')
    lines+=['.popsection',')KOOPA");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
