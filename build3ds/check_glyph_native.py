#!/usr/bin/env python3
"""Differential glyph blit against original ARM instructions, with test VRAM."""
import ctypes, random, re
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
ROOT=Path(__file__).resolve().parent.parent
repo=ROOT/'sm64ds-decomp'
syms={}
for line in (repo/'config/arm9/symbols.txt').read_text().splitlines():
    m=re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)',line)
    if m:syms[m[1]]=int(m[2],16)
emu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
emu.mem_map(0x02000000,0x400000)
emu.mem_write(0x02004000,(repo/'extracted/arm9_dec.bin').read_bytes())
emu.mem_map(0x06000000,0x10000)
END=0x02300000
FONT=0x02310000
lib=ctypes.CDLL(str(ROOT/'build3ds/validation/glyph-native.dylib'))
lib.bind_video.argtypes=[ctypes.c_void_p]
lib.func_020316d8.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_uint,ctypes.c_void_p]
video=(ctypes.c_ubyte*0x10000)();lib.bind_video(video)
rng=random.Random(0x316d8)
fontbytes=rng.randbytes(0x8000);font=ctypes.create_string_buffer(fontbytes)
emu.mem_write(FONT,fontbytes)
getters={syms[n] for n in ['_ZN2G213GetBG2CharPtrEv','_ZN3G2S13GetBG0CharPtrEv','func_02054d88']}
copy=syms['MultiCopy_Int']
def hook(uc,address,size,user):
    if address in getters:
        uc.reg_write(UC_ARM_REG_R0,0x06000000)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    elif address==copy:
        src,dst,n=[uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]
        uc.mem_write(dst,bytes(uc.mem_read(src,n)))
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
emu.hook_add(UC_HOOK_CODE,hook)
def setbytes(name,value):
    data=bytes(value)
    emu.mem_write(syms[name],data)
    ctypes.memmove(ctypes.addressof((ctypes.c_ubyte*len(data)).in_dll(lib,name)),data,len(data))
setbytes('data_0209fcdc',[3]);setbytes('data_0209fc78',[5]);setbytes('data_0209fc88',[96])
count=0
for screen in range(3):
    for width in [1,8,9,16,24]:
        setbytes('data_0208f174',[width]*256)
        for char in [0,17,127,255]:
            for x in range(32):
                initial=rng.randbytes(0x10000)
                emu.mem_write(0x06000000,initial);ctypes.memmove(video,initial,len(initial))
                for reg,value in [(UC_ARM_REG_R0,screen),(UC_ARM_REG_R1,char),(UC_ARM_REG_R2,x),(UC_ARM_REG_R3,FONT),(UC_ARM_REG_SP,0x023f0000),(UC_ARM_REG_LR,END)]:
                    emu.reg_write(reg,value)
                emu.emu_start(syms['func_020316d8'],END,count=10000)
                assert emu.reg_read(UC_ARM_REG_PC)==END,'ROM did not return'
                lib.func_020316d8(screen,char,x,font)
                expected=bytes(emu.mem_read(0x06000000,0x10000));actual=bytes(video)
                assert actual==expected,(screen,width,char,x,next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b))
                count+=1
report=f'PASS: {count} glyph blits match original ROM ARM execution across 3 screen selectors, 5 widths, 4 character tiles and 32 horizontal offsets, including full VRAM surroundings.\n'
(ROOT/'build3ds/validation/glyph-native-verification.txt').write_text(report)
print(report,end='')
