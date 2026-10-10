#!/usr/bin/env python3
"""Native overlay100 profiles checked against the local ROM spawn registry."""
import argparse,struct
from pathlib import Path
ROWS=[
 ('BUTTERFLY','daBtfly_c_classInit',0x2147e78,(0x2141ea4,0x1500150,0x10002,0x32000,0x32000,0x1000000,0x320000)),
 ('IRONBALL','daIbl_c_classInit',0x2147f58,(0x214316c,0x4500dc,2,0x64000,0x7d0000,0x1800000,0x1800000)),
 ('WANWAN2','daWanwan2_c_classInit',0x2148030,(0x21442dc,0x1510151,0x10000002,0x190000,0x190000,0x1f40000,0x1f40000)),
 ('DOOR','daDoor_c_classInit',0x2148164,(0x214589c,0x1610161,0x60800002,0x7d000,0x7d000,0x1f40000,0)),
 ('STAR_GATE','daStarGate_c_classInit',0x21483a8,(0x21461d4,0x1620162,2,0x96000,0x96000,0x1f40000,0)),
 ('FISH','daFish_c_classInit',0x2148498,(0x2146d38,0x1580158,0x10002,0,0x1e000,0x1000000,0x4b0000)),
 ('PATH_LIFT','daObjPathLift_c_classInit',0x2148558,(0x2147328,0x12b001f,2,0,0x300000,0x1000000,0x1000000))]
def generate(root):
 b=(root/'extracted/overlays/overlay_0100.bin').read_bytes();arm9=(root/'extracted/arm9_dec.bin').read_bytes()
 lines=['extern "C" {','struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};','static_assert(sizeof(Profile)==28,"profile ABI");']
 for name,factory,a,expected in ROWS:
  v=struct.unpack_from('<7I',b,a-0x2140d80)
  if v!=expected or struct.unpack_from('<I',arm9,0x2090864-0x2004000+(v[1]&65535)*4)[0]!=a:raise ValueError('profile changed '+name)
  if name=='STAR_GATE':continue # The original class TU already defines it.
  lines += [f'void* {factory}();',f'Profile g_profile_{name}={{'+','.join([factory,str(v[1]&65535),str(v[1]>>16)]+[str(x) for x in v[2:]])+'};']
 return '\n'.join(lines+['}'])+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();a.output.write_text(generate(a.root))
