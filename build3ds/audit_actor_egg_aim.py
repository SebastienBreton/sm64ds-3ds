#!/usr/bin/env python3
"""Execute retail/native egg aim callbacks and compare memory + virtual dispatch."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC

root=Path(__file__).resolve().parent.parent
elf_path=root/'build3ds/cmake/smoke_player.elf'
rom=(root/'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
with elf_path.open('rb') as stream:
    elf=ELFFile(stream)
    text=elf.get_section_by_name('.text')
    native=text.data();native_base=text['sh_addr']
    symbols=elf.get_section_by_name('.symtab')
    entry=symbols.get_symbol_by_name('_ZN8dActor_c25OnAimedAtWithEggReturnVecEv')[0]['st_value']
    base_radius=symbols.get_symbol_by_name('_ZN8dActor_c16OnAimedAtWithEggEv')[0]['st_value']
    egg_entry=symbols.get_symbol_by_name('_ZN8dActor_c13OnTurnIntoEggER6Player')[0]['st_value']
    kill_entry=symbols.get_symbol_by_name('_ZN8dActor_c24KillAndTrackInDeathTableEv')[0]['st_value']

def execute(source,base,start,radius_fn,position,radius):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for page in sorted({start&~4095,radius_fn&~4095}):
        u.mem_map(page,4096)
        u.mem_write(page,source[page-base:page-base+4096])
    actor=0x10000000;table=actor+0x200;result=actor+0x400
    override=actor+0x800;stop=actor+0x1000
    u.mem_map(actor,0x20000)
    body=bytearray(b'\xa5'*0x200)
    struct.pack_into('<I',body,0,table)
    struct.pack_into('<III',body,0x5c,*(p&0xffffffff for p in position))
    u.mem_write(actor,bytes(body))
    target=radius_fn if radius is None else override
    u.mem_write(table+0x74,struct.pack('<I',target))
    u.mem_write(result-4,b'\xcc'*20)
    # Override returns a runtime radius; r0 must receive the actual actor.
    u.mem_write(override,struct.pack('<III',0xe59f0000,0xe12fff1e,(radius or 0)&0xffffffff))
    calls=[]
    def hook(uc,address,size,_):
        if address==override:
            assert uc.reg_read(UC_ARM_REG_R0)==actor
            calls.append(address)
    u.hook_add(UC_HOOK_CODE,hook)
    for reg,value in [(UC_ARM_REG_R0,result),(UC_ARM_REG_R1,actor),
                      (UC_ARM_REG_SP,actor+0x1f000),(UC_ARM_REG_LR,stop)]:
        u.reg_write(reg,value)
    u.emu_start(start,stop,count=200)
    assert u.reg_read(UC_ARM_REG_PC)==stop
    assert u.mem_read(actor,len(body))==body
    assert u.mem_read(result-4,4)==b'\xcc'*4 and u.mem_read(result+12,4)==b'\xcc'*4
    assert len(calls)==(radius is not None)
    return bytes(u.mem_read(result,12))

records=[]
for position in [(0,0,0),(123,-456,789),(-4915200,1040384,27344992),
                 (0x7fffffff,0x7fffffff,-0x80000000),(-1,-0x80000000,1)]:
    for radius in [None,0,1,-1,0x14000,0x28000,-0x14000,0x7fffffff]:
        expected=struct.pack('<III',position[0]&0xffffffff,
                             (position[1]+(0x14000 if radius is None else radius))&0xffffffff,
                             position[2]&0xffffffff)
        retail=execute(rom,0x02004000,0x020100dc,0x02010124,position,radius)
        port=execute(native,native_base,entry,base_radius,position,radius)
        assert retail==port==expected,(position,radius,retail.hex(),port.hex())
        records.append(dict(position=position,radius=radius,result=port.hex()))

def transition(source,base,start,target):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for page in sorted({start&~4095,target&~4095}):
        u.mem_map(page,4096)
        u.mem_write(page,source[page-base:page-base+4096])
    reached=[]
    def hook(uc,address,size,_):
        if address==target:
            reached.append((uc.reg_read(UC_ARM_REG_R0),uc.reg_read(UC_ARM_REG_LR)))
            uc.emu_stop()
    u.hook_add(UC_HOOK_CODE,hook)
    u.reg_write(UC_ARM_REG_R0,0x10001234)
    u.reg_write(UC_ARM_REG_R1,0x10004321)
    u.reg_write(UC_ARM_REG_LR,0x10008000)
    u.emu_start(start,0x10008000,count=10)
    assert reached==[(0x10001234,0x10008000)],reached
    return dict(entry=hex(start),target=hex(target),this_preserved=True,tail_call=True)

transitions=[transition(rom,0x02004000,0x02010154,0x0200f9b8),
             transition(native,native_base,egg_entry,kill_entry)]
output=root/'build3ds/validation/actor-egg-aim'
output.mkdir(parents=True,exist_ok=True)
shutil.copy2(elf_path,output/'smoke_player.elf')
(output/'audit.json').write_text(json.dumps(dict(result='PASS',
    elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest(),
    rom_sha256=hashlib.sha256(rom).hexdigest(),cases=records,egg_transitions=transitions),indent=2)+'\n')
print('PASS',len(records),'egg aim ARM comparisons: base/override radius, sret bounds, unchanged actor; both egg-transition tail calls preserve this/LR')
