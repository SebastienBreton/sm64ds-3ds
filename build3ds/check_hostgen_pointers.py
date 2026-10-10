#!/usr/bin/env python3
"""Regression for initialized MMIO pointers and Stage's shadowed p1 local."""
import importlib.util
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parent.parent
REPO=ROOT/'sm64ds-decomp'
spec=importlib.util.spec_from_file_location('hostgen',REPO.parent/'port/tools/hostgen.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
cases=[
 ('volatile u32 *p=(volatile u32*)0x04000000; *p=1; *p |= 2;',2),
 ('volatile u32 *p; p=(volatile u32*)0x04000000; *p=1;',1),
 ('volatile u32 *p=(volatile u32*)0x04000000; p=other; *p=1;',0),
 ('volatile u32 *p=(volatile u32*)0x04000000; p=(volatile u32*)0x04001000; *p=1;',0),
 ('volatile u32 *p=(volatile u32*)0x04000000; p++; *p=1;',0),
 ('volatile u32 *p=(volatile u32*)0x04000000; p+=1; *p=1;',0),
 ('volatile u32 *param=(volatile u32*)0x040002b8; param[0]=3; param[1]=4;',2),
]
for source,expected in cases:
    output,count=module.rewrite_mmio_pointers(source)
    assert count==expected,(source,output,count,expected)
source=(REPO/'src/_ZN5Stage13InitResourcesEv.cpp').read_text()
output,count=module.transform(source)
for name in ['p0','stage_sub_display','pVram']:
    for hit in re.finditer(r'\*\s*'+name+r'\b',output):
        assert re.search(r'(?:u32|u16)\s*$',output[:hit.start()]),name
assert '*p1 = 0;' in output and 'p1 += 1;' in output
# Header macros are expanded after hostgen; verify the generated override
# redirects the actual register assignment rather than leaving a raw DS address.
macro_source=(REPO/'src/func_02055454.c').read_text()
macro_output,macro_count=module.transform(macro_source)
assert '#undef REG_BG0OFS' in macro_output
assert '#define REG_BG0OFS (NTR_MMIO(u32, 0x04000010))' in macro_output
assert 'REG_BG0OFS = v;' in macro_output
affine_source=(REPO/'src/_ZN3G2x12SetBGyAffineEPVtP9Matrix2x2iiii.cpp').read_text()
affine_output,_=module.transform(affine_source)
assert affine_output.count('p = ntr::xlate<unsigned short>(p);')==1
# All fixed-point affine calculations following the translation stay original.
body=affine_source.split('\n{',1)[1]
assert body in affine_output.replace('\n    p = ntr::xlate<unsigned short>(p);','',1)
for ctype,addr in [('volatile u16','0x27fffa8'),('unsigned short','0x027fffa8'),
                   ('volatile unsigned short','0x27ffc40'),('const u32','0x023fffa8')]:
    source=f'*({ctype}*){addr}'
    output,count=module.transform(source)
    assert count==1 and output==f'*ntr::xlate<{ctype}>((void*){addr}u)',output
    assert module.needs_memory_translation(source)
assert not module.needs_memory_translation('flags |= 0x4000000; /* *(u16*)0x4000006 */')
crash=(REPO/'src/func_02013f4c.c').read_text()
translated,_=module.transform(crash)
assert translated.count('ntr::xlate<volatile u16>')==2
assert translated.count('NTR_MMIO(u16, 0x4000006)')==2
assert module.needs_memory_translation('#include "nitro/hw/registers.h"\nREG_BG0OFS=0;')
assert module.transform('*(const s32*)0x40002b4')[0]=='NTR_MMIO(s32, 0x40002b4)'
report=(f'PASS: seven pointer mutation/store fixtures, Stage display accesses translated, '
        f'separate byte-array p1 preserved; {count} Stage rewrites. Header macros, affine destinations, volatile shared RAM mirrors, const register reads and indexed operands checked.\n')
(ROOT/'build3ds/validation/stage-mmio-verification.txt').write_text(report)
print(report,end='')
