#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes();rows=[]
for offset,width,value,mask in [(9,1,255,0xffff),(0xa,2,0xffffffa0,9),(0xc,2,128,0x8000),(0x19,1,286,0x8009),(0x1a,1,7,0x8009),(9,1,127,0),(9,1,127,0x10000)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code);sp=0x201f000;u.mem_write(sp,struct.pack('<I',width))
 for i in (0,3,15):u.mem_write(0x2001000+i*0x40,bytes([0xa5])*0x40)
 def hook(u,pc,size,data):
  if pc!=0x23859cc:return
  i=u.reg_read(UC_ARM_REG_R1);u.reg_write(UC_ARM_REG_R0,0x2001000+i*0x40 if i in (0,3,15) else 0);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook)
 for reg,v in [(UC_ARM_REG_R0,3),(UC_ARM_REG_R1,mask),(UC_ARM_REG_R2,offset),(UC_ARM_REG_R3,value),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2385f2c,0x100000,count=2000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 touched=[]
 for i in (0,3,15):
  expected=bytearray([0xa5]*0x40)
  if mask&(1<<i):expected[offset:offset+width]=(value&((1<<(8*width))-1)).to_bytes(width,'little');touched.append(i)
  assert bytes(u.mem_read(0x2001000+i*0x40,0x40))==expected
 rows.append(dict(offset=offset,width=width,value=value,mask=mask,touched=touched))
out=root/'build3ds/validation/sound-track-parameters-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM command4: five fields, byte/halfword truncation, sparse/missing/high/empty track masks')
