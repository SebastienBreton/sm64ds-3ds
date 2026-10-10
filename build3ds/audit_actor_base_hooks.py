#!/usr/bin/env python3
"""Compare native leaf callbacks to extracted ARM9 and a live DS RAM snapshot."""
import hashlib
import json
import shutil
from pathlib import Path
from elftools.elf.elffile import ELFFile

root=Path(__file__).resolve().parent.parent
elf_path=root/'build3ds/cmake/smoke_player.elf'
rom_path=root/'sm64ds-decomp/extracted/arm9_dec.bin'
ram_path=root/'build3ds/reference-ds/route-castle/entrance-039.ram'
rom=rom_path.read_bytes()
ram=ram_path.read_bytes()
hooks={
    '_ZN8dActor_c16OnAimedAtWithEggEv': (0x02010124,8),
    '_ZN8dActor_c19OnHitFromUnderneathERS_': (0x0201012c,4),
    '_ZN8dActor_c15OnHitByMegaCharER6Player': (0x02010130,4),
    '_ZN8dActor_c24OnHitByCannonBlastedCharERS_': (0x02010134,4),
    '_ZN8dActor_c8OnPushedERS_': (0x02010138,4),
    '_ZN8dActor_c8OnKickedERS_': (0x0201013c,4),
    '_ZN8dActor_c11OnAttacked2ERS_': (0x02010140,4),
    '_ZN8dActor_c11OnAttacked1ERS_': (0x02010144,4),
    '_ZN8dActor_c15OnGroundPoundedERS_': (0x02010148,4),
    '_ZN8dActor_c9Virtual50Ev': (0x0201014c,8),
}
records=[]
with elf_path.open('rb') as stream:
    elf=ELFFile(stream)
    symbols=elf.get_section_by_name('.symtab')
    for name,(address,size) in hooks.items():
        matches=symbols.get_symbol_by_name(name)
        assert matches and len(matches)==1,name
        symbol=matches[0]
        assert symbol['st_size']==size,(name,symbol['st_size'],size)
        section=elf.get_section(symbol['st_shndx'])
        offset=symbol['st_value']-section['sh_addr']
        native=section.data()[offset:offset+size]
        expected=rom[address-0x02004000:address-0x02004000+size]
        live=ram[address-0x02000000:address-0x02000000+size]
        assert native==expected==live,(name,native.hex(),expected.hex(),live.hex())
        records.append(dict(symbol=name,rom_address=hex(address),native_address=hex(symbol['st_value']),bytes=native.hex()))
output=root/'build3ds/validation/actor-base-hooks'
output.mkdir(parents=True,exist_ok=True)
shutil.copy2(elf_path,output/'smoke_player.elf')
(output/'audit.json').write_text(json.dumps(dict(result='PASS',
    elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest(),
    rom_sha256=hashlib.sha256(rom).hexdigest(),ram_sha256=hashlib.sha256(ram).hexdigest(),
    hooks=records),indent=2)+'\n')
print('PASS',len(records),'base actor hooks: native ARM == decompressed ROM == live DS RAM')
