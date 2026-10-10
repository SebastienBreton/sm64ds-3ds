#!/usr/bin/env python3
"""Bind camera state/script PMFs from the local ROM to native ARM methods."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    init=(root/"src/__sinit_02073a24.c").read_text()
    dispatch=(root/"src/func_02008550.cpp").read_text()
    names=sorted(set(re.findall(r"data_02086[0-9a-f]{3}",init+dispatch)))
    symbols=(root/"config/arm9/symbols.txt").read_text()
    functions={int(a,16):n for n,a in re.findall(r"(\w+) kind:function.*addr:0x([0-9a-f]+)",symbols)}
    rom=(root/"extracted/arm9_dec.bin").read_bytes()
    entries=[]
    for name in names:
        target,adjust=struct.unpack_from("<II",rom,int(name[-8:],16)-0x02004000)
        if adjust or target not in functions: raise ValueError(f"unsupported PMF {name}")
        entries.append((name,functions[target]))
    if len(entries)!=77: raise ValueError(f"unexpected callback count {len(entries)}")
    targets=sorted(set(t for _,t in entries))
    lines=['// Generated camera state and script PMFs; no DS code pointers.', 'struct NativeCameraCallbacks {']
    for i,t in enumerate(targets): lines.append(f'int call_{i}(unsigned char*, int, int) asm("{t}");')
    lines+=['};','using Callback = int (NativeCameraCallbacks::*)(unsigned char*,int,int);',
            'static_assert(sizeof(Callback)==8,"camera PMF ABI");','extern "C" {']
    for n,t in entries:lines.append(f'Callback {n} = &NativeCameraCallbacks::call_{targets.index(t)};')
    lines+=['}', 'asm(R"BSS(', '.pushsection .bss.camera_callbacks,"aw",%nobits','.balign 4']
    start,end=0x0209b004,0x0209b270
    cursor=start
    for n,a in re.findall(r"(\w+) kind:bss addr:0x([0-9a-f]+)",symbols):
        addr=int(a,16)
        if not start<=addr<end:continue
        if addr>cursor:lines.append(f'.zero {addr-cursor}')
        lines += [f'.global {n}',f'.type {n}, %object',f'{n}:']
        cursor=addr
    lines += [f'.zero {end-cursor}', '.popsection', ')BSS");','']
    return "\n".join(lines)
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("root",type=Path);p.add_argument("output",type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
