#!/usr/bin/env python3
"""Execute ROM variable resolution, player setup and stop on controlled status."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent
code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
for base,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(base,size)
u.mem_write(0x2380000,code)
def write(a,v):u.mem_write(a,struct.pack('<I',v))
def word(a):return struct.unpack_from('<I',code,a-0x2380000)[0]
def call(pc,*args):
 for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args):u.reg_write(reg,v)
 u.reg_write(UC_ARM_REG_SP,0x201f000);u.reg_write(UC_ARM_REG_LR,0x100000)
 u.emu_start(pc,0x100000,count=10000);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 return u.reg_read(UC_ARM_REG_R0)
status=0x2008000;player=0x2001000
write(word(0x2384c18),status);u.mem_write(player+2,bytes([3]))
u.mem_write(status,bytes([0x55])*0x280)
rows=[]
for index in (0,15,16,31):
 address=call(0x2384bd8,player,index)
 expected=status+0x20+3*0x24+index*2 if index<16 else status+0x260+(index-16)*2
 assert address==expected
 rows.append(dict(index=index,status_offset=address-status))
call(0x2386814,3,0,0x1234ffff);call(0x23867f8,15,0x1002a)
assert bytes(u.mem_read(status+0x20+3*0x24,2))==b'\xff\xff'
assert bytes(u.mem_read(status+0x27e,2))==b'\x2a\x00'
before=bytes(u.mem_read(status,0x280));call(0x2385b24,player,0x2002000)
expected=bytearray(before);expected[0x20+3*0x24:0x40+3*0x24]=b'\xff'*32;expected[0x40+3*0x24:0x44+3*0x24]=b'\x00'*4
assert bytes(u.mem_read(status,0x280))==expected
# Stop's track-release helper is instrumented; execute the real player stop.
released=[]
def hook(u,pc,size,data):
 if pc==0x2385968:
  released.append(u.reg_read(UC_ARM_REG_R1));u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
u.mem_write(player,b'\x01');before=bytes(u.mem_read(player,0x24))
call(0x238592c,player)
assert bytes(u.mem_read(player,0x24))==b'\x00'+before[1:]
assert bytes(u.mem_read(status,0x280))==expected and released==list(range(16))
out=root/'build3ds/validation/sseq-variable-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),resolver=rows,command_halfword_truncation=True,setup_resets_only_selected_local_variables_and_tick=True,stop_preserves_variables_and_parameters=True),indent=2)+'\n')
print('PASS ROM variable scopes, halfword writes, local-only setup reset and stop state retention')
