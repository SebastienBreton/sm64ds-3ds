#!/usr/bin/env python3
"""Relocate reviewed IronBall, Chomp and path-lift state data from the local ROM."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    lines=['// ROM-checked native overlay100 dependencies.']
    for ov,base,ranges in [(100,0x02140d80,[(0x02147f18,0x02147f40),(0x02147ff8,0x02148008)]),
                           (2,0x020ad660,[(0x0210aed0,0x0210af00)])]:
        cfg=(root/f'config/arm9/overlays/ov{ov:03}/symbols.txt').read_text()
        targets={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:function\S* addr:0x([0-9a-f]+)',cfg,re.M)}
        blob=(root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
        callbacks=[]
        for first,last in ranges:
            for a in range(first,last,8):
                code,adjust=struct.unpack_from('<II',blob,a-base)
                if adjust or code not in targets:raise ValueError('unsupported PMF')
                callbacks.append((a,targets[code]))
        lines.append(f'struct States{ov} {{')
        for i,(_,target) in enumerate(callbacks):lines.append(f'void state{i}() asm("{target}");')
        lines += ['};',f'using State{ov}=void (States{ov}::*)();',f'static_assert(sizeof(State{ov})==8,"PMF ABI");','extern "C" {']
        for i,(a,_) in enumerate(callbacks):lines.append(f'State{ov} data_ov{ov:03}_{a:08x}=&States{ov}::state{i};')
        lines.append('}')
    b100=(root/'extracted/overlays/overlay_0100.bin').read_bytes()
    b2=(root/'extracted/overlays/overlay_0002.bin').read_bytes();base=0x020ad660
    lines.append('extern "C" {')
    for name,blob,offset,expected in [('data_ov100_02148008',b100,0x02148008-0x02140d80,(0,-655360,0)),
                                     ('data_ov002_0210af00',b2,0x0210af00-base,(8192,8192,8192))]:
        values=struct.unpack_from('<3i',blob,offset)
        if values!=expected:raise ValueError('vector changed')
        lines.append(f'int {name}[3]={{'+','.join(map(str,values))+'};')
    for a in [0x0210aeb8,0x0210aec0,0x0210aec8]:
        raw=b2[a-base:a-base+8]
        if 0 not in raw:raise ValueError('unterminated path state name')
        lines.append(f'char data_ov002_{a:08x}[]={{'+','.join(map(str,raw))+'};')
    records=struct.unpack_from('<15I',b2,0x0210af2c-base)
    expected=[0,0,0,0,0x0210aec0,0,0,0,0,0x0210aec8,0,0,0,0,0x0210aeb8]
    if list(records)!=expected:raise ValueError('path state table changed')
    lines += ['struct PathState {State2 init,behavior;const char* name;};',
              'static_assert(sizeof(PathState)==20,"path state ABI");',
              'PathState data_ov002_0210af2c[3]={{nullptr,nullptr,data_ov002_0210aec0},{nullptr,nullptr,data_ov002_0210aec8},{nullptr,nullptr,data_ov002_0210aeb8}};']
    clps=b2[0x0210d7d4-base:0x0210d7f4-base]
    if clps[:8]!=b'CLPS\x08\x00\x01\x00':raise ValueError('CLPS changed')
    lines+=['alignas(4) unsigned char data_ov002_0210d7d4[]={'+','.join(map(str,clps))+'};','}']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
