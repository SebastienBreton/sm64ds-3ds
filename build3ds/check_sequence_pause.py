#!/usr/bin/env python3
"""Execute the supplied ROM's pause command and sequencer scheduler in Unicorn."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
def word(address):return struct.unpack_from('<I',code,address-0x2380000)[0]
players=word(0x238625c);status_ptr=word(0x2386478);rows=[]
for paused in (0,1):
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code)
 def write(a,v):u.mem_write(a,struct.pack('<I',v))
 def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
 id=3;player=players+id*0x24;status=0x2008000
 u.mem_write(player,bytes([1,0,id]));u.mem_write(player+0x1c,struct.pack('<H',177));write(status_ptr,status)
 releases=[];runs=[];updates=[]
 def hook(u,pc,size,data):
  if pc==0x23859cc:
   logical=u.reg_read(UC_ARM_REG_R1);u.reg_write(UC_ARM_REG_R0,0x2001000+logical*0x40 if logical in (0,4,15) else 0)
  elif pc==0x2385ab0:releases.append([u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2)])
  elif pc==0x23859f8:runs.append(u.reg_read(UC_ARM_REG_R0))
  elif pc==0x238579c:updates.append(u.reg_read(UC_ARM_REG_R0))
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook)
 def call(pc,r0,r1=0):
  u.reg_write(UC_ARM_REG_R0,r0);u.reg_write(UC_ARM_REG_R1,r1);u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
  u.emu_start(pc,0x100000,count=10000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 call(0x2386204,id,paused)
 assert u.mem_read(player+1,1)[0]==paused
 assert releases==([[0x2001000+i*0x40,player,127] for i in (0,4,15)] if paused else [])
 call(0x23863f0,1)
 assert runs==([] if paused else [player]) and updates==[player] and read(status+4)==8
 assert struct.unpack('<H',u.mem_read(player+0x1c,2))[0]==177
 rows.append(dict(paused=paused,release_override=[r[2] for r in releases],sequencer_calls=len(runs),channel_parameter_updates=len(updates),active_mask=read(status+4)))
out=root/'build3ds/validation/sseq-pause';out.mkdir(parents=True,exist_ok=True)
(out/'rom-pause.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM pause command: reserved tracks release127; paused scheduler skips sequencer, keeps parameter updates and active mask')
