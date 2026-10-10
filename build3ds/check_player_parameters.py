#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import *
root=Path(__file__).resolve().parent.parent;code=(root/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
base=struct.unpack_from('<I',code,0x6008)[0];rows=[]
for offset,value,width in [(4,0x140,1),(4,255,1),(6,0x8000,2),(6,0xffffffa0,2),(6,0,2),(0x1a,128,2),(0x1a,0x1000100,2)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for address,size in [(0x2380000,0x30000),(0x3800000,0x10000),(0x2000000,0x20000),(0x100000,0x1000)]:u.mem_map(address,size)
 u.mem_write(0x2380000,code);player=base+3*0x24;u.mem_write(player,bytes([0xa5])*0x24)
 for reg,v in [(UC_ARM_REG_R0,3),(UC_ARM_REG_R1,offset),(UC_ARM_REG_R2,value),(UC_ARM_REG_R3,width),(UC_ARM_REG_SP,0x201f000),(UC_ARM_REG_LR,0x100000)]:u.reg_write(reg,v)
 u.emu_start(0x2385fc0,0x100000,count=100);assert u.reg_read(UC_ARM_REG_PC)==0x100000
 expected=bytearray([0xa5]*0x24);mask=(1<<(width*8))-1;expected[offset:offset+width]=(value&mask).to_bytes(width,'little')
 assert bytes(u.mem_read(player,0x24))==expected
 rows.append(dict(offset=offset,input=value,width=width,stored=value&mask))
out=root/'build3ds/validation/sound-volume-tempo-rom';out.mkdir(parents=True,exist_ok=True)
(out/'report.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(code).hexdigest(),cases=rows),indent=2)+'\n')
print('PASS ROM command3 field writes: priority/attenuation/tempo offsets and byte-width truncation; other bytes unchanged')
