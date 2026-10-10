#!/usr/bin/env python3
"""Execute all restored base-collider callbacks against their retail ARM code."""
import hashlib,json,re,shutil,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
root=Path(__file__).resolve().parent.parent
rom=(root/'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
symbols_text=(root/'sm64ds-decomp/config/arm9/symbols.txt').read_text()
names=['_ZN4dBgW11GetVelocityER7Vector3','_ZN4dBgW14GetAngularVelYEv',
       '_ZN4dBgW12TransformPosERK7Vector3RS0_',
       '_ZN4dBgW10BeforeClsnER5dBgPiP8dActor_cR7Vector3P10Vector3_16S7_',
       '_ZN4dBgW10DetectClsnER12dBgCh_SphCrr','_ZN4dBgW10DetectClsnER9dBgCh_Lin',
       '_ZN4dBgW10DetectClsnER9dBgCh_Gnd','_ZN4dBgW9Virtual08Ev']
path=root/'build3ds/cmake/smoke_player.elf'
with path.open('rb') as stream:
    elf=ELFFile(stream);section=elf.get_section_by_name('.text')
    code=section.data();base=section['sh_addr'];symbols=elf.get_section_by_name('.symtab')
    entries={name:symbols.get_symbol_by_name(name)[0]['st_value'] for name in names}

def execute(blob,origin,entry,name,variant):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    page=entry&~4095;u.mem_map(page,8192)
    u.mem_write(page,blob[page-origin:page-origin+8192])
    actor=0x10000000;callback=actor+0x800;stop=actor+0x1000;stack=actor+0x1f000
    u.mem_map(actor,0x20000);u.mem_write(actor,b'\xa5'*0x1000)
    result=actor+[0x300,0x400,0x4][variant]
    other=actor+0x500 if variant!=1 else 0
    position=actor+0x400;motion=actor+0x600;angle=0 if variant==2 else actor+0x620
    u.mem_write(actor+0x18,struct.pack('<I',callback))
    u.mem_write(callback,struct.pack('<I',0xe12fff1e))
    u.mem_write(stack,struct.pack('<II',motion,angle))
    initial=bytes(u.mem_read(actor,0x1000));calls=[]
    def hook(uc,pc,size,_):
        if pc==callback:
            args=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
            args+=list(struct.unpack('<II',uc.mem_read(uc.reg_read(UC_ARM_REG_SP),8)))
            calls.append(args)
    u.hook_add(UC_HOOK_CODE,hook)
    for reg,value in [(UC_ARM_REG_R0,actor),(UC_ARM_REG_R1,result),(UC_ARM_REG_R2,other),
                      (UC_ARM_REG_R3,position),(UC_ARM_REG_SP,stack),(UC_ARM_REG_LR,stop)]:u.reg_write(reg,value)
    u.emu_start(entry,stop,count=100)
    assert u.reg_read(UC_ARM_REG_PC)==stop
    final=bytes(u.mem_read(actor,0x1000))
    expected=bytearray(initial)
    if 'GetVelocity' in name:expected[result-actor:result-actor+12]=bytes(12)
    assert final==expected,(name,variant,'memory')
    expected_calls=[[actor,other,result,position,motion,angle]] if 'BeforeClsn' in name else []
    assert calls==expected_calls,(name,variant,calls)
    value=u.reg_read(UC_ARM_REG_R0)
    if any(s in name for s in ('DetectClsn','TransformPos','GetAngularVelY')):assert value==0
    return dict(memory_sha256=hashlib.sha256(final).hexdigest(),calls=calls)

records=[]
for name in names:
    match=re.search(r'^'+re.escape(name)+r'.*addr:0x([0-9a-fA-F]+)',symbols_text,re.M)
    address=int(match[1],16)
    for variant in range(3):
        retail=execute(rom,0x02004000,address,name,variant)
        port=execute(code,base,entries[name],name,variant)
        assert retail==port
        records.append(dict(symbol=name,variant=variant,rom_address=hex(address),result=port))
output=root/'build3ds/validation/bg-base-hooks';output.mkdir(parents=True,exist_ok=True)
shutil.copy2(path,output/'smoke_player.elf')
(output/'audit.json').write_text(json.dumps(dict(result='PASS',
    elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),cases=records),indent=2)+'\n')
print('PASS',len(records),'retail/native ARM collider cases: returns, output bounds, callback register/stack order')
