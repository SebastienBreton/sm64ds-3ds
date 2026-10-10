#!/usr/bin/env python3
"""Import retail ov005 menu tables, excluding profile/RTTI/native vtable."""
from pathlib import Path
import argparse,re,struct
BASE=0x020bfec0
SPANS=[(0x020c2250,0x020c2440),(0x020c2464,0x020c2488),(0x020c24d8,0x020c3020)]
def generate(root):
    blobpath=(root/'extracted/overlays/overlay_0005.bin').resolve()
    blob=blobpath.read_bytes()
    assert len(blob)==0x3160 and struct.unpack_from('<I',blob,0x020c2430-BASE)[0]==0
    assert struct.unpack_from('<2I',blob,0x020c2440-BASE)==(0x020c21ec,0x00080005)
    arm=(root/'extracted/arm9_dec.bin').read_bytes()
    assert struct.unpack_from('<I',arm,0x02090864-0x02004000+5*4)[0]==0x020c2440
    cfg=root/'config/arm9/overlays/ov005'
    names={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:.* addr:0x([0-9a-f]+)',(cfg/'symbols.txt').read_text(),re.M)}
    inside=lambda a:any(lo<=a<hi for lo,hi in SPANS)
    relocs={}
    for m in re.finditer(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',(cfg/'relocs.txt').read_text()):
        a,t=int(m[1],16),int(m[3],16)
        if not inside(a):continue
        assert m[2]=='load' and m[4]=='overlay(5)' and inside(t) and t in names
        assert struct.unpack_from('<I',blob,a-BASE)[0]==t
        relocs[a]=names[t]
    lines=['// Generated ov005 tables from the supplied ROM.','asm(R"MG(']
    for i,(lo,hi) in enumerate(SPANS):
        lines += [f'.pushsection .data.minigame_menu_{i},"aw",%progbits','.balign 4',f'.global sm64ds_minigame_data_{i}_begin',f'sm64ds_minigame_data_{i}_begin:']
        cursor=lo
        for a in sorted({lo,hi}|{a for a in names if lo<=a<hi}|{a for a in relocs if lo<=a<hi}):
            assert a>=cursor
            if a>cursor:lines.append(f'.incbin "{blobpath}", {cursor-BASE}, {a-cursor}')
            if a in names and a<hi:lines += [f'.global {names[a]}',names[a]+':']
            if a in relocs:lines.append('.word '+relocs[a]);cursor=a+4
            else:cursor=a
        lines += [f'.global sm64ds_minigame_data_{i}_end',f'sm64ds_minigame_data_{i}_end:','.popsection']
    lines += [')MG");','extern "C" {','void* dScMiniGm_c_classInit();',
              'struct Profile {void*(*create)();unsigned short behavior,render;};',
              'static_assert(sizeof(Profile)==8,"scene profile ABI");',
              'Profile sm64ds_minigame_profile={dScMiniGm_c_classInit,5,8};','}']
    # Permanent ARM9 selection state survives scene unload; overlapping views
    # of the selected 0x34-byte entry share one allocation.
    assert struct.unpack_from('<2i',arm,0x0208a170-0x02004000)==(3,-1)
    lines += ['extern "C" { int data_0208a170=3; int data_0208a174=-1; }',
              'asm(R"COMMON(','.pushsection .bss.minigame_common,"aw",%nobits','.balign 4',
              '.global data_0209b304','data_0209b304:','.zero 4',
              '.global data_0209b308','data_0209b308:','.zero 20',
              '.global data_0209b31c','data_0209b31c:','.zero 32','.popsection',')COMMON");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
