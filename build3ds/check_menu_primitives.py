#!/usr/bin/env python3
"""Compare native menu primitives against actual ROM ARM instructions."""
import ctypes,json,random,re,struct,subprocess
from pathlib import Path
from ndspy.code import MainCodeFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR
root=Path(__file__).resolve().parent.parent;repo=root/'sm64ds-decomp';out=root/'build3ds/menu-compile'
subprocess.run(['clang++','-std=c++17','-dynamiclib',str(repo.parent/'port/menu/primitives.cpp'),'-o',str(out/'primitives.dylib')],check=True)
native=ctypes.CDLL(str(out/'primitives.dylib'))
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.mem_map(0x01ff8000,0x8000);uc.mem_map(0x02000000,0x400000);uc.mem_map(0x03000000,0x2000)
for section in MainCodeFile((repo/'extracted/arm9_dec.bin').read_bytes(),0x02004000).sections:
    uc.mem_write(section.ramAddress,bytes(section.data))
regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]
def call(address,args):
    for reg,value in zip(regs,args):uc.reg_write(reg,value&0xffffffff)
    uc.reg_write(UC_ARM_REG_SP,0x03000ff0);uc.reg_write(UC_ARM_REG_LR,0x03001000)
    uc.emu_start(address,0x03001000,count=20000)
    return uc.reg_read(UC_ARM_REG_R0)|(uc.reg_read(UC_ARM_REG_R1)<<32)
f=native.func_01ffaa34;f.argtypes=[ctypes.c_int64,ctypes.c_int64];f.restype=ctypes.c_int64
rng=random.Random(64)
cases=[(a,b) for a in [0,1,-1,2**31-1,-2**31,2**63-1,-2**63] for b in [0,1,-1,2,-2,2**31,-2**63]]
cases += [(rng.randrange(-2**63,2**63),rng.randrange(-2**63,2**63)) for _ in range(300)]
for a,b in cases:
    expected=call(0x01ffaa34,[a,a>>32,b,b>>32]);actual=f(a,b)&((1<<64)-1)
    assert actual==expected,(a,b,actual,expected)
symboltext=(repo/'config/arm9/symbols.txt').read_text()
for symbol,count in [('Matrix3x3_LoadIdentity',9),('func_02052ec8',16)]:
    address=int(re.search(r'^'+symbol+r'.*addr:0x([0-9a-f]+)',symboltext,re.M)[1],16)
    buffer=(ctypes.c_int32*(count+4))(*([0x6badbeef]*(count+4)))
    uc.mem_write(0x02300000,bytes(buffer));call(address,[0x02300008])
    getattr(native,symbol)(ctypes.byref(buffer,8))
    assert bytes(buffer)==bytes(uc.mem_read(0x02300000,len(bytes(buffer))))
address=int(re.search(r'^MultiStore32Bytes.*addr:0x([0-9a-f]+)',symboltext,re.M)[1],16)
for length in [0,1,3,4,7,8,28,31,32,33,63,64,65,127]:
    buffer=(ctypes.c_uint32*40)(*([0x6badbeef]*40));uc.mem_write(0x02300000,bytes(buffer))
    call(address,[0x789abcde,0x02300008,length]);native.MultiStore32Bytes(ctypes.c_uint32(0x789abcde),ctypes.byref(buffer,8),length)
    assert bytes(buffer)==bytes(uc.mem_read(0x02300000,len(bytes(buffer)))),length
report={'result':'PASS','division_cases':len(cases),'matrix_cases':2,'fill_cases':14,'reference':'actual supplied ROM ARM instructions via Unicorn'}
(out/'primitives-result.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
