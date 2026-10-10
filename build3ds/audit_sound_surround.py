#!/usr/bin/env python3
"""Execute original ARM7 surround-volume calculation for native reference."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR
ROOT=Path(__file__).resolve().parent.parent
blob=(ROOT/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
c=Uc(UC_ARCH_ARM,UC_MODE_ARM);c.mem_map(0x2380000,0x30000);c.mem_write(0x2380000,blob)
c.mem_map(0x3800000,0x10000);c.mem_map(0x3000000,0x10000)
h=2166136261;count=0
for amount in [0,0x1000,0x3000,0x8000]:
 c.mem_write(0x38070b4,struct.pack('<I',amount))
 for pan in range(128):
  for volume in [0,1,64,126,127]:
   c.reg_write(UC_ARM_REG_R0,volume);c.reg_write(UC_ARM_REG_R1,pan)
   c.reg_write(UC_ARM_REG_SP,0x300f000);c.reg_write(UC_ARM_REG_LR,0x300ff00)
   c.emu_start(0x2383450,0x300ff00,count=100)
   value=c.reg_read(UC_ARM_REG_R0)&127;h=((h^value)*16777619)&0xffffffff;count+=1
assert struct.unpack_from('<I',blob,0x351c)[0]==0xfff5
p=ROOT/'build3ds/validation/sound-surround';p.mkdir(parents=True,exist_ok=True)
(p/'reference.json').write_text(json.dumps({'arm7_sha256':hashlib.sha256(blob).hexdigest(),'cases':count,'hash':hex(h),'affected_channels':hex(0xfff5)},indent=2)+'\n')
(p/'reference.h').write_text('static const unsigned surround_reference_hash=0x%08xu;\n'%h)
print('PASS: original ARM7 surround reference, %d cases, hash %08x'%(count,h))
