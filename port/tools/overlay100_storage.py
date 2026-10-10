#!/usr/bin/env python3
"""Unify native overlay100 BSS while preserving all original interior addresses."""
import argparse,re
from pathlib import Path

def generate(root):
    start,end=0x02148600,0x02148a80
    cfg=(root/'config/arm9/overlays/ov100/symbols.txt').read_text()
    labels={int(m[2],16):m[1] for m in re.finditer(r'^(\w+) kind:bss addr:0x([0-9a-f]+)',cfg,re.M)}
    labels[0x0214893c]='sm64ds_star_gate_destructor_record'
    lines=['// Unified overlay100 resource and state storage.','asm(R"BSS(','.pushsection .bss.overlay100,"aw",%nobits','.balign 4',
           '.global sm64ds_overlay100_bss_begin','sm64ds_overlay100_bss_begin:']
    cursor=start
    for a,n in sorted(labels.items()):
        if not cursor<=a<end:raise ValueError('BSS symbol out of bounds')
        if a>cursor:lines.append(f'.zero {a-cursor}')
        lines += [f'.global {n}',n+':'];cursor=a
    lines += [f'.zero {end-cursor}', '.global sm64ds_overlay100_bss_end','sm64ds_overlay100_bss_end:', '.popsection',')BSS");']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate(a.root))
