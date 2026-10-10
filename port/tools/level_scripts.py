#!/usr/bin/env python3
"""Import packed camera/level scripts and retain their original bytewise fixups."""
from pathlib import Path
import argparse,re,struct
START,END,BASE=0x0208751c,0x0208a0e0,0x02004000
# The first55 records are {point_count, Elem*}, consumed by func_02008b4c.
# Their spline payloads use eight-byte elements. They precede packed bytecode,
# whose apparent pointer-like words must remain untouched. A separate six-entry
# table contains ordinary aligned script pointers.
PATHS=range(START,0x020876d4,8)
POINTERS=set(range(START+4,0x020876d4,8)) | set(range(0x020876e4,0x020876fc,4))
def generate(root):
    blob=(root/'extracted/arm9_dec.bin').read_bytes();symbols={}
    for line in (root/'config/arm9/symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:data\(\w+\) addr:0x([0-9a-f]+)',line)
        if m and START<=int(m[2],16)<END:symbols[int(m[2],16)]=m[1]
    for address in PATHS:
        count,target=struct.unpack_from('<II',blob,address-BASE)
        if not 4<=count<=255 or target not in symbols or target+count*8>END:
            raise ValueError(f'invalid camera path at {address:08x}')
    lines=['// Local ROM packed scripts; runtime address bytes patched by original code.',
           'asm(R"SCRIPT(','.pushsection .data.level_scripts,"aw",%progbits','.balign 4']
    cursor=START
    while cursor<END:
        if cursor in symbols:lines += [f'.global {symbols[cursor]}',f'{symbols[cursor]}:']
        if cursor in POINTERS:
            target=struct.unpack_from('<I',blob,cursor-BASE)[0]
            if target not in symbols:raise ValueError('unknown script target')
            lines.append('.word '+symbols[target]);cursor+=4
        else:
            stop=min(cursor+16,END)
            for b in range(cursor+1,stop):
                if b in symbols or b in POINTERS:stop=b;break
            lines.append('.byte '+','.join(map(str,blob[cursor-BASE:stop-BASE])));cursor=stop
    lines += ['.popsection',')SCRIPT");','']
    src=(root/'src/__sinit_02073e6c.c').read_text()
    cut=src.index('    *(void **)&data_0209b294[0]')
    src=src[:cut]+'}\n'
    src=src.replace('void __sinit_02073e6c(void)','void sm64ds_init_level_scripts(void)')
    lines += ['extern "C" {',src,'}']
    return '\n'.join(lines)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
