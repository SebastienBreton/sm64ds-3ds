#!/usr/bin/env python3
"""Verify canonical numeric aliases and resource pointer relocations in ARM ELF.

Checks storage identity/bytes only; a relocatable ELF is not runtime/link closure.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True);p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--overlay',type=int,required=True);p.add_argument('--base',type=lambda s:int(s,0),required=True)
    p.add_argument('--island',type=lambda s:int(s,0),required=True)
    p.add_argument('--start',type=lambda s:int(s,0),required=True)
    p.add_argument('--end',type=lambda s:int(s,0),required=True)
    p.add_argument('--stride',type=int,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--pointer-section');p.add_argument('--pointer-offset',type=lambda s:int(s,0))
    p.add_argument('--pointer-target')
    a=p.parse_args();assert not a.output.exists();rom=a.rom.read_bytes()
    with a.elf.open('rb') as f:
        elf=ELFFile(f);assert elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM'
        table=elf.get_section_by_name('.symtab')
        symbols={s.name:s for s in table.iter_symbols() if s.name}
        island=symbols[f'level_{a.overlay}_{a.island:08x}'];index=island['st_shndx']
        section=elf.get_section(index)
        aliases=[]
        for addr in range(a.start,a.end,a.stride):
            name=f'data_ov{a.overlay:03}_{addr:08x}';symbol=symbols[name]
            assert symbol['st_shndx']==index
            value=symbol['st_value'];assert value-island['st_value']==addr-a.island
            assert section.data()[value:value+a.stride]==rom[addr-a.base:addr-a.base+a.stride]
            aliases.append({'symbol':name,'section':section.name,'offset':value,'rom_address':hex(addr)})
        assert elf.get_section_by_name(f'.data.overlay{a.overlay}_{a.start:x}') is None,'Duplicate actor allocation'
        pointer=None
        if a.pointer_section:
            target_section=elf.get_section_by_name(a.pointer_section);assert target_section
            references=[]
            for sec in elf.iter_sections():
                if not isinstance(sec,RelocationSection) or elf.get_section(sec['sh_info']).name!=a.pointer_section:continue
                symtab=elf.get_section(sec['sh_link'])
                for reloc in sec.iter_relocations():
                    if reloc['r_offset']==a.pointer_offset:
                        references.append((reloc['r_info_type'],symtab.get_symbol(reloc['r_info_sym']).name))
            assert references==[(2,a.pointer_target)],references # R_ARM_ABS32, exact public alias
            assert struct.unpack_from('<I',target_section.data(),a.pointer_offset)[0]==0
            pointer={'section':a.pointer_section,'offset':a.pointer_offset,'target':a.pointer_target,'type':'R_ARM_ABS32'}
    result={'status':'PASS ARM32 canonical level aliases, ROM bytes and resource relocation',
        'elf_sha256':sha(a.elf),'rom_sha256':sha(a.rom),'aliases':aliases,'pointer':pointer,
        'limits':['Relocatable ELF only; constructor/actor/resource lifecycle and runtime remain unproven']}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(result['status'])


if __name__=='__main__':main()
