#!/usr/bin/env python3
"""Run retail ARM7 missing-wave note setup, with no instruction replacements."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
blob=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
u.mem_map(0x2380000,0x30000);u.mem_write(0x2380000,blob)
u.mem_map(0x2000000,0x10000)
bank,wave,instrument,stack,stop=0x2001000,0x2002000,0x2003000,0x200f000,0x200ff00
cases=[]
for kind in ['unbound','sample-out-of-range']:
 for slot in range(4):
  u.mem_write(bank,bytes(128));u.mem_write(wave,bytes(128))
  u.mem_write(bank+0x18+slot*8,struct.pack('<I',0 if kind=='unbound' else wave))
  u.mem_write(wave+0x38,struct.pack('<I',2))
  u.mem_write(instrument,struct.pack('<BBHH6B',1,0,2,slot,60,127,127,127,127,64))
  u.mem_write(stack,struct.pack('<II',bank,instrument))
  for reg,val in [(UC_ARM_REG_R0,0x2004000),(UC_ARM_REG_R1,71),(UC_ARM_REG_R2,127),(UC_ARM_REG_R3,12),(UC_ARM_REG_SP,stack),(UC_ARM_REG_LR,stop)]:u.reg_write(reg,val)
  u.emu_start(0x2386510,stop,count=200)
  result=u.reg_read(UC_ARM_REG_R0);assert result==0,(kind,slot,result)
  cases.append({'kind':kind,'slot':slot,'result':result})
out=root/'build3ds/validation/menu-settings-audio-bindings'
out.mkdir(parents=True,exist_ok=True)
(out/'arm7-note-oracle.json').write_text(json.dumps({'status':'PASS','arm7_sha256':hashlib.sha256(blob).hexdigest(),'function':'02386510','cases':cases},indent=2)+'\n')
print('ARM7 original note rejection PASS',len(cases))
