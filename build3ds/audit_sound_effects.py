#!/usr/bin/env python3
"""Derive native stream-effect reference hashes by executing the user's ARM9."""
from pathlib import Path
import hashlib,json,struct
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
from ndspy.codeCompression import decompress
ROOT=Path(__file__).resolve().parent.parent
ROM=(ROOT/'sm64ds-decomp/extracted/arm9.bin').read_bytes()
IMAGE=decompress(ROM)
def fnv(data):
 h=2166136261
 for b in data:h=((h^b)*16777619)&0xffffffff
 return h
def run(mode):
 cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 cpu.mem_map(0x2000000,0x100000);cpu.mem_write(0x2004000,IMAGE)
 cpu.mem_map(0x3000000,0x10000)
 left,right,state,stop=0x3000000,0x3001000,0x3002000,0x300ff00
 def hook(uc,pc,size,arg):
  if pc==0x2058c40:uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 cpu.hook_add(UC_HOOK_CODE,hook)
 out=[]
 for block in range(32):
  count=[2,24,25,512][block%4]
  for side,address in enumerate([left,right]):
   samples=[((i*7919+block*1733+side*19001)&65535)-32768 for i in range(count)]
   cpu.mem_write(address,struct.pack('<'+'h'*count,*samples))
  for reg,value in [(UC_ARM_REG_R0,left),(UC_ARM_REG_R1,right),(UC_ARM_REG_R2,count*2),(UC_ARM_REG_R3,state),(UC_ARM_REG_SP,0x300f000),(UC_ARM_REG_LR,stop)]:cpu.reg_write(reg,value)
  cpu.emu_start([0,0x20499bc,0x20497a0][mode],stop,count=1000000)
  assert cpu.reg_read(UC_ARM_REG_PC)==stop
  out.append(fnv(bytes(cpu.mem_read(left,count*2))+bytes(cpu.mem_read(right,count*2))+bytes(cpu.mem_read(state,0xcc))))
 return out
def main():
 result={'arm9_sha256':hashlib.sha256(ROM).hexdigest(),'mode1':run(1),'mode2':run(2),'note':'Original ARM9 effect routines; only cache flush intercepted; persistent state, saturating inputs,2/24/25/512 sample blocks.'}
 output=ROOT/'build3ds/validation/sound-effects';output.mkdir(parents=True,exist_ok=True)
 (output/'reference.json').write_text(json.dumps(result,indent=2)+'\n')
 (output/'reference.h').write_text('\n'.join('static const unsigned mode%d[32]={%s};'%(m,','.join('0x%08xu'%v for v in result['mode%d'%m])) for m in [1,2])+'\n')
 print('PASS: ARM9 stream effect reference,64 blocks; '+str(output))
if __name__=='__main__':main()
