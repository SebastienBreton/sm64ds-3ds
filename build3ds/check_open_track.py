#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes();rows=[]
for number,exists in [(1,True),(1,False),(0,True),(15,True),(16,False)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x2380000,0x30000);u.mem_write(0x2380000,code);u.mem_map(0x2000000,0x20000);u.mem_map(0x100000,0x1000)
 t,target,events=0x2001000,0x2001100,0x2003000
 def write(a,v):u.mem_write(a,struct.pack('<I',v))
 def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
 u.mem_write(t,bytes([2]));write(t+0x24,events);write(t+0x28,events)
 write(target+0x20,7);write(target+0x28,events+100);u.mem_write(target+4,bytes([90]))
 u.mem_write(events,bytes([0x93,number,32,0,0,0x80,1,0xff])+bytes(128));releases=[]
 def hook(u,pc,size,data):
  if pc==0x2385ef0:pass
  elif pc==0x2385ea0:u.reg_write(UC_ARM_REG_R0,u.mem_read(u.reg_read(UC_ARM_REG_R0),1)[0])
  elif pc==0x23859cc:u.reg_write(UC_ARM_REG_R0,(t if number==0 else target) if exists else 0)
  elif pc==0x23859bc:releases.append(u.reg_read(UC_ARM_REG_R0))
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook);u.reg_write(UC_ARM_REG_R0,t);u.reg_write(UC_ARM_REG_R1,0x2008000);u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
 u.emu_start(0x2384c98,0x100000,count=5000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 changed=exists and number!=0
 assert read(target+0x28)==events+(32 if changed else 100)
 assert read(target+0x20)==7 and u.mem_read(target+4,1)[0]==90
 assert releases==([target] if changed else [])
 rows.append(dict(number=number,reserved=exists,restarted=changed,wait_preserved=7,volume_preserved=90))
out=root/'build3ds/validation/sseq-track-slots';out.mkdir(parents=True,exist_ok=True)
(out/'rom-open-track.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS five ROM open-track cases: existing/self/missing/high slots; parameters preserved')
