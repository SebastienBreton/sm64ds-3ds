#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes();rows=[]
for pp,tp in [(0,64),(64,64),(10,64),(9,64),(255,255)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code);track=0x2001000;player=0x2002000;channel=0x2003000;sp=0x201f000
 u.mem_write(track+0x12,bytes([tp]));u.mem_write(track+0x1e,b'\xff\xff');u.mem_write(player+4,bytes([pp]));u.mem_write(player+0x18,struct.pack('<HH',120,256));u.mem_write(sp,struct.pack('<I',127));seen=[]
 def hook(u,pc,size,data):
  if pc==0x238d594:u.reg_write(UC_ARM_REG_R0,127)
  elif pc==0x2386618:u.mem_write(u.reg_read(UC_ARM_REG_R3),bytes([2])+bytes(11));u.reg_write(UC_ARM_REG_R0,1)
  elif pc==0x23843d4:seen.append(u.reg_read(UC_ARM_REG_R1));u.emu_stop();return
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 hook_id=u.hook_add(UC_HOOK_CODE,hook)
 for reg,v in [(UC_ARM_REG_R0,track),(UC_ARM_REG_R1,player),(UC_ARM_REG_R2,60),(UC_ARM_REG_R3,100),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2385578,0x100000,count=1000);assert seen==[pp+tp],seen
 u.hook_del(hook_id);u.reg_write(UC_ARM_REG_R0,channel);u.reg_write(UC_ARM_REG_R3,seen[0]);u.reg_write(UC_ARM_REG_SP,sp)
 u.emu_start(0x2383fcc,0x2383fec,count=100);stored=u.mem_read(channel+0x22,1)[0];assert stored==(pp+tp)&255
 # Existing tied channel skips allocation and keeps its original priority.
 u.mem_write(track,bytes([8]));u.mem_write(track+0x3c,struct.pack('<I',channel));u.mem_write(track+0xe,bytes([255])*4)
 u.mem_write(channel+0x22,bytes([84]));u.mem_write(player+4,bytes([255]));u.mem_write(track+0x12,bytes([255]));u.mem_write(sp,struct.pack('<I',127))
 tied_allocations=[]
 def tie_hook(u,pc,size,data):
  if pc==0x238d594:u.reg_write(UC_ARM_REG_R0,127);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif pc==0x23843d4:tied_allocations.append(pc)
 u.hook_add(UC_HOOK_CODE,tie_hook)
 for reg,v in [(UC_ARM_REG_R0,track),(UC_ARM_REG_R1,player),(UC_ARM_REG_R2,68),(UC_ARM_REG_R3,90),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2385578,0x100000,count=1000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 assert not tied_allocations and u.mem_read(channel+0x22,1)[0]==84
 rows.append(dict(player=pp,track=tp,allocation=seen[0],stored=stored,tie_priority_preserved=84))
out=root/'build3ds/validation/sound-priority-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM note allocation priority: full player+track sum, channel byte truncation')
