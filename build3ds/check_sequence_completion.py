#!/usr/bin/env python3
"""Run the ROM ARM7 track-completion aggregator with controlled track results."""
from pathlib import Path
import json,hashlib,struct
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
rows=[]
for results in [[],[0],[-1],[0,-1],[-1,0],[-1,-1],[0]*16,[-1]*16]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x2380000,0x30000);u.mem_write(0x2380000,code);u.mem_map(0x2000000,0x20000);u.mem_map(0x100000,0x1000)
 base=0x2001000;released=[]
 for i in range(len(results)):u.mem_write(base+i*64+0x28,struct.pack('<I',0x2005000))
 def hook(u,pc,size,data):
  if pc==0x23859cc:
   i=u.reg_read(UC_ARM_REG_R1);u.reg_write(UC_ARM_REG_R0,base+i*64 if i<len(results) else 0)
  elif pc==0x2384c98:
   i=(u.reg_read(UC_ARM_REG_R0)-base)//64;u.reg_write(UC_ARM_REG_R0,results[i]&0xffffffff)
  elif pc==0x2385968:released.append(u.reg_read(UC_ARM_REG_R1))
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook);u.reg_write(UC_ARM_REG_R0,0x2008000);u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
 u.emu_start(0x2384c1c,0x100000,count=5000)
 assert u.reg_read(UC_ARM_REG_PC)==0x100000
 ended=u.reg_read(UC_ARM_REG_R0)
 assert ended==int(0 not in results)
 assert released==[i for i,r in enumerate(results) if r==-1]
 rows.append(dict(track_results=results,sequence_ended=ended,released_tracks=released))
out=root/'build3ds/validation/sequence-completion-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(arm7_sha256=hashlib.sha256(code).hexdigest(),entry='02384c1c',cases=rows),indent=2)+'\n')
print('PASS ROM completion aggregator:',len(rows),'track combinations; ended tracks released individually')
