#!/usr/bin/env python3
"""Unify native overlay102 BSS while preserving all original interior addresses."""
import argparse,re,struct
from pathlib import Path

def generate(root):
    start,end=0x0214e6e0,0x0214eaa0
    cfg=(root/'config/arm9/overlays/ov102/symbols.txt').read_text()
    labels={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:bss addr:0x([0-9a-f]+)',cfg,re.M)}
    lines=['// Unified overlay102 resource and state storage.','asm(R"BSS(','.pushsection .bss.overlay102,"aw",%nobits','.balign 4',
           '.global sm64ds_overlay102_bss_begin','sm64ds_overlay102_bss_begin:']
    cursor=start
    for a,n in sorted(labels.items()):
        if not cursor<=a<end:raise ValueError('BSS symbol out of bounds')
        if a>cursor:lines.append(f'.zero {a-cursor}')
        lines += [f'.global {n}',n+':'];cursor=a
    lines += [f'.zero {end-cursor}', '.global sm64ds_overlay102_bss_end','sm64ds_overlay102_bss_end:', '.popsection',')BSS");']
    blob=(root/'extracted/overlays/overlay_0102.bin').read_bytes()
    if struct.unpack_from('<2I',blob,0x0214d70c-0x02148a80)!=(0x0210da18,0x0210da20):
        raise ValueError('shell model handles changed')
    lines += ['extern "C" {', 'extern unsigned char data_ov002_0210da18[],data_ov002_0210da20[];',
              'void* data_ov102_0214d70c[]={data_ov002_0210da18,data_ov002_0210da20};','}']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
