#!/usr/bin/env python3
"""Restore link relocations in the ARM object and compare imported tables to ROM."""
import importlib.util
import re
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parent.parent
REPO=ROOT/'sm64ds-decomp'
spec=importlib.util.spec_from_file_location('tables',REPO.parent/'port/tools/stage_tables.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
addresses={}
for cfg in ['config/arm9/symbols.txt','config/arm9/overlays/ov002/symbols.txt']:
    for line in (REPO/cfg).read_text().splitlines():
        match=re.match(r'(\w+) kind:\S+ addr:0x([0-9a-f]+)',line)
        if match: addresses[match[1]]=int(match[2],16)
blob=(REPO/'extracted/arm9_dec.bin').read_bytes()
expected_edges={**m.POINTERS,**m.RESOURCE_POINTERS}
seen={};count=0
with (ROOT/'build3ds/cmake/CMakeFiles/smoke_player.dir/stage_tables.o').open('rb') as f:
    elf=ELFFile(f);symbols=elf.get_section_by_name('.symtab')
    for start,end in m.SPANS:
        name=f'.data.stage_tables_{start:08x}'
        sec=elf.get_section_by_name(name)
        assert sec and sec['sh_size']==end-start,name
        data=bytearray(sec.data())
        relsec=elf.get_section_by_name('.rel'+name)
        for rel in relsec.iter_relocations() if relsec else []:
            assert rel['r_info_type']==2
            offset=rel['r_offset'];sym=symbols.get_symbol(rel['r_info_sym'])
            if sym.name in addresses: target=addresses[sym.name]
            else:
                target_section=elf.get_section(sym['st_shndx']).name
                assert target_section.startswith('.data.stage_tables_'),target_section
                target=int(target_section.rsplit('_',1)[1],16)+sym['st_value']
            target+=struct.unpack_from('<I',data,offset)[0]
            seen[start+offset]=target
            struct.pack_into('<I',data,offset,target)
        assert data==blob[start-0x02004000:end-0x02004000],name
        count+=len(data)
assert seen==expected_edges,(seen,expected_edges)
report=f'PASS: {count} initialized table bytes and {len(seen)} native relocations match the local ROM. Runtime callback behavior remains a separate check.\n'
(ROOT/'build3ds/validation/stage-tables-verification.txt').write_text(report)
print(report,end='')
