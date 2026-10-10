#!/usr/bin/env python3
"""Audit linked private actor numeric islands and typed resident bindings.

This proves static ARM32 bindings against ROM, not original actor execution.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from native_actor_overlay_imports import symbols


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(root, prepared, plan_path, elf_path):
    prep=json.loads((prepared/'preparation.json').read_text())
    manifest_path=prepared/'tools/native_actor_overlay_manifest.json'
    manifest=json.loads(manifest_path.read_text())
    plan=json.loads(plan_path.read_text())
    proof={'numeric_sections':[], 'common_profiles':[], 'resident_slots':[],
           'strong_helpers':[], 'common_initializers':[]}
    with elf_path.open('rb') as stream:
        elf=ELFFile(stream)
        assert elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM'
        syms={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()
              if s['st_shndx']!='SHN_UNDEF'}
        def address(name, function=False):
            s=syms[name]
            assert s['st_info']['bind']=='STB_GLOBAL',name
            if function: assert s['st_info']['type']=='STT_FUNC',name
            return s['st_value']
        def read(at,size):
            for s in elf.iter_sections():
                begin,length=s['sh_addr'],s['sh_size']
                if s['sh_flags']&2 and begin<=at and at+size<=begin+length:
                    return bytes(size) if s['sh_type']=='SHT_NOBITS' else s.data()[at-begin:at-begin+size]
            raise ValueError(f'Unmapped ELF range {at:x}+{size}')
        def words(at,count):return list(struct.unpack('<'+str(count)+'I',read(at,4*count)))
        required_initializers=plan.get('required_common_initializers',[])
        if required_initializers:
            helper='sm64ds_init_common_level_actor_profiles'
            caller=syms[helper]
            caller_address=address(helper,True)
            assert caller_address%4==0 and caller['st_size']%4==0
            branches=[]
            for index,instruction in enumerate(words(caller_address,caller['st_size']//4)):
                if instruction&0x0f000000!=0x0b000000:continue
                displacement=instruction&0xffffff
                if displacement&0x800000:displacement-=0x1000000
                at=caller_address+4*index
                branches.append((at,at+8+4*displacement))
            for name in required_initializers:
                target=address(name,True)
                calls=[at for at,destination in branches if destination==target]
                assert len(calls)==1, f'Required initializer not called exactly once: {name}'
                proof['common_initializers'].append({'symbol':name,'address':hex(target),
                    'caller':helper,'linked_arm_bl':hex(calls[0]),'strong':True})
        for module in manifest['modules']:
            ov=module['overlay']
            if ov not in (2,12,13,15,79):continue
            own=symbols(root,ov)
            blob=(root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
            assert sha(root/f'extracted/overlays/overlay_{ov:04}.bin')==module['rom_sha256']
            for section in module['sections']:
                start=section['start'];count=len(section['words'])
                names=[n for n,(at,kind) in own.items() if at==start]
                assert names, f'Numeric island has no exported identity: {ov}/{start:x}'
                base=address(names[0]);native=words(base,count);expected=[]
                for index,word in enumerate(section['words']):
                    assert words_from_blob(blob,start-module['base']+4*index,1)[0]==word['value']
                    value=address(word['symbol']) if 'symbol' in word else 0 if 'resident_slot' in word else word['value']
                    expected.append(value)
                    if 'resident_slot' in word:
                        assert address(word['resident_slot'])==base+4*index
                assert native==expected, f'Linked island differs: {ov}/{start:x}'
                proof['numeric_sections'].append({'bank':ov,'original_start':hex(start),
                    'native_start':hex(base),'words':count,'exact':True})
            if module.get('bss_bytes'):
                begin=address(f'sm64ds_overlay{ov}_bss_begin')
                end=address(f'sm64ds_overlay{ov}_bss_end')
                assert end-begin==module['bss_bytes']
                for name,(at,kind) in own.items():
                    if kind.startswith('bss'):
                        assert address(name)-begin==at-module['base']-len(blob),name
        original2=(root/'extracted/overlays/overlay_0002.bin').read_bytes()
        base2=next(m['base'] for m in manifest['modules'] if m['overlay']==2)
        common_source=(prepared/'common_scenery_profiles.cpp').read_text()
        registry_names={int(actor):name for actor,name in re.findall(
            r'data_020a4bb8\[(\d+)\]=&(\w+);',common_source)}
        for row in plan['required_common_profiles']:
            retail=words_from_blob(original2,int(row['address'],16)-base2,7)
            assert retail==row['original_profile_words']
            linked_name=registry_names[row['actor_id']]
            native=words(address(linked_name),7)
            assert native[1:]==retail[1:]
            factories=[n for n in row['factory_symbol_candidates'] if n in syms]
            assert any(address(n,True)==native[0] for n in factories)
            proof['common_profiles'].append({'actor':row['actor_id'],'profile':row['symbol'],'linked_profile':linked_name,
                'factory_symbols':factories,'retail_words':retail,'native_words':native})
        start=address('sm64ds_level_address_ranges');end=address('sm64ds_level_address_ranges_end')
        ranges=[words(at,4) for at in range(start,end,16)]
        slots=prep['resident_slots']
        group=address('sm64ds_resident_groups')
        assert words(address('sm64ds_resident_group_count'),1)==[1]
        bank,slot_table,count,scratch,prepared_count=words(group,5)
        assert [bank,count,prepared_count]==[79,len(slots),0]
        assert words(scratch,2*count)==[0]*(2*count)
        native_destinations=[]
        for index,row in enumerate(slots):
            destination,original,size,allowed,allowed_count=words(slot_table+20*index,5)
            assert destination==address(row['slot_label']) and words(destination,1)==[0]
            assert [original,size]==[int(row['target'],16),row['read_extent']]
            assert words(allowed,allowed_count)==row['allowed_providers']==[15]
            matching=[r for r in ranges if r[0]==15 and r[1]<=original and original+size<=r[1]+r[2]]
            assert len(matching)==1
            provider,original_begin,length,native_begin=matching[0]
            resolved=native_begin+original-original_begin
            data=read(resolved,size)
            assert hashlib.sha256(data).hexdigest()==row['sha256']
            assert data[:8]==b'CLPS\x08\x00\x01\x00'
            assert original_begin==int(row['island'],16)
            native_destinations.append(resolved)
            proof['resident_slots'].append({'source':row['from'],'slot':hex(destination),
                'original_target':row['target'],'provider':15,'range':hex(original_begin),
                'resolved_native':hex(resolved),'size':size,'sha256':row['sha256'],
                'initial_pointer_zero':True})
        assert len(set(native_destinations))==len(slots), 'Equal CLPS bytes must not collapse separate numeric addresses'
        for name in ('func_02011d38','func_02048f04','func_02048fd4','func_020228dc',
                     'func_0202293c','func_ov002_020dd8b8','sm64ds_prepare_actor_relocations',
                     'sm64ds_commit_actor_relocations','sm64ds_clear_actor_relocations'):
            proof['strong_helpers'].append({'name':name,'address':hex(address(name,True))})
    return {'status':'PASS_ARM32_ROM_STATIC_BINDINGS_NOT_RUNTIME',**proof,
        'elf_sha256':sha(elf_path),'manifest_sha256':sha(manifest_path),
        'limits':['Resident bank79 requires unique loaded provider15; levels18/39 remain unadmitted.',
                  'Numeric/relocation/BSS/profile bindings only; no constructor/actor behavior execution.',
                  'Raw DS vtables are not imported; generated C++ bodies/vtable-store adapters are separate evidence.']}


def words_from_blob(blob,offset,count):
    return list(struct.unpack_from('<'+str(count)+'I',blob,offset))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('--prepared',type=Path,required=True)
    p.add_argument('--plan',type=Path,required=True);p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Preserve previous proof archives')
    result=audit(a.root.resolve(),a.prepared.resolve(),a.plan.resolve(),a.elf.resolve())
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(result['status'],len(result['numeric_sections']),'sections',len(result['resident_slots']),'resident slots')
