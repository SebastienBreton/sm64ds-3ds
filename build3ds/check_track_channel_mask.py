#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes();rows=[]
for selected,channels in [(0x8001,0),(0xffff,0x1000400),(0x10,0xffff),(0x10000,0x4000)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code)
 for i in (0,4,15):u.mem_write(0x2001000+i*0x40,bytes([0x12])*0x40)
 def hook(u,pc,size,data):
  if pc!=0x23859cc:return
  i=u.reg_read(UC_ARM_REG_R1);u.reg_write(UC_ARM_REG_R0,0x2001000+i*0x40 if i in (0,4,15) else 0);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook)
 for reg,v in [(UC_ARM_REG_R0,3),(UC_ARM_REG_R1,selected),(UC_ARM_REG_R2,channels),(UC_ARM_REG_SP,0x201f000),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2386100,0x100000,count=1000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 touched=[]
 for i in (0,4,15):
  expected=bytearray([0x12]*0x40)
  if selected&(1<<i):expected[0]|=0x80;expected[0x1e:0x20]=struct.pack('<H',channels&0xffff);touched.append(i)
  assert bytes(u.mem_read(0x2001000+i*0x40,0x40))==expected
 rows.append(dict(selected=selected,channels=channels,stored=channels&0xffff,touched=touched))
out=root/'build3ds/validation/sound-channel-mask-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM command6: selected channel masks truncated to16 bits, explicit flag set, other bytes preserved')
