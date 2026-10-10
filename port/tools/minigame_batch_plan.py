#!/usr/bin/env python3
"""Private overlay6 batch preparation. Never writes product or admits actors.

Plan, prepare and compile are separate, explicitly requested stages. A complete
ROM-data span and all retail ctors prevent next-label/indirect-writer omissions;
native type/ABI/link/runtime evidence remains an independent gate.
"""
import argparse
import bisect
import hashlib
import importlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def sections(text):
    result = []
    for line in text.splitlines():
        if line and not line[0].isspace():
            break
        m = re.search(r'(\.\w+)\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:(\w+)', line)
        if m:
            result.append((m[1], int(m[2], 16), int(m[3], 16), m[4]))
    return result


def read_module(root, module):
    cfg = root / 'config/arm9'
    if module.isdigit():
        cfg /= f'overlays/ov{int(module):03}'
    elif module != 'main':
        cfg /= module
    text = (cfg / 'symbols.txt').read_text()
    symbols = {}
    for name, kind, address in re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)', text, re.M):
        symbols.setdefault(int(address, 16), []).append((name, kind))
    sec = sections((cfg / 'delinks.txt').read_text())
    relocs = [(int(a, 16), k, int(t, 16), m) for a, k, t, m in re.findall(
        r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',
        (cfg / 'relocs.txt').read_text())]
    return {'symbols': symbols, 'sections': sec, 'relocs': relocs}


def module_candidates(hint):
    if hint in ('main', 'itcm', 'dtcm'):
        return [hint]
    return re.findall(r'\d+', hint)


def condensed_source_DAG(sources, funcs, edges):
    """Tarjan SCCs make real recursive/shared call cycles explicit, not a fake DAG."""
    owners={f['symbol']: f['source'] for f in funcs if f['source']}
    graph={path:set() for path in sources}
    for edge in edges:
        resolved=edge['resolved']
        a=owners.get(edge['from'])
        b=owners.get(resolved['symbol']) if resolved and resolved['module']=='6' else None
        if a and b and a!=b:graph[a].add(b)
    index=0;indices={};low={};stack=[];active=set();components=[]
    def visit(node):
        nonlocal index
        indices[node]=low[node]=index;index+=1;stack.append(node);active.add(node)
        for target in sorted(graph[node]):
            if target not in indices:visit(target);low[node]=min(low[node],low[target])
            elif target in active:low[node]=min(low[node],indices[target])
        if low[node]==indices[node]:
            component=[]
            while True:
                target=stack.pop();active.remove(target);component.append(target)
                if target==node:break
            components.append(sorted(component))
    for node in sorted(graph):
        if node not in indices:visit(node)
    ids={path:i for i,component in enumerate(components) for path in component}
    links=sorted({(ids[a],ids[b]) for a in graph for b in graph[a] if ids[a]!=ids[b]})
    return {'strongly_connected_source_components':components,'condensed_DAG_edges':links,
            'note':'Each multi-source component is an explicit cycle; no dependency is dropped.'}


