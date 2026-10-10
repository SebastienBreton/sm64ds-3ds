#!/usr/bin/env python3
"""Compare retained native minigame table bytes and relocations to the ROM."""
from pathlib import Path
import argparse,json,re,struct
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('elf',type=Path);p.add_argument('--work',type=Path,default=Path('build3ds/wanted-closure'))
a=p.parse_args();work=a.work
records=json.loads((work/'data-audit.json').read_text())['symbols']
defined=set((work/'defined.txt').read_text().splitlines())
emitted=set(re.findall(r'^\.global (\w+)$',(work/'storage.cpp').read_text(),re.M))
bases={'main':0x02004000,'1':0x020aa420,'4':0x020ad660,'6':0x020bfec0}
native={(v['module'],v['address']):n for n,v in records.items()
        if n.startswith(('_ZTI','_ZTV','_ZTS')) and n in defined}
with a.elf.open('rb') as f:
    elf=ELFFile(f);symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    count=total=relocs=bss=0
    for name in sorted(emitted):
        if name not in symbols or name not in records:continue
        entry=records[name]
        if (entry['module'],entry['address']) in native:continue
        symbol=symbols[name];section=elf.get_section(symbol['st_shndx'])
        if entry['kind']=='bss':
            assert section['sh_type']=='SHT_NOBITS',name
            bss+=entry['bytes'];continue
        offset=symbol['st_value']-section['sh_addr'];actual=section.data()[offset:offset+entry['bytes']]
        module=entry['module'];address=int(entry['address'],16)
        blob=(Path('sm64ds-decomp/extracted')/('arm9_dec.bin' if module=='main'
              else f'overlays/overlay_{int(module):04}.bin')).read_bytes()
        expected=bytearray(blob[address-bases[module]:address-bases[module]+entry['bytes']])
        for relocation in entry['relocations']:
            target=relocation['target'];dest=records[target]
            alias=native.get((dest['module'],dest['address']));target=alias or target
            assert target in symbols,(name,target)
            pointer=symbols[target]['st_value']+(8 if alias and alias.startswith('_ZTV') else 0)
            struct.pack_into('<I',expected,int(relocation['at'],16)-address,pointer);relocs+=1
        assert actual==expected,name
        count+=1;total+=len(actual)
report={'status':'PASS','objects_checked':count,'bytes':total,'relocations':relocs,
        'bss_bytes':bss,'runtime_validated':False,'elf':str(a.elf)}
(work/'storage-oracle.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
