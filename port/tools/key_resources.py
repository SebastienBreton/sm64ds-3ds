#!/usr/bin/env python3
"""Rebind overlay89 key resource tables and initialize their shared file handles."""
from pathlib import Path
import argparse,re,struct
START,END,BASE=0x02132880,0x021328d4,0x02130f00
HANDLES={0x02132c40:0x44f,0x02132c48:0x451,0x02132c50:0x44d,0x02132c58:0x49e,
         0x02132c60:0x44e,0x02132c68:0x49d,0x02132c70:0x450,0x02132c78:0x49c}
def generate(root):
    blob=(root/'extracted/overlays/overlay_0089.bin').read_bytes()
    targets={a:f'data_ov089_{a:08x}' for a in HANDLES}
    targets.update({0x021305d8:'data_ov085_021305d8',0x0211094c:'data_ov002_0211094c',
                    0x0210da40:'data_ov002_0210da40',0x0210d9a0:'data_ov002_0210d9a0',
                    0x0210d9c0:'data_ov002_0210d9c0'})
    relocs={}
    for line in (root/'config/arm9/overlays/ov089/relocs.txt').read_text().splitlines():
        m=re.match(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',line)
        if m and START<=int(m[1],16)<END:
            a,t=int(m[1],16),int(m[3],16)
            module='overlay(89)' if t in HANDLES else ('overlay(85)' if t==0x21305d8 else 'overlay(2)')
            if m[2]!='load' or m[4]!=module or t not in targets:raise ValueError(line)
            if struct.unpack_from('<I',blob,a-BASE)[0]!=t:raise ValueError('ROM pointer mismatch')
            relocs[a]=targets[t]
    if len(relocs)!=15:raise ValueError(f'unexpected relocation count {len(relocs)}')
    lines=['// Generated key resource tables; not the full key actor initializer.',
           'extern "C" {','void func_02017e0c(void*, unsigned);']
    for a in HANDLES:lines.append(f'extern unsigned char data_ov089_{a:08x}[];')
    lines+=['alignas(4) unsigned char data_ov085_021305d8[8] __attribute__((weak));',
            'void sm64ds_init_key_resources() {']
    for a,h in HANDLES.items():lines.append(f'func_02017e0c(data_ov089_{a:08x}, {h});')
    lines+=['func_02017e0c(data_ov085_021305d8, 0x452);','}','}',
            'asm(R"TABLE(','.pushsection .data.key_resources,"aw",%progbits','.balign 4']
    for a in range(START,END,4):
        if a in [START,0x02132894,0x021328b4]:lines += [f'.global data_ov089_{a:08x}',f'data_ov089_{a:08x}:']
        if a in relocs:lines.append('.word '+relocs[a])
        else:lines.append('.word '+str(struct.unpack_from('<I',blob,a-BASE)[0]))
    lines+=['.popsection',')TABLE");','']
    return '\n'.join(lines)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
