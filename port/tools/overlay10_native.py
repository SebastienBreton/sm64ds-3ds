#!/usr/bin/env python3
"""ROM-checked castle interior storage, profiles and native PMF constants."""
import argparse,re,struct
from pathlib import Path
from native_overlay_data import emit_bss

def generate(root):
    base,start,end=0x021111a0,0x02112d00,0x02112d80
    blob=(root/'extracted/overlays/overlay_0010.bin').read_bytes()
    if len(blob)!=start-base:raise ValueError('overlay10 image layout changed')
    lines=['// Generated from the local ROM; original actor code stays unchanged.',
           '#include "daObjC1_Trap_c.h"',
           'using TrapState=void (daObjC1_Trap_c::*)();',
           'static_assert(sizeof(TrapState)==8,"native member pointer ABI");',
           'extern "C" {']
    for address,state,target in [(0x2112a78,0,0x2111320),(0x2112a80,2,0x2111284),(0x2112a88,1,0x21112b4),(0x2112a90,4,0x211124c),(0x2112a98,3,0x211125c)]:
        if struct.unpack_from('<2I',blob,address-base)!=(target,0):raise ValueError('trap state PMF changed')
        lines.append(f'TrapState data_ov010_{address:08x}=&daObjC1_Trap_c::State{state};')
    lines+=['struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int rangeOffset,range,draw,secondary;};','static_assert(sizeof(Profile)==28,"profile ABI");']
    arm9=(root/'extracted/arm9_dec.bin').read_bytes()
    for name,factory,address,actor,target in [('C1_HIKARI','daObjC1Hikari_c_classInit',0x2112b84,37,0x2111dd0),('C1_PEACH','daObjC1Peach_c_classInit',0x2112c44,38,0x2112004)]:
        v=struct.unpack_from('<7I',blob,address-base)
        if v[0]!=target or v[1]&65535!=actor or struct.unpack_from('<I',arm9,0x02090864+actor*4-0x02004000)[0]!=address:raise ValueError('actor profile changed')
        lines += [f'void* {factory}();',f'Profile g_profile_{name}={{'+','.join([factory,str(v[1]&65535),str(v[1]>>16)]+[hex(x) for x in v[2:]])+'};']
    if struct.unpack_from('<7I',blob,0x2112ac0-base)!=(0x2111998,0x1430024,0,0,0x1f4000,0,0):raise ValueError('native trap profile changed')
    cfg=(root/'config/arm9/overlays/ov010/symbols.txt').read_text()
    labels={}
    for name,addr in re.findall(r'^(\w+) kind:bss addr:0x([0-9a-f]+)(?: ambiguous)?$',cfg,re.M):
        address=int(addr,16)
        if not start<=address<end:raise ValueError('BSS label outside overlay')
        labels.setdefault(address,[]).append(name)
    lines+=['}', 'asm(R"BSS(']+emit_bss(10,labels,start,end)+[')BSS");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.write_text(generate(a.root))