def plan(root, out):
    sys.path.insert(0, str(root / 'tools'))
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
    import srcpath
    srcpath.set_root(root)
    ov = read_module(root, '6')
    modules = {'6': ov}
    blockers = []
    cfg = root / 'config/arm9/overlays/ov006'
    blob = (root / 'extracted/overlays/overlay_0006.bin').read_bytes()
    base = min(s for _, s, _, kind in ov['sections'] if kind == 'code')
    funcs = []
    sources = {}
    for address, labels in sorted(ov['symbols'].items()):
        for name, kind in labels:
            m = re.match(r'function\([^,]+,size=0x([0-9a-f]+)\)', kind)
            if not m:
                continue
            size = int(m[1], 16)
            path = srcpath.path_for(name)
            if path is None:
                blockers.append({'kind': 'missing_original_source', 'symbol': name})
            else:
                rel = str(path.relative_to(root))
                sources.setdefault(rel, []).append(name)
            funcs.append({'symbol': name, 'address': address, 'size': size,
                          'source': str(path.relative_to(root)) if path else None})
    for path, names in sorted(sources.items()):
        text = (root / path).read_text()
        if importlib.import_module('hostgen').is_asm(text):
            blockers.append({'kind': 'original_ARM_asm', 'source': path, 'symbols': names})
    starts = [f['address'] for f in funcs]
    edges = []
    for at, kind, target, hint in ov['relocs']:
        index = bisect.bisect_right(starts, at) - 1
        owner = funcs[index] if index >= 0 and at < starts[index] + funcs[index]['size'] else None
        candidates = []
        for module in module_candidates(hint):
            if module not in modules:
                try:
                    modules[module] = read_module(root, module)
                except FileNotFoundError:
                    modules[module] = {'symbols': {}, 'sections': [], 'relocs': []}
            for name, target_kind in modules[module]['symbols'].get(target, []):
                candidates.append({'module': module, 'symbol': name, 'kind': target_kind})
        # This exact preference is the existing reviewed minigame data contract.
        # Record every alternative and the reason, rather than hiding ambiguity.
        chosen = [c for c in candidates if c['module'] in ('4', '6')]
        chosen.sort(key=lambda c: (c['module'] != '4', c['symbol']))
        same_module_aliases=bool(candidates) and len({c['module'] for c in candidates})==1
        canonical=sorted(candidates,key=lambda c:(
            'size=0x0)' in c['kind'], not c['symbol'].startswith('data_'), c['symbol']))
        resolved = chosen[0] if chosen else canonical[0] if same_module_aliases else None
        if resolved is None:
            blockers.append({'kind': 'unresolved_module_relocation', 'at': hex(at),
                             'target': hex(target), 'hint': hint, 'candidates': candidates})
        edges.append({'from': owner['symbol'] if owner else hex(at), 'at': hex(at),
                      'kind': kind, 'target': hex(target), 'hint': hint,
                      'candidates': candidates, 'resolved': resolved,
                      'resolution_contract': 'active-minigame-ov4-before-ov6' if chosen else 'same-module-retail-address-aliases',
                      'native_alias_identity_gate': 'PENDING' if same_module_aliases and len(candidates)>1 else 'not-applicable'})
    ctor_section = next((s, e) for n, s, e, _ in ov['sections'] if n == '.ctor')
    ctor_words = struct.unpack_from('<' + str((ctor_section[1] - ctor_section[0]) // 4) + 'I',
                                    blob, ctor_section[0] - base)
    if struct.unpack_from('<I', blob, ctor_section[1] - base)[0] != 0:
        blockers.append({'kind': 'retail_ctor_missing_terminator'})
    ctors = []
    for index, address in enumerate(ctor_words):
        labels = [n for n, k in ov['symbols'].get(address, []) if k.startswith('function')]
        if len(labels) != 1 or not labels[0].startswith('__sinit'):
            blockers.append({'kind': 'retail_ctor_identity', 'address': hex(address), 'labels': labels})
        symbol=labels[0] if len(labels)==1 else None
        original=srcpath.path_for(symbol) if symbol else None
        text=original.read_text() if original else ''
        code=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
        code=re.sub(r'\bextern\s+[^;{}]*;','',code)
        vectors=[]
        for match in re.finditer(r'__cxa_vec_ctor\s*\(([^;]+)\);',code):
            arguments=match[1].split(',')
            if len(arguments)!=5:continue
            root_name=arguments[0].strip().lstrip('&').strip();values=[]
            for argument in arguments[1:3]:
                try:values.append(int(argument.strip(),0))
                except ValueError:values.append(None)
            address_candidates=[a for a,ls in ov['symbols'].items() if any(n==root_name for n,_ in ls)]
            element_count,stride=values
            object_start=address_candidates[0] if len(address_candidates)==1 else None
            total=element_count*stride if element_count is not None and stride is not None else None
            in_BSS=object_start is not None and total is not None and any(
                k=='bss' and s<=object_start and object_start+total<=e for _,s,e,k in ov['sections'])
            row={'original_call':match[0], 'root':root_name,'count':element_count,'stride':stride,
                 'constructor':arguments[3].strip(),'destructor':arguments[4].strip(),
                 'bytes':total,'whole_BSS_contains_vector':in_BSS}
            vectors.append(row)
            if not in_BSS:blockers.append({'kind':'ctor_vector_dimensions_or_extent_unproved',
                                         'ctor':symbol,'vector':row})
        ctors.append({'retail_order': index, 'address': hex(address), 'symbol':symbol,
                      'source':str(original.relative_to(root)) if original else None,
                      'source_sha256':digest(original) if original else None,'vector_objects':vectors,
                      'resource_or_exit_registration_calls':[line.strip() for line in code.splitlines()
                         if re.search(r'func_020(?:17acc|17a24|731dc)|SharedFilePtr',line) and not line.lstrip().startswith('extern')]})
    data_sections = [(n, s, e) for n, s, e, k in ov['sections'] if k in ('rodata', 'data', 'bss') and n != '.ctor']
    roots = {n for address, labels in ov['symbols'].items() for n, k in labels
             if not k.startswith('function') and any(s <= address < e for _, s, e in data_sections)}
    # Data referenced directly from function/ctor literal pools is not reachable
    # from the overlay's data tables. Include it from the code relocation graph.
    original_identifiers=set()
    for path in sources:
        text=re.sub(r'/\*.*?\*/|//[^\n]*','',(root/path).read_text(),flags=re.S)
        original_identifiers.update(re.findall(r'\b\w+\b',text))
    code_data_roots={e['resolved']['symbol'] for e in edges if e['resolved'] and
                     not e['resolved']['kind'].startswith('function') and
                     any(c['symbol'] in original_identifiers for c in e['candidates'])}
    # This fixed DS RAM window has no ROM extent. Its actual read/write forms
    # are already rewritten by hostgen to ntr::host_ptr, rather than storage.
    platform_providers=[]
    if 'data_023c0000' in code_data_roots:
        transform=(Path(__file__).resolve().parents[1] / 'tools/hostgen.py').read_text()
        if "ntr::host_ptr(0x023c0000u)" not in transform:
            raise ValueError('Shared RAM provider transform missing')
        code_data_roots.remove('data_023c0000')
        platform_providers.append({'symbol':'data_023c0000','address':'0x023c0000',
            'provider':'ntr::host_ptr(0x023c0000u)','proof':'hostgen.transform rewrites &data_023c0000 and (int)data_023c0000',
            'transform_sha256':digest(Path(__file__).resolve().parents[1] / 'tools/hostgen.py'),
            'native_undefined_gate':'Must remain absent from compiled undefined symbols; no synthetic storage extent.'})
    roots.update(code_data_roots)
    roots.update((Path(__file__).resolve().parents[1] / 'menu/wanted_storage_roots.txt').read_text().splitlines())
    roots.discard('')
    native_types = sorted(n for n in roots if n.startswith(('_ZTI', '_ZTS', '_ZTV')))
    layout = json.loads((Path(__file__).resolve().parents[1] / 'tools/native_vtable_layouts.json').read_text())
    proved = set(layout['gcc_primary_tables'])
    table_status = [{'symbol': n, 'current_primary_layout_proved': n in proved,
                     'native_definition_and_linked_prefix_gate': 'PENDING'} for n in native_types if n.startswith('_ZTV')]
    for row in table_status:
        if not row['current_primary_layout_proved']:
            blockers.append({'kind': 'native_vtable_prefix_unproved', 'symbol': row['symbol'],
                             'next': 'Compile original owner and inspect ELF; never silently offset unknown tables.'})
    report = {'status': 'PRIVATE_BATCH_PLAN_REVIEW_REQUIRED', 'runtime_admission': [],
              'root': str(root), 'source_owners': sources, 'functions': funcs, 'relocation_edges': edges,
              'source_dependency_DAG':condensed_source_DAG(sources,funcs,edges),
              'constructors': ctors, 'storage_sections': [{'section': n, 'start': hex(s), 'end': hex(e), 'bytes': e-s}
                                                      for n, s, e in data_sections],
              'native_types': native_types, 'vtable_ABI': table_status, 'blockers': blockers,
              'code_relocation_data_roots':sorted(code_data_roots),
              'platform_owned_data_providers':platform_providers,
              'source_sha256': {path: digest(root / path) for path in sources},
              'input_sha256': {str(p): digest(p) for p in [cfg/'symbols.txt', cfg/'delinks.txt', cfg/'relocs.txt',
                                root/'extracted/overlays/overlay_0006.bin', Path(__file__).resolve().parents[1] / 'menu/wanted_storage_roots.txt']},
              'limits': ['Plan is not compile/link/runtime evidence.',
                         'All constructors are candidates for a private whole overlay, not public automatic admission.']}
    import minigame_imports
    catalog=minigame_imports.audit(root)
    report['actor_catalog']=catalog['retail_catalog']
    report['profile_identity_gate']=catalog['status']
    out.mkdir(parents=True, exist_ok=True)
    (out/'storage-roots.txt').write_text('\n'.join(sorted(roots))+'\n')
    (out/'native-type-candidates.txt').write_text('\n'.join(native_types)+'\n')
    (out/'all-ctor-candidates.inc').write_text(''.join(
        f"SM64DS_MINIGAME_INIT({r['symbol']},{r['address']}) // retail order {r['retail_order']}\n"
        for r in ctors if r['symbol']))
    save(out/'plan.json', report)
    return report


def prepare(root, out, report, extra_consumers):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
    import minigame_data_audit, minigame_initializer_audit, minigame_oam_extent_audit
    import hostgen, minigame_sources, batch_native_abi
    audit = minigame_data_audit.audit(root, (out/'storage-roots.txt').read_text().splitlines())
    gaps = []
    # All data labels, not just symbol-reachable labels, must cover the original
    # sections. Any unlabeled gap is explicitly retained only if relocation-free.
    ov = read_module(root, '6')
    for section in report['storage_sections']:
        start, end = int(section['start'],16), int(section['end'],16)
        intervals = sorted((int(v['address'],16), int(v['end'],16)) for v in audit['symbols'].values()
                           if v['module']=='6' and 'end' in v and start <= int(v['address'],16) < end)
        cursor = start
        for a, b in intervals + [(end,end)]:
            if a > cursor:
                name = f'batch_ov006_gap_{cursor:08x}'
                relocations = [r for r in ov['relocs'] if cursor <= r[0] < a]
                gaps.append({'start':hex(cursor),'end':hex(a),'relocations':relocations})
                if relocations:
                    raise ValueError('Unlabeled storage gap contains relocations; explicit review required: '+name)
                audit['symbols'][name]={'module':'6','address':hex(cursor),'end':hex(a),
                    'kind':'bss' if section['section']=='.bss' else 'data(any)',
                    'bytes':a-cursor,'relocations':[]}
                audit['roots'].append(name)
            if b > end:
                raise ValueError('Next-label extent crosses retail section end')
            cursor=max(cursor,b)
        if cursor != end:
            raise ValueError('Whole storage interval incomplete')
    audit['whole_overlay_storage']={'status':'COMPLETE_INTERVALS', 'sections':report['storage_sections'],
                                    'unlabeled_gaps':gaps,'OAM_extent_policy':'Every original data byte retained, including all interior labels; no clamp.'}
    save(out/'storage-audit.json',audit)
    original = [root/path for path in report['source_owners']]
    init = minigame_initializer_audit.audit(root,audit,original)
    present={row['symbol'] for row in report['constructors']}
    required=set(init['additional_constructor_candidates'])
    init['whole_overlay_ctor_gate']={'status':'PASS' if required <= present else 'MISSING',
                                   'required_not_in_retail_table':sorted(required-present),
                                   'full_retained_BSS_and_all_retail_ctors':True}
    save(out/'initializer-audit.json',init)
    consumer_files=[Path(__file__).resolve().parents[1] / 'menu/wanted_oam_consumers.txt',*extra_consumers]
    names=[]
    for path in consumer_files:
        names.extend(line.strip() for line in path.read_text().splitlines() if line.strip() and not line.lstrip().startswith('#'))
    names=list(dict.fromkeys(names))
    save(out/'oam-extent-audit.json',minigame_oam_extent_audit.audit(root,audit,names))
    generated=out/'generated'
    emitted=[]
    for original_path in original:
        if hostgen.is_asm(original_path.read_text()):
            emitted.append({'source':str(original_path),'status':'ARM_ASM_BLOCKER'})
            continue
        try:
            path,rewrites=hostgen.emit(original_path,generated,root,True,True)
            emitted.append({'source':str(original_path),'generated':str(path),'MMIO_rewrites':rewrites,'status':'GENERATED'})
        except (ValueError,AssertionError) as error:
            emitted.append({'source':str(original_path),'status':'GENERATION_ABI_BLOCKER','error':str(error)})
    # Run existing narrow repairs one source at a time so a new TU failure
    # cannot hide the remaining batch's explicit blocker inventory.
    adaptation=[]
    for row in emitted:
        if 'generated' not in row:continue
        isolated=out/'adaptation-one';isolated.mkdir(exist_ok=True)
        path=Path(row['generated']);copy=isolated/path.name
        copy.write_text(path.read_text())
        try:
            minigame_sources.adapt(isolated);path.write_text(copy.read_text())
            adapted,evidence=batch_native_abi.adapt(Path(row['source']),path.read_text(),root)
            path.write_text(adapted);row['batch_ABI_evidence']=evidence
        except (ValueError,AssertionError) as error:
            row['status']='GENERATION_ABI_BLOCKER';row['error']=str(error)
            adaptation.append({'source':str(path),'error':str(error)})
        finally:copy.unlink()
    for row in emitted:
        if 'generated' in row:row['sha256']=digest(row['generated'])
    save(out/'generation.json',{'sources':emitted,'runtime_admission':[],'adaptation_blockers':adaptation,
        'status':'GENERATED_PENDING_ARM_ABI_LINK_GATES'})


def freeze_common(root,out,common_build,manifest_path):
    manifest=json.loads(manifest_path.read_text());frozen_dir=out/'common-inputs';frozen_dir.mkdir(exist_ok=True)
    frozen={}
    def clone(source,target,expected):
        if digest(source)!=expected:raise ValueError('Common input changed from manifest: '+str(source))
        if not target.exists():subprocess.run(['rtk','proxy','cp','-c',str(source),str(target)],check=True)
        if source.stat().st_ino==target.stat().st_ino or digest(target)!=expected:
            raise ValueError('Independent clone/hash contract failed: '+str(target))
    for index,(name,expected) in enumerate(manifest['inputs'].items()):
        source=Path(name);target=frozen_dir/(f'{index:04}-'+source.name)
        clone(source,target,expected);frozen[name]={'copy':str(target),'sha256':expected}
        if index%256==0:print(f'Freeze common {index+1}/{len(manifest["inputs"])}',flush=True)
    elf=common_build/'smoke_player.elf';elf_copy=out/'baseline-common.elf'
    clone(elf,elf_copy,manifest['elf_sha256'])
    flags=common_build/'CMakeFiles/sm64ds_wanted.dir/flags.make'
    (out/'wanted-flags.make').write_text(flags.read_text())
    for name in ('game_entry.cpp','renderer_defaults.h'):
        source=Path(__file__).resolve().parents[1] / 'platform/3ds'/name
        (out/('baseline-'+name)).write_text(source.read_text())
    save(out/'common-freeze.json',{'status':'INDEPENDENT_COMMON_INPUTS_HASH_VERIFIED','inputs':frozen,
        'manifest_path':str(manifest_path),'manifest_sha256':digest(manifest_path),
        'elf':str(elf_copy),'elf_sha256':digest(elf_copy),
        'flags_sha256':digest(out/'wanted-flags.make'),
        'scope':'Independent APFS clones, no shared object/library mutation; no physical-space saving claim.'})
    return elf_copy


def compile_batch(root, out, common_build,common_manifest,prior_compile=None,frozen_baseline=None):
    """Compile each private TU, keep all errors; do not mutate or link common."""
    shared=common_build
    if frozen_baseline:
        frozen=json.loads((frozen_baseline/'common-freeze.json').read_text())
        if frozen['manifest_sha256']!=digest(common_manifest):raise ValueError('Reused baseline manifest differs')
        if any(digest(r['copy'])!=r['sha256'] for r in frozen['inputs'].values()):raise ValueError('Reused frozen input changed')
        if digest(frozen['elf'])!=frozen['elf_sha256']:raise ValueError('Reused baseline ELF changed')
        save(out/'common-freeze.json',frozen)
        (out/'wanted-flags.make').write_text((frozen_baseline/'wanted-flags.make').read_text())
        baseline=Path(frozen['elf'])
    else:baseline=freeze_common(root,out,shared,common_manifest)
    flags_file=out/'wanted-flags.make'
    flag_text=flags_file.read_text()
    flags=['-I'+str(Path(__file__).resolve().parents[1] / 'platform/3ds')]
    for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS'):
        flags+=shlex.split(re.search('^'+key+' = (.*)$',flag_text,re.M)[1])
    generated=json.loads((out/'generation.json').read_text())['sources']
    objects=out/'objects';objects.mkdir(exist_ok=True)
    rows=[]
    previous={}
    if prior_compile:
        for row in json.loads(prior_compile.read_text())['sources']:
            if row.get('status')=='PASS':previous[row['source'].split('/generated/')[-1]]=row
    for index,row in enumerate(generated):
        if row['status']!='GENERATED':rows.append(row);continue
        source=Path(row['generated']);obj=objects/(f'{index:03}-'+source.stem+'.o')
        prior=previous.get(str(source).split('/generated/')[-1])
        if prior and digest(source)==prior['source_sha256'] and digest(prior['object'])==prior['object_sha256']:
            cached=dict(prior);cached['source']=str(source);cached['reused_private_object']=True;rows.append(cached)
            print(f'[{index+1}/{len(generated)}] {source.name}: prior exact object reused',flush=True)
            continue
        command=['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags,'-c',str(source),'-o',str(obj)]
        try:run=subprocess.run(['rtk','proxy',*command],capture_output=True,text=True,timeout=120)
        except subprocess.TimeoutExpired as error:
            run=subprocess.CompletedProcess(command,124,
                error.stdout.decode() if isinstance(error.stdout,bytes) else error.stdout or '',
                (error.stderr.decode() if isinstance(error.stderr,bytes) else error.stderr or '')+'\nPRIVATE_TU_TIMEOUT\n')
        log=objects/(obj.stem+'.log');log.write_text(run.stdout+run.stderr)
        rows.append({'source':str(source),'command':command,'status':'PASS' if run.returncode==0 else 'COMPILE_BLOCKER',
                     'returncode':run.returncode,'log':str(log),'object':str(obj) if run.returncode==0 else None,
                     'source_sha256':digest(source),'object_sha256':digest(obj) if run.returncode==0 else None})
        print(f'[{index+1}/{len(generated)}] {source.name}: {rows[-1]["status"]}',flush=True)
    summarize_batch(root,out,rows,flags_file)


def summarize_batch(root,out,rows,flags_file):
    baseline=Path(json.loads((out/'common-freeze.json').read_text())['elf'])
    compiled=[row['object'] for row in rows if row.get('object')]
    nm=subprocess.run(['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g',*compiled],
                      capture_output=True,text=True,check=True) if compiled else None
    if nm:
        (out/'compiled-symbols.txt').write_text(nm.stdout)
        defined=set(re.findall(r'^[0-9a-f]+ [A-TV-Z] (\S+)$',nm.stdout,re.M))
        undefined=set(re.findall(r'^\s+U (\S+)$',nm.stdout,re.M))-defined
    else:defined=set();undefined=set()
    common_nm=subprocess.run(['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g',str(baseline)],
                             capture_output=True,text=True,check=True).stdout
    (out/'baseline-common-symbols.txt').write_text(common_nm)
    common_defined=set(re.findall(r'^[0-9a-f]+ [A-TV-Z] (\S+)$',common_nm,re.M))
    # A final ELF has garbage-collected many valid common dependencies. Read
    # the actual immutable .o/.a inputs before classifying missing code.
    frozen=json.loads((out/'common-freeze.json').read_text())['inputs']
    files=[r['copy'] for r in frozen.values() if Path(r['copy']).suffix in ('.o','.a')]
    object_witness=[];common_object_defined=set()
    for begin in range(0,len(files),200):
        result=subprocess.run(['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm',
                               '-g','--defined-only',*files[begin:begin+200]],
                              capture_output=True,text=True,check=True)
        common_object_defined.update(re.findall(r'^[0-9a-f]+ [A-TV-Z] (\S+)$',result.stdout,re.M))
        object_witness.append({'first_input':begin,'input_count':len(files[begin:begin+200]),
                               'output_sha256':hashlib.sha256(result.stdout.encode()).hexdigest()})
    save(out/'frozen-common-defined.json',{'defined':sorted(common_object_defined),
        'batches':object_witness,'scope':'All exact frozen common .o/.a definitions, not only gc-surviving ELF symbols.'})
    plan_data=json.loads((out/'plan.json').read_text())
    available_common=common_defined|common_object_defined
    missing_types=sorted(set(plan_data['native_types'])-defined-available_common)
    external_missing=sorted(undefined-available_common)
    storage_status='BLOCKED_MISSING_NATIVE_TYPES'
    if not missing_types:
        sys.path.insert(0,str(Path(__file__).resolve().parents[1] / 'tools'))
        import minigame_storage
        bindings=set((Path(__file__).resolve().parents[1] / 'menu/wanted_native_bindings.txt').read_text().splitlines())
        bindings.update(n for n in defined|available_common if n.startswith(('_ZTI','_ZTS','_ZTV')))
        # Current native main-memory symbols remain canonical; no cloned strong
        # heap or mirrored main ownership is introduced by full ov6 closure.
        audit=json.loads((out/'storage-audit.json').read_text())
        bindings.update(n for n,r in audit['symbols'].items() if r['module']=='main' and n in available_common)
        (out/'native-bindings.txt').write_text('\n'.join(sorted(bindings))+'\n')
        (out/'storage-candidate.cpp').write_text(minigame_storage.generate(root,audit,bindings))
        if '.global data_020a0eac\n' in (out/'storage-candidate.cpp').read_text():
            raise ValueError('Canonical heap alias would be duplicated')
        storage_status='CANDIDATE_EMITTED_PENDING_LINKED_ABI_GATES'
    save(out/'compile-report.json',{'status':'COMPILE_BLOCKERS' if any(row['status']!='PASS' for row in rows) else 'ARM_OBJECTS_COMPILED_LINK_PENDING',
        'sources':rows,'native_definition_missing':missing_types,'undefined_not_defined_within_batch':sorted(undefined),
        'undefined_not_in_frozen_common':external_missing,'storage_generation':storage_status,
        'dependencies_present_in_common_inputs_but_gc_removed_from_ELF':sorted((undefined-common_defined)&common_object_defined),
        'flag_source_sha256':digest(flags_file),'runtime_admission':[],
        'limits':['Undefined batch symbols may already exist in common; require exact frozen link closure.',
                  'Native vtable prefixes, alias identity and constructor/teardown require linked verification.']})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('out',type=Path)
    parser.add_argument('--prepare',action='store_true')
    parser.add_argument('--compile',action='store_true')
    parser.add_argument('--summarize',action='store_true',help='Reclassify existing objects without recompiling')
    parser.add_argument('--common-build',type=Path)
    parser.add_argument('--common-manifest',type=Path)
    parser.add_argument('--prior-compile',type=Path)
    parser.add_argument('--frozen-baseline',type=Path)
    parser.add_argument('--oam-consumers',type=Path,action='append',default=[])
    args=parser.parse_args();root=args.root.resolve();out=args.out.resolve()
    if args.summarize:
        rows=json.loads((out/'compile-report.json').read_text())['sources']
        summarize_batch(root,out,rows,out/'wanted-flags.make')
    elif args.compile:
        if not args.common_build or not args.common_manifest:
            parser.error('--compile requires --common-build and --common-manifest')
        compile_batch(root,out,args.common_build.resolve(),args.common_manifest.resolve(),
                      args.prior_compile,args.frozen_baseline)
    else:
        report=plan(root,out)
        if args.prepare:prepare(root,out,report,args.oam_consumers)
    print('Private batch stage terminal; no common edit or actor admission.',flush=True)


if __name__=='__main__':
    main()
