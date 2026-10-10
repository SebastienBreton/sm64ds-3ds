#!/usr/bin/env python3
"""Native castle profiles checked against the local ROM spawn registry."""
import argparse,struct
from pathlib import Path
ROWS=[('SBIRD', 'daSBird_c_classInit', 34683188, (34675248, 22479191, 65538, 0, 163840, 16777216, 3276800)), ('MC_WATER', 'daObjMcWater_c_classInit', 34683380, (34676108, 22151506, 0, 0, 3276800, 32768000, 327680)), ('MC_METALNET', 'daObjMc_Metalnet_c_classInit', 34683580, (34676808, 22217043, 2, 0, 5242880, 16777216, 0)), ('MC_FLAG', 'daMcFlag_c_classInit', 34683772, (34677232, 22413654, 536870915, 819200, 819200, 50331648, 0))]
def generate(root):
 b=(root/'extracted/overlays/overlay_0009.bin').read_bytes();arm9=(root/'extracted/arm9_dec.bin').read_bytes()
 lines=['extern "C" {','struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};','static_assert(sizeof(Profile)==28,"profile ABI");']
 for name,factory,a,expected in ROWS:
  v=struct.unpack_from('<7I',b,a-0x21111a0)
  if v!=expected or struct.unpack_from('<I',arm9,0x2090864-0x2004000+(v[1]&65535)*4)[0]!=a:raise ValueError('profile changed '+name)
  lines += [f'void* {factory}();',f'Profile g_profile_{name}={{'+','.join([factory,str(v[1]&65535),str(v[1]>>16)]+[str(x) for x in v[2:]])+'};']
 return '\n'.join(lines+['}'])+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();a.output.write_text(generate(a.root))
