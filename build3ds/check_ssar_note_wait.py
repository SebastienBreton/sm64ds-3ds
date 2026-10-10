#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x2380000,0x30000);u.mem_write(0x2380000,code)
u.mem_map(0x2000000,0x20000);u.mem_map(0x100000,0x1000)
t,p,e,ch=0x2001000,0x2002000,0x2003000,0x2004000
write=lambda a,x:u.mem_write(a,struct.pack('<I',x))
read=lambda a:struct.unpack('<I',u.mem_read(a,4))[0]
notes=[]
def hook(u,pc,size,data):
 if pc==0x2385ef0: u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 elif pc==0x2385ea0:
  u.reg_write(UC_ARM_REG_R0,u.mem_read(u.reg_read(UC_ARM_REG_R0),1)[0]);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 elif pc==0x2385578:
  notes.append(read(u.reg_read(UC_ARM_REG_SP)));write(t+0x3c,ch);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
def run():
 u.reg_write(UC_ARM_REG_R0,t);u.reg_write(UC_ARM_REG_R1,p);u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
 u.emu_start(0x2384c98,0x100000,count=10000)
 assert u.reg_read(UC_ARM_REG_PC)==0x100000
 return dict(pos=read(t+0x28)-e,wait=read(t+0x20),wait_for_notes=bool(u.mem_read(t,1)[0]&16),notes=len(notes))
rows=[]
for length in [0,1,7]:
 u.mem_write(t,bytes(0x80));u.mem_write(t,bytes([2]));u.mem_write(e,bytes([60,127,length,0x80,7,0xff])+bytes(16));write(t+0x28,e);notes.clear()
 first=run();held=run();write(t+0x3c,0);released=run()
 if length==0:assert first==held==dict(pos=3,wait=0,wait_for_notes=True,notes=1) and released==dict(pos=5,wait=7,wait_for_notes=False,notes=1)
 rows.append(dict(length=length,first=first,held=held,released=released))
out=root/'build3ds/validation/ssar-zero-note';out.mkdir(parents=True,exist_ok=True)
(out/'rom-reference.json').write_text(json.dumps(dict(arm7_sha256=hashlib.sha256(code).hexdigest(),entry='02384c98',cases=rows),indent=2)+'\n')
print(json.dumps(rows));print('PASS ROM zero-duration note waits for channel list to empty')
