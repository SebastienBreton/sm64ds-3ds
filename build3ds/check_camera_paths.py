#!/usr/bin/env python3
"""Verify all native spline descriptors and their payloads against the local ROM."""
import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf',type=Path,default=ROOT/'build3ds/cmake/smoke_player.elf')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
repo=ROOT/'sm64ds-decomp'
spec=importlib.util.spec_from_file_location('level_scripts',repo.parent/'port/tools/level_scripts.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
rom=(repo/'extracted/arm9_dec.bin').read_bytes()
records=[]
with args.elf.open('rb') as stream:
    elf=ELFFile(stream)
    symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    def data(name,size):
        symbol=symbols[name]
        section=elf.get_section(symbol['st_shndx'])
        offset=symbol['st_value']-section['sh_addr']
        value=section.data()[offset:offset+size]
        if len(value)!=size:raise ValueError(f'truncated payload: {name}')
        return value
    for address in module.PATHS:
        count,target=struct.unpack_from('<II',rom,address-module.BASE)
        name=f'data_{address:08x}'
        target_name=f'data_{target:08x}'
        native_count,pointer=struct.unpack('<II',data(name,8))
        if native_count!=count or pointer!=symbols[target_name]['st_value']:
            raise ValueError(f'incorrect spline relocation: {name}')
        payload=data(target_name,count*8)
        if payload!=rom[target-module.BASE:target-module.BASE+count*8]:
            raise ValueError(f'changed spline payload: {target_name}')
        records.append(dict(descriptor=hex(address),count=count,source=hex(target),
                            native=hex(pointer),sha256=hashlib.sha256(payload).hexdigest()))
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(dict(passed=True,count=len(records),records=records),indent=2)+'\n')
print(f'PASS: {len(records)} spline relocations and complete ROM payloads')
