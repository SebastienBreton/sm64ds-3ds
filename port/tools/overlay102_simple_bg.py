#!/usr/bin/env python3
"""Relocate SimpleBg resources; resolve level-owned CLPS at point of use."""
import argparse,struct
from pathlib import Path
ROWS=[(0x214e710,0x214e738,0x2113ccc),(0x214e6e8,0x214e6f0,0x2113cec),
      (0x214e700,0x214e6e0,0x21136f4),(0x214e718,0x214e6f8,0x210d814),
      (0x214e730,0x214e708,0x2112c18),(0x214e720,0x214e728,0x2112c58)]
def generate(root):
 b=(root/'extracted/overlays/overlay_0102.bin').read_bytes()
 if struct.unpack_from('<18I',b,0x214e188-0x2148a80)!=tuple(v for row in ROWS for v in row):raise ValueError('SimpleBg resource table changed')
 b2=(root/'extracted/overlays/overlay_0002.bin').read_bytes();clps=b2[0x210d814-0x20ad660:0x210d824-0x20ad660]
 if clps[:8]!=b'CLPS\x08\x00\x01\x00':raise ValueError('common CLPS header changed')
 lines=['// ROM-checked six interleaved native resource pairs.','asm(R"TABLE(','.pushsection .data.simple_bg,"aw",%progbits','.balign 4',
        '.global data_ov102_0214e188','data_ov102_0214e188:']
 for i,(model,collision,address) in enumerate(ROWS):
  lines.append(f'.word data_ov102_{model:08x}')
  if i==0:lines += ['.global data_ov102_0214e18c','data_ov102_0214e18c:']
  lines += [f'.word data_ov102_{collision:08x}', '.word 0'] # CLPS is resolved below, never a DS pointer.
 lines += ['.popsection',')TABLE");', '#include <cstdio>','#include <cstdlib>','#include <cstring>',
           'extern "C" int sm64ds_current_level_overlay();',
           'extern "C" void* sm64ds_resolve_level_address(unsigned,unsigned,unsigned);',
           'alignas(4) static unsigned char common_clps[]={'+','.join(map(str,clps))+'};',
           'extern "C" void* sm64ds_simple_bg_clps(unsigned variant) {',
           ' static const unsigned addresses[]={'+','.join(str(r[2]) for r in ROWS)+'};',
           ' if(variant==3)return common_clps;',
           ' const int level=sm64ds_current_level_overlay();',
           ' auto* p=variant<6 && level>=0 ? static_cast<unsigned char*>(sm64ds_resolve_level_address(level,addresses[variant],8)):nullptr;',
           ' if(p && std::memcmp(p,"CLPS",4)==0 && p[4]==8 && p[5]==0) {',
           '  const unsigned bytes=8+8*(unsigned(p[6])+(unsigned(p[7])<<8));',
           '  if(sm64ds_resolve_level_address(level,addresses[variant],bytes))return p;',
           ' }',
           ' std::fprintf(stderr,"FATAL: SimpleBg CLPS variant=%u level=%d unavailable or invalid\\n",variant,level);std::abort();',
           '}']
 return '\n'.join(lines)+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();a.output.write_text(generate(a.root))
