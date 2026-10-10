#!/usr/bin/env python3
"""Rebind the thirteen mutable archive entries to native name/path pointers."""
from pathlib import Path
import argparse, re, struct
BASE, START, COUNT = 0x02004000, 0x0208ecf4, 13

def generate(root):
    blob=(root/'extracted/arm9_dec.bin').read_bytes()
    reloc={}
    for line in (root/'config/arm9/relocs.txt').read_text().splitlines():
        m=re.match(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',line)
        if m and START<=int(m[1],16)<START+COUNT*20:
            if m[2]!='load' or m[4]!='main': raise ValueError(line)
            reloc[int(m[1],16)]=int(m[3],16)
    strings={}; expected={}; rows=[]
    for i in range(COUNT):
        a=START+i*20
        loaded,heap,first,end,name,path=struct.unpack_from('<IIHHII',blob,a-BASE)
        if loaded or heap or first!=0x8000+i*0x400 or not first<end<=first+0x400:
            raise ValueError('unexpected archive entry')
        for slot,target in [(a+12,name),(a+16,path)]:
            expected[slot]=target
            if not BASE<=target<START: raise ValueError('string outside reviewed ROM area')
            off=target-BASE;stop=blob.index(b'\0',off)
            if stop-off>128: raise ValueError('oversized archive string')
            strings[target]=blob[off:stop+1]
        rows.extend([f'.word 0, 0',f'.hword {first}, {end}',
                     f'.word archive_string_{name:08x}, archive_string_{path:08x}'])
    if expected!=reloc: raise ValueError('archive relocation map differs from ROM')
    lines=['// Generated from local ROM; all archive pointers are native relocations.',
        'asm(R"DATA(','.pushsection .rodata.archive_strings,"a",%progbits']
    for a,s in sorted(strings.items()):
        lines.extend([f'.global archive_string_{a:08x}',f'archive_string_{a:08x}:',
                      '.byte '+','.join(map(str,s))])
    lines+=['.popsection','.pushsection .data.archive_table,"aw",%progbits','.balign 4',
            '.global data_0208ecf4','.type data_0208ecf4, %object','data_0208ecf4:']+rows
    lines+=['.size data_0208ecf4, .-data_0208ecf4','.popsection',')DATA");','']
    return '\n'.join(lines)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
