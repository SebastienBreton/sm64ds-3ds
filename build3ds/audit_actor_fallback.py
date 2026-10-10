#!/usr/bin/env python3
"""Prove late-weakened fallback bodies do not get embedded in actor teardown."""
from pathlib import Path
import hashlib,json,shutil
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
root=Path(__file__).resolve().parent.parent
names=['_ZN10dBgW_KcMbgD1Ev','_ZN5ModelD1Ev','_ZN8dActor_cD1Ev']

def trace(path):
    with path.open('rb') as stream:
        elf=ELFFile(stream);section=elf.get_section_by_name('.text')
        code=section.data();base=section['sh_addr'];symbols=elf.get_section_by_name('.symtab')
        address=lambda name:symbols.get_symbol_by_name(name)[0]['st_value']
        entry=address('_ZN10dBgActor_cD1Ev')
        targets={address(name):name for name in names}
        traps=symbols.get_symbol_by_name('_ZL10actor_trapPKc')
        trap=traps[0]['st_value'] if traps else None
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    low=base&~4095;high=(base+len(code)+4095)&~4095
    u.mem_map(low,high-low);u.mem_write(base,code)
    actor=0x10000000;stop=actor+0x1000
    u.mem_map(actor,0x20000)
    events=[]
    def hook(uc,pc,size,_):
        if pc==trap:
            events.append({'trap':True});uc.emu_stop()
        elif pc in targets:
            events.append({'callee':targets[pc],'offset':uc.reg_read(UC_ARM_REG_R0)-actor})
            # Only test dispatch/order here, not the callee implementation.
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    u.hook_add(UC_HOOK_CODE,hook)
    u.reg_write(UC_ARM_REG_R0,actor)
    u.reg_write(UC_ARM_REG_SP,actor+0x1f000)
    u.reg_write(UC_ARM_REG_LR,stop)
    u.emu_start(entry,stop,count=100)
    return dict(events=events,returned=u.reg_read(UC_ARM_REG_PC)==stop,
                elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest())

before=trace(root/'build3ds/actor-before-noipa.elf')
current=root/'build3ds/cmake/smoke_player.elf'
after=trace(current)
assert before['events']==[{'trap':True}] and not before['returned'],before
assert after['events']==[{'callee':n,'offset':o} for n,o in zip(names,[0x124,0xd4,0])],after
assert after['returned'],after
output=root/'build3ds/validation/actor-fallback'
output.mkdir(parents=True,exist_ok=True)
shutil.copy2(current,output/'after.elf')
shutil.copy2(root/'build3ds/actor-before-noipa.elf',output/'before.elf')
(output/'audit.json').write_text(json.dumps(dict(result='PASS',before=before,after=after),indent=2)+'\n')
print('PASS before embeds abort; after dispatches collider/model/actor destructors at correct offsets and returns')
