#!/usr/bin/env python3
"""Prepare complete ov6 storage/ctors and reviewed native sources without admitting actors.

Two initialized source owners are compiled solely to prove ROM data ownership.
The target build and final linked ABI/runtime gates remain mandatory.
"""
import argparse, hashlib, json, re, shutil, subprocess
from pathlib import Path
import minigame_batch_plan as batch
import hostgen, batch_native_abi, initialized_data_owner, minigame_storage


def prepare(root, stage, compiler):
    policy=json.loads((Path(__file__).resolve().parents[1] / 'menu/overlay6_native_policy.json').read_text())
    out=stage/'batch-audit';out.mkdir(parents=True,exist_ok=True)
    report=batch.plan(root,out)
    bindings=set((Path(__file__).resolve().parents[1] / 'menu/wanted_native_bindings.txt').read_text().splitlines())
    shared={(r['at'],r['target'],r['hint']):r for r in policy['shared_native_relocations']}
    fatal=[]
    for blocker in report['blockers']:
        if blocker['kind']=='native_vtable_prefix_unproved':continue
        key=(blocker.get('at'),blocker.get('target'),blocker.get('hint'))
        reviewed=shared.get(key)
        if reviewed and reviewed['native_symbol'] in bindings and any(
            r['module']=='1' and r['symbol']==reviewed['native_symbol'] for r in blocker['candidates']):
            blocker['reviewed_native_provider']=reviewed['native_symbol']
        else:fatal.append(blocker)
    if fatal:raise ValueError('Original closure blockers: '+str(fatal))
    batch.prepare(root,out,report,[])
    generation=json.loads((out/'generation.json').read_text())
    if generation['adaptation_blockers'] or any(r['status']!='GENERATED' for r in generation['sources']):
        raise ValueError('Native ABI adaptation blockers')
    audit=json.loads((out/'storage-audit.json').read_text())
    if audit['unresolved']:raise ValueError('Unresolved original storage roots')
    init=json.loads((out/'initializer-audit.json').read_text())
    if init['whole_overlay_ctor_gate']['status']!='PASS':raise ValueError('BSS constructor omissions')
    oam=json.loads((out/'oam-extent-audit.json').read_text())
    if oam['status']!='PASS':raise ValueError('OAM extents incomplete: '+str(oam['status']))
    import minigame_imports
    imports=minigame_imports.audit(root)
    if sorted(r['actor'] for r in imports['imports'])!=policy['admitted_actor_ids']:
        raise ValueError('Actor admission differs from the reviewed runtime policy')
    if [r['function'] for r in imports['initializers']] != [r['symbol'] for r in report['constructors']]:
        raise ValueError('Runtime constructor list must equal the entire retail order')
    for source in (out/'generated/src').rglob('*.cpp'):
        destination=stage/'src'/source.relative_to(out/'generated/src')
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    # Main helpers keep canonical original owners; no reconstructed source edits.
    helper_evidence=[]
    for symbol in policy['helpers']:
        original=root/'src'/(symbol+'.c')
        path,_=hostgen.emit(original,out/'helpers',root,True,True)
        text,evidence=batch_native_abi.adapt(original,path.read_text(),root)
        destination=stage/'src'/path.relative_to(out/'helpers/src')
        destination.parent.mkdir(parents=True,exist_ok=True);destination.write_text(text)
        helper_evidence.append(evidence)
    # Compile exact current generated owners before making declarations external.
    flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations',
           '-ffunction-sections','-D__3DS__','-O2','-DNDEBUG','-std=gnu++17','-fsigned-char',
           '-fno-strict-aliasing','-Wno-narrowing','-Werror=return-type','-fpermissive',
           '-DSM64DS_NATIVE_SHADOW','-DSM64DS_PLATFORM_PC',
           '-I'+str(Path(__file__).resolve().parents[1]),'-I'+str(root/'include'),'-I'+str(Path(__file__).resolve().parents[1] / 'ntr/include'),
           '-isystem','/opt/devkitpro/libctru/include']
    ownership=[]
    for request in policy['ownership']:
        source=stage/Path(request['source']).with_suffix('.cpp')
        obj=out/(source.stem+'.ownership.o');log=out/(source.stem+'.ownership.log')
        command=[compiler,*flags,'-c',str(source),'-o',str(obj)]
        with log.open('w') as stream:result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
        if result.returncode:raise ValueError('Ownership precompile failed: '+str(log))
        proof=initialized_data_owner.prove(root,audit,obj,request['symbols'])
        text=source.read_text();candidate=initialized_data_owner.extern_definitions(text,request['symbols'])
        proof.update(source=str(source),source_sha256=hashlib.sha256(text.encode()).hexdigest(),
                     candidate_sha256=hashlib.sha256(candidate.encode()).hexdigest(),command=command)
        ownership.append(proof);source.write_text(candidate)
    bindings=set((Path(__file__).resolve().parents[1] / 'menu/wanted_native_bindings.txt').read_text().splitlines())
    storage=minigame_storage.generate(root,audit,bindings,metadata_types=policy['metadata_types'])
    (stage/'storage.cpp').write_text(storage)
    # GNU .set to an external symbol is not an exported alias. Bind audited
    # retail address-point aliases at the final link, never allow duplicate defs.
    options=[]
    for row in policy['vtable_aliases']:
        left=audit['symbols'][row['alias']];right=audit['symbols'][row['target']]
        if (left['module'],left['address'])!=(right['module'],right['address']) or row['offset']!=8:
            raise ValueError('Retail vtable alias identity changed')
        options.append('-Wl,--defsym='+row['alias']+'='+row['target']+'+8')
    (stage/'vtable-link-options.cmake').write_text('set(_wanted_vtable_link_options\n'+''.join('  "'+x+'"\n' for x in options)+')\n')
    batch.save(out/'ownership-proof.json',{'status':'PASS','owners':ownership,'compiler':compiler})
    batch.save(out/'helper-ABI-proof.json',helper_evidence)
    batch.save(out/'integration-gate.json',{'status':'SOURCE_STORAGE_CTOR_OWNERSHIP_PASS_LINK_RUNTIME_PENDING',
        'sources':len(generation['sources']),'ctors':len(imports['initializers']),
        'admitted_actors':[r['actor'] for r in imports['imports']],
        'metadata_types':policy['metadata_types'],'aliases':policy['vtable_aliases'],
        'original_source_sha256':report['source_sha256'],'limits':policy['limits']})
    # Audit intermediates must never become target sources through recursive glob.
    shutil.rmtree(out/'generated');shutil.rmtree(out/'helpers');shutil.rmtree(out/'adaptation-one')
    print('ov6 source/storage/ctor/ownership gates PASS; linked/runtime gates pending')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('stage',type=Path);p.add_argument('compiler')
    a=p.parse_args();prepare(a.root.resolve(),a.stage.resolve(),a.compiler)
