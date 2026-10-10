#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes();rows=[]
for mode in (0,1,2,3):
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
 u.mem_write(0x2380000,code);releases=[]
 for i in (0,4,15):u.mem_write(0x2001000+i*0x40,bytes([0x92]))
 def hook(u,pc,size,data):
  if pc==0x23859cc:
   i=u.reg_read(UC_ARM_REG_R1);u.reg_write(UC_ARM_REG_R0,0x2001000+i*0x40 if i in (0,4,15) else 0)
  elif pc==0x2385ab0:releases.append([u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R2)])
  else:return
  u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,hook)
 for reg,v in [(UC_ARM_REG_R0,3),(UC_ARM_REG_R1,0x8001),(UC_ARM_REG_R2,mode),(UC_ARM_REG_SP,0x201f000),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2386170,0x100000,count=2000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 for i in (0,4,15):assert u.mem_read(0x2001000+i*0x40,1)[0]==(0x92|((mode&1)<<2) if i!=4 else 0x92)
 assert releases==([[0x2001000+i*0x40,127] for i in (0,15)] if mode else [])
 rows.append(dict(mode=mode,muted=bool(mode&1),releases=len(releases),unselected_preserved=True))
note_rows=[]
for muted in (False,True):
 for key,transpose,expected in [(60,0,60),(60,-128,0),(127,127,127)]:
  u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
  for base,size in [(0x2380000,0x30000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
  u.mem_write(0x2380000,code);track,player,events=0x2001000,0x2002000,0x2003000;notes=[]
  u.mem_write(track,bytes([2|(4 if muted else 0)]));u.mem_write(track+0x13,bytes([transpose&255]));u.mem_write(track+0x28,struct.pack('<I',events));u.mem_write(events,bytes([key,100,5,0xff]))
  def note_hook(u,pc,size,data):
   if pc==0x2385ef0:pass
   elif pc==0x2385ea0:u.reg_write(UC_ARM_REG_R0,u.mem_read(u.reg_read(UC_ARM_REG_R0),1)[0])
   elif pc==0x2385578:notes.append(u.reg_read(UC_ARM_REG_R2))
   else:return
   u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  u.hook_add(UC_HOOK_CODE,note_hook)
  for reg,v in [(UC_ARM_REG_R0,track),(UC_ARM_REG_R1,player),(UC_ARM_REG_SP,0x201f000),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
  u.emu_start(0x2384c98,0x100000,count=2000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
  assert notes==([] if muted else [expected])
  assert u.mem_read(track+0x14,1)[0]==expected and struct.unpack('<I',u.mem_read(track+0x20,4))[0]==5
  note_rows.append(dict(muted=muted,key=key,transpose=transpose,portamento_key=expected,allocated=len(notes),wait=5))
out=root/'build3ds/validation/sound-track-mute-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows,note_cases=note_rows),indent=2)+'\n')
print('PASS ROM track mute modes0/1/2/3, selected tracks, release127 and low-bit latch')
