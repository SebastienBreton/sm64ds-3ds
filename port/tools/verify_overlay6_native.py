#!/usr/bin/env python3
"""Verify the linked ARM32 overlay6 storage/native RTTI and vtable contract.

Static ABI evidence only. Actor admission, dynamic RTTI behavior of unadmitted
classes and gameplay/save gates remain separate requirements.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from initialized_data_owner import elf
from retail_rtti_bridge import generate as metadata_proof


def verify(root, binary, audit, policy, bindings):
    sections,records,_=elf(binary)
    symbols={r['name']:r['offset'] for r in records if r['section'] != 0}
    def read(address,size):
        for section in sections:
            start=section['address']
            if section['type']!=8 and start<=address and address+size<=start+section['size']:
                return section['bytes'][address-start:address-start+size]
        raise ValueError('No physical linked interval: '+hex(address))
    def words(address,count):return list(struct.unpack('<'+'I'*count,read(address,count*4)))
    aliases=[]
    for row in policy['vtable_aliases']:
        actual=symbols[row['alias']];expected=symbols[row['target']]+row['offset']
        if row['offset']!=8 or actual!=expected:raise ValueError('Native address-point alias changed: '+row['alias'])
        aliases.append(row|{'actual':actual})
    native=[]
    for name in sorted(n for n in audit['symbols'] if n.startswith('_ZTV') and n in bindings and not n.startswith('_ZTVN3abi')):
        type_name='_ZTI'+name[4:];actual=words(symbols[name],2)
        if actual!=[0,symbols[type_name]]:raise ValueError('Primary vtable prefix/RTTI mismatch: '+name)
        native.append({'symbol':name,'address':symbols[name],'RTTI':type_name,'prefix':actual})
    _,proof=metadata_proof(root,audit,policy['metadata_types']);metadata=[];tails=[]
    for row in proof['records']:
        name=row['symbol'];ts='_ZTS'+name[4:]
        expected=[symbols[row['native_vptr'][:-2]]+8,symbols[ts]]
        if row['original']['bytes']==12:expected.append(symbols[row['original']['relocations'][2]['target']])
        actual=words(symbols[name],len(expected))
        if actual!=expected:raise ValueError('Retail RTTI native layout mismatch: '+name)
        literal=row['literal'].encode()+b'\0'
        if read(symbols[ts],len(literal))!=literal:raise ValueError('RTTI literal mismatch: '+name)
        metadata.append({'symbol':name,'words':actual})
        record=audit['symbols'][ts];start=int(record['address'],16)
        for reloc in record['relocations']:
            offset=int(reloc['at'],16)-start;value=words(symbols[ts]+offset,1)[0]
            if value!=symbols[reloc['target']]:raise ValueError('Name interval trailing prefix lost: '+ts)
            tails.append({'name':ts,'offset':offset,'target':reloc['target'],'actual':value})
    if symbols['data_020a0eac']!=symbols['data_020a0eac_c']:raise ValueError('Canonical heap alias duplicated')
    strong_undefined=[r['name'] for r in records if r['section']==0 and r['name'] and r['info']>>4==1]
    if strong_undefined:raise ValueError('Strong undefined dependencies: '+str(strong_undefined))
    return {'status':'PASS_STATIC_LINKED_ABI','elf_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'aliases':aliases,'all_native_primary_vtables':native,'metadata':metadata,
        'retained_name_interval_tail_relocations':tails,'canonical_heap':symbols['data_020a0eac'],
        'strong_undefined':strong_undefined,
        'limits':['Static contract only; unadmitted actors and dynamic RTTI still require runtime gates.',
                  'Synthetic Poker access does not prove natural rabbit unlock or exact deal parity.']}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['root','binary','audit','policy','bindings','output']:p.add_argument(name,type=Path)
    a=p.parse_args();result=verify(a.root,a.binary,json.loads(a.audit.read_text()),json.loads(a.policy.read_text()),set(a.bindings.read_text().splitlines()))
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(result['status'])
