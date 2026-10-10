#!/usr/bin/env python3
"""Private metadata bridge for proven retail class/single-public-base RTTI.

No reconstructed class layout, method implementation, ctor or actor admission
is invented. Unsupported record kinds/relocations stop generation. Compiler
layout oracle and subsequent linked/runtime gates remain separate evidence.
"""
import argparse, hashlib, json
from pathlib import Path

KINDS={
 '_ZTVN3abi17__class_type_infoE':(8,'_ZTVN10__cxxabiv117__class_type_infoE'),
 '_ZTVN3abi20__si_class_type_infoE':(12,'_ZTVN10__cxxabiv120__si_class_type_infoE'),
}
def generate(root,audit,names):
    records=audit['symbols'];lines=['// Private retail RTTI metadata: no synthetic class or methods.',
        'static_assert(sizeof(void*)==4,"retail metadata requires ARM32");','asm(R"RTTI(']
    ledger=[];emitted=set()
    for name in names:
        if not name.startswith('_ZTI'):raise ValueError('Expected explicit RTTI symbol: '+name)
        record=records[name]
        if record['module']!='6':raise ValueError('Only audited ov6 ownership supported')
        start=int(record['address'],16);relocs=sorted(record['relocations'],key=lambda r:int(r['at'],16))
        if not relocs or int(relocs[0]['at'],16)!=start:raise ValueError('Missing RTTI vptr relocation')
        size,native=KINDS[relocs[0]['target']]
        if record['bytes']!=size or len(relocs)!=size//4:
            raise ValueError('RTTI width/relocation count mismatch: '+name)
        if [int(r['at'],16)-start for r in relocs]!=list(range(0,size,4)):
            raise ValueError('RTTI contains unexpected scalar/interior fields')
        ts='_ZTS'+name[4:]
        if relocs[1]['target']!=ts:raise ValueError('RTTI name relocation mismatch')
        name_record=records[ts]
        if name_record['module']!='6':
            raise ValueError('RTTI name must be literal in the same overlay')
        blob_path=root/'extracted/overlays/overlay_0006.bin';blob=blob_path.read_bytes()
        nstart=int(name_record['address'],16)-0x020bfec0
        literal=blob[nstart:nstart+name_record['bytes']].split(b'\0',1)[0]
        literal_end=int(name_record['address'],16)+len(literal)+1
        if any(int(r['at'],16)<literal_end for r in name_record['relocations']):
            raise ValueError('Relocation overlaps RTTI name literal')
        if literal.decode('ascii')!=name[4:]:raise ValueError('RTTI literal does not match mangled name')
        if ts not in emitted:
            lines += [f'.pushsection .rodata.private_rtti.{ts},"a",%progbits',
                f'.global {ts}',f'.type {ts},%object',f'{ts}:',
                '.byte '+','.join(str(v) for v in literal+b'\0'),
                f'.size {ts},.-{ts}','.popsection']
            emitted.add(ts)
        lines += [f'.pushsection .rodata.private_rtti.{name},"a",%progbits','.balign 4',
                  f'.global {name}',f'.type {name},%object',f'{name}:',
                  f'.word {native}+8',f'.word {ts}']
        if size==12:
            parent=relocs[2]['target']
            if not parent.startswith('_ZTI'):raise ValueError('Single base is not RTTI')
            lines.append('.word '+parent)
        lines += [f'.size {name},.-{name}','.popsection']
        ledger.append({'symbol':name,'original':record,'native_vptr':native+'+8',
            'literal':literal.decode(),'literal_bytes_with_terminator':len(literal)+1,
            'name_interval_tail_relocations':name_record['relocations'],
            'original_binary_sha256':hashlib.sha256(blob).hexdigest(),
            'contract':'Only RTTI namespace/address-point translation; original name and single-public-base edge retained.'})
        emitted.add(name)
    lines+=[')RTTI");']
    return '\n'.join(lines)+'\n',{'status':'PRIVATE_METADATA_GENERATED_LINK_RUNTIME_GATES_PENDING',
        'records':ledger,'defined_symbols':sorted(emitted),
        'limits':['No native class size/method/actor admission claim',
                  'Require independent native compiler record oracle and linked alias verification',
                  'Require linked native type_info behavior before actor runtime gates']}

def preserved_storage(root,audit,defined,names):
    # Keep every original RTTI/name interval and its trailing vtable prefixes.
    # Only four checked TI/TS records bypass the native-only guard; all other
    # missing native types remain rejected by the production generator.
    import minigame_storage
    _,proof=generate(root,audit,names)
    allowed=set(proof['defined_symbols'])
    native_targets={k:v[1]+' + 8' for k,v in KINDS.items()}
    result=minigame_storage.generate(root,audit,defined,metadata_types=names)
    for name in allowed:
        if result.count(name+':\n')!=1:raise ValueError('Metadata interval not retained once: '+name)
    proof.update(status='PRESERVED_INTERVAL_CANDIDATE_LINK_RUNTIME_GATES_PENDING',
        production_generator_sha256=hashlib.sha256(Path(minigame_storage.__file__).read_bytes()).hexdigest(),
        metadata_vptr_targets=native_targets,
        contract='Original TI/TS records and entire trailing data retained; only RTTI vptr rebind and explicit type-guard exception.')
    return result,proof

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('audit',type=Path)
    p.add_argument('output',type=Path);p.add_argument('symbols',nargs='+')
    p.add_argument('--native-bindings',type=Path);a=p.parse_args()
    audit=json.loads(a.audit.read_text())
    if a.native_bindings:
        text,report=preserved_storage(a.root,audit,set(a.native_bindings.read_text().splitlines()),a.symbols)
    else:text,report=generate(a.root,audit,a.symbols)
    a.output.write_text(text);a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
