#!/usr/bin/env python3
"""Native overlay102 profiles checked against the local ROM spawn registry."""
import argparse,struct
from pathlib import Path
ROWS=[('KI_IWA', 'daObjSimpleBg_c_classInit_KI_IWA', 34922720, (34901900, 12124221, 2, 0, 5242880, 33554432, 0)), ('KI_HASIRA_DAI', 'daObjSimpleBg_c_classInit_KI_HASIRA_DAI', 34922748, (34901852, 11993147, 2, 0, 2097152, 33554432, 0)), ('BK_TOWER', 'daObjSimpleBg_c_classInit_BK_TOWER', 34922776, (34901804, 13238321, 0, 3145728, 8388608, 16777216, 0)), ('DOKAN', 'daObjSimpleBg_c_classInit_DOKAN', 34922804, (34901756, 12255530, 2, 0, 2097152, 16777216, 0)), ('MC_DODAI', 'daObjSimpleBg_c_classInit_MC_DODAI', 34922832, (34901708, 22282580, 0, 393216, 3145728, 16777216, 0)), ('MC_HAZAD', 'daObjSimpleBg_c_classInit_MC_HAZAD', 34922860, (34901660, 22348117, 0, 393216, 3145728, 16777216, 0)), ('VS_ITEM_BLOCK', 'daObjHatenaBlock_c_classInit_VS_ITEM_BLOCK', 34923468, (34908460, 2621462, 2, 393216, 1048576, 16777216, 16777216)), ('HATENA_BLOCK', 'daObjHatenaBlock_c_classInit_HATENA_BLOCK', 34923496, (34908396, 2490388, 2, 393216, 1048576, 16777216, 16777216)), ('ITEM_BLOCK', 'daObjHatenaBlock_c_classInit_ITEM_BLOCK', 34923524, (34908332, 2555925, 2, 393216, 1048576, 16777216, 16777216)), ('CAP_BLOCK_M', 'daObjHatenaBlock_c_classInit_CAP_BLOCK_M', 34923552, (34908268, 2686999, 2, 393216, 1048576, 16777216, 16777216)), ('CAP_BLOCK_L', 'daObjHatenaBlock_c_classInit_CAP_BLOCK_L', 34923580, (34908204, 2818073, 2, 393216, 1048576, 16777216, 16777216)), ('CAP_BLOCK_W', 'daObjHatenaBlock_c_classInit_CAP_BLOCK_W', 34923608, (34908140, 2752536, 2, 393216, 1048576, 16777216, 16777216)), ('BOMBHEI', 'daBmb_c_classInit', 34923828, (34916088, 2949326, 268468355, 204800, 286720, 16777216, 16777216)), ('SHELL', 'daShl_c_classInit', 34924076, (34920116, 9896221, 2, 409600, 409600, 16777216, 6553600))]
def generate(root):
 b=(root/'extracted/overlays/overlay_0102.bin').read_bytes();arm9=(root/'extracted/arm9_dec.bin').read_bytes()
 lines=['extern "C" {','struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};','static_assert(sizeof(Profile)==28,"profile ABI");']
 for name,factory,a,expected in ROWS:
  v=struct.unpack_from('<7I',b,a-0x2148a80)
  if v!=expected or struct.unpack_from('<I',arm9,0x2090864-0x2004000+(v[1]&65535)*4)[0]!=a:raise ValueError('profile changed '+name)
  if name=='BOMBHEI':continue # The original class TU already defines it.
  lines += [f'void* {factory}();',f'Profile g_profile_{name}={{'+','.join([factory,str(v[1]&65535),str(v[1]>>16)]+[str(x) for x in v[2:]])+'};']
 return '\n'.join(lines+['}'])+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();a.output.write_text(generate(a.root))
