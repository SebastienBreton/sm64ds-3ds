#!/usr/bin/env python3
"""Check emitted ARM object bytes and each relocation against local level ROMs."""
import importlib.util
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parent.parent
REPO=ROOT/'sm64ds-decomp'
spec=importlib.util.spec_from_file_location('levels',REPO.parent/'port/tools/all_level_data.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
levels=module.collect(REPO)
path=ROOT/'build3ds/cmake/CMakeFiles/smoke_player.dir/all_level_data.o'
bytes_checked=relocs_checked=0
with path.open('rb') as f:
    elf=ELFFile(f)
    symbols=elf.get_section_by_name('.symtab')
    for ov,root,blob,spans,edges in levels:
        if blob is None: continue
        def covered(address,size):
            assert any(a<=address and address+size<=b for a,b in spans), (ov,hex(address),size)
        covered(root,28)
        clps,objects=struct.unpack_from('<II',blob,root-module.BASE)
        covered(clps,8)
        magic,stride,count=struct.unpack_from('<4sHH',blob,clps-module.BASE)
        assert magic==b'CLPS' and stride==8,(ov,magic,stride)
        covered(clps,8+stride*count)
        areas=struct.unpack_from('<I',blob,root+16-module.BASE)[0]
        area_count=blob[root+20-module.BASE]
        covered(areas,12*area_count)
        tables=[objects]+[struct.unpack_from('<I',blob,areas+12*i-module.BASE)[0] for i in range(area_count)]
        for table in tables:
            if table==0:continue
            covered(table,8)
            n=struct.unpack_from('<H',blob,table-module.BASE)[0]
            entries=struct.unpack_from('<I',blob,table+4-module.BASE)[0]
            if n: covered(entries,8*n)
            # Sizes used directly by the original category loaders. Other
            # categories publish pointers for later consumers; check presence
            # here without claiming their complete payload lengths.
            strides={0:16,1:16,5:8,6:8,9:12,10:14}
            for i in range(n):
                entry=entries+8*i
                category,number=struct.unpack_from('<BB',blob,entry-module.BASE)
                category &= 31
                assert category<15,(ov,category)
                pointer=struct.unpack_from('<I',blob,entry+4-module.BASE)[0]
                if category in strides and number:
                    covered(pointer,strides[category]*number)
                elif category==14:
                    covered(pointer,4)
                elif pointer:
                    covered(pointer,1)
        for start,end in spans:
            name=f'.data.level_{ov}_{start:08x}'
            sec=elf.get_section_by_name(name)
            assert sec is not None and sec['sh_size']==end-start,name
            data=bytearray(sec.data())
            expected=blob[start-module.BASE:end-module.BASE]
            relsec=elf.get_section_by_name('.rel'+name)
            actual_edges={}
            for rel in relsec.iter_relocations() if relsec else []:
                assert rel['r_info_type']==2,'expected ARM ABS32'
                offset=rel['r_offset']
                sym=symbols.get_symbol(rel['r_info_sym'])
                target_section=elf.get_section(sym['st_shndx']).name
                prefix=f'.data.level_{ov}_'
                assert target_section.startswith(prefix),(name,target_section)
                base=int(target_section[len(prefix):],16)
                target=base+sym['st_value']+struct.unpack_from('<I',data,offset)[0]
                actual_edges[start+offset]=target
                struct.pack_into('<I',data,offset,target)
            assert actual_edges=={a:t for a,t in edges.items() if start<=a<end},name
            assert data==expected,name
            bytes_checked+=len(data);relocs_checked+=len(actual_edges)
    sec=elf.get_section_by_name('.data.level_roots')
    assert sec['sh_size']==52*4
    root_rels=list(elf.get_section_by_name('.rel.data.level_roots').iter_relocations())
    assert len(root_rels)==52
    for index,rel in enumerate(root_rels):
        assert rel['r_offset']==index*4 and rel['r_info_type']==2
        ov,root,blob,spans,edges=levels[index]
        sym=symbols.get_symbol(rel['r_info_sym'])
        addend=struct.unpack_from('<I',sec.data(),index*4)[0]
        if blob is None:
            assert sym.name=='data_ov009_02112bdc' and addend==0
        else:
            name=elf.get_section(sym['st_shndx']).name
            prefix=f'.data.level_{ov}_'
            assert name.startswith(prefix)
            assert int(name[len(prefix):],16)+sym['st_value']+addend==root
report=(f'PASS: {bytes_checked} level-data bytes and {relocs_checked} internal pointers '
        'match their ROM overlays; all52 root relocations select the correct overlay. '
        'CLPS, area arrays, category tables and directly spawned object payloads fit retained islands. '
        'Castle root reuses its existing import. Actor initialization, deferred category payload '
        'extents, texture runtime mutation and level transitions are not verified.\n')
(ROOT/'build3ds/validation/all-level-data-verification.txt').write_text(report)
print(report,end='')
