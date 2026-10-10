#!/usr/bin/env python3
"""Verify strong/weak reservations and explicit-mask override in original ARM7."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
ROOT=Path(__file__).resolve().parent.parent
BLOB=(ROOT/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
def run(address,args,strong=0,weak=0):
 c=Uc(UC_ARCH_ARM,UC_MODE_ARM);c.mem_map(0x2380000,0x30000);c.mem_write(0x2380000,BLOB)
 c.mem_map(0x3800000,0x10000);c.mem_map(0x3000000,0x10000)
 def put(a,v):c.mem_write(a,struct.pack('<I',v))
 put(0x38075a4,strong);put(0x38075a0,weak)
 stopped=[]
 def hook(u,pc,size,arg):
  if pc in [0x2383634,0x2383fcc]:
   if pc==0x2383634:stopped.append(u.reg_read(UC_ARM_REG_R0))
   u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 c.hook_add(UC_HOOK_CODE,hook)
 for reg,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):c.reg_write(reg,v)
 c.reg_write(UC_ARM_REG_SP,0x300f000);c.reg_write(UC_ARM_REG_LR,0x300ff00)
 c.emu_start(address,0x300ff00,count=100000)
 assert c.reg_read(UC_ARM_REG_PC)==0x300ff00
 return {'strong':struct.unpack('<I',c.mem_read(0x38075a4,4))[0],'weak':struct.unpack('<I',c.mem_read(0x38075a0,4))[0],'stopped':stopped,'result':c.reg_read(UC_ARM_REG_R0)}
def main():
 results=[]
 for flag in [0,1]:
  r=run(0x2384200,[7,flag],2,4);assert r['stopped']==[0,2]
  assert (r['strong'],r['weak'])==((7,4) if flag==0 else (2,7));results.append(r)
  r=run(0x23841c4,[6,flag],7,7);assert (r['strong'],r['weak'])==((1,7) if flag==0 else (7,1));results.append(r)
 for strong,weak,explicit,ok in [(0,0,0,1),(1,0,0,0),(1,0,1,0),(0,1,0,0),(0,1,1,1),(1,1,1,0)]:
  r=run(0x23843d4,[1,127,explicit,0],strong,weak);assert bool(r['result'])==bool(ok);results.append(r)
 output=ROOT/'build3ds/validation/arm7-sound-reservations';output.mkdir(parents=True,exist_ok=True)
 (output/'reference.json').write_text(json.dumps({'arm7_sha256':hashlib.sha256(BLOB).hexdigest(),'cases':results,'note':'Original lock/unlock/allocation selectors; downstream hardware-stop and channel initialization intercepted.'},indent=2)+'\n')
 print('PASS: ARM7 strong/weak reservations, stop selection, explicit-track-mask override')
if __name__=='__main__':main()
