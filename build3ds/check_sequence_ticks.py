#!/usr/bin/env python3
"""Check original ARM7 tick accounting; instrument track runner and stop."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
statusptr=struct.unpack_from('<I',code,0x5aac)[0]
rows=[]
for count,tempo,rate,initial,end in [(0,120,256,0,False),(120,120,256,0,False),(240,120,256,0,False),(600,300,384,1,False),(720,120,256,0xfffffffe,True),(65535,65535,65535,100,False)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code);player=0x2001000;status=0x2008000
 u.mem_write(statusptr,struct.pack('<I',status));u.mem_write(player+2,b'\x03')
 u.mem_write(player+0x18,struct.pack('<HHH',tempo,rate,count))
 u.mem_write(status+0x40+3*0x24,struct.pack('<I',initial));calls=[];stops=[]
 def hook(u,pc,size,data):
  if pc==0x2384c1c:
   calls.append(1);u.reg_write(UC_ARM_REG_R0,int(end))
  elif pc==0x238592c:stops.append(1)
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook)
 u.reg_write(UC_ARM_REG_R0,player);u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
 u.emu_start(0x23859f8,0x100000,count=20000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 ticks=struct.unpack('<I',u.mem_read(status+0x40+3*0x24,4))[0]
 accumulator=struct.unpack('<H',u.mem_read(player+0x1c,2))[0]
 due=count//240
 assert ticks==(initial+due)&0xffffffff and accumulator==(count%240+((tempo*rate)>>8))&0xffff
 assert len(calls)==(min(1,due) if end else due) and len(stops)==int(end and due>0)
 rows.append(dict(count=count,tempo=tempo,ratio=rate,initial=initial,end=end,ticks=ticks,accumulator=accumulator,event_passes=len(calls)))
out=root/'build3ds/validation/sseq-tick-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM scheduled ticks, END batching, wrap and tempo accumulator (6 cases)')
