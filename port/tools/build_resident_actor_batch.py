#!/usr/bin/env python3
"""Link a prepared resident actor batch against frozen, private ARM11 inputs.

Never mutates CMake or the production manifest. Reused actor objects must match
both the frozen source and freshly generated production adapter output.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path
from native_level_actor_sources import generate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(root, prepared, reports, rom, output, frozen_baseline=None):
    if output.exists():
        raise ValueError('Preserve prior link/build evidence')
    output.mkdir(parents=True)
    (output/'logs').mkdir()
    preparation = json.loads((prepared/'preparation.json').read_text())
    baseline = frozen_baseline or prepared/'baseline'
    flags_text = (baseline/'flags.make').read_text()
    inputs = json.loads((baseline/'inputs.json').read_text())
    by_original = {r['original_path']: r for r in inputs}
    command = json.loads((baseline/'link-command.json').read_text())
    if not isinstance(command, list):
        raise ValueError('Expected explicit frozen command vector')
    for row in inputs:
        if sha(Path(row['snapshot_path'])) != row['sha256']:
            raise ValueError('Frozen input changed: '+row['snapshot_path'])
    reused = {}
    for path in reports:
        report = json.loads(path.read_text())
        if report['flags_sha256'] != sha(baseline/'flags.make'):
            raise ValueError('Reuse flags differ: '+str(path))
        for row in report['rows']:
            if row['exit_code'] != 0 or row['source'] in reused:
                raise ValueError('Failed/repeated reuse source')
            reused[row['source']] = row
    env = dict(os.environ)
    env['PYTHONPATH'] = str(prepared/'tools')+os.pathsep+str(Path(__file__).resolve().parents[1] / 'tools')
    # ov79 now owns its complete BSS and relocation table. The common egg
    # helper previously retained a smaller slice of that same island. Keep
    # coin/egg PMFs unchanged, but never link two owners of the ov79 storage.
    common_tool=Path(__file__).resolve().parents[1] / 'tools/common_actor_states.py'
    common_source=common_tool.read_text()
    marker='    # The original egg helper calls ov79'
    if common_source.count(marker)!=1:
        raise ValueError('Common ov79 storage boundary changed')
    common_source=common_source.replace('def generate_batch(root):','def generate_batch(root, include_whomp=True):')
    common_source=common_source.replace(marker,
        '    if not include_whomp:\n'
        '        parts.append("#include <cstring>\\n"+"\\n".join(checks))\n'
        '        return "\\n".join(parts)\n'+marker)
    (output/'common_actor_states_private.py').write_text(common_source)
    env['PYTHONPATH']=str(output)+os.pathsep+env['PYTHONPATH']
    generation = (
        'import json,sys;from pathlib import Path;'
        'import all_level_data,native_actor_overlay_imports,native_overlay_info,common_actor_states_private;'
        'r=Path(sys.argv[1]);p=Path(sys.argv[2]);o=Path(sys.argv[3]);'
        'm=json.loads((p/"tools/native_actor_overlay_manifest.json").read_text());'
        '(o/"all_level_data.cpp").write_text(all_level_data.generate(r));'
        '(o/"native_overlay_info.cpp").write_text(native_overlay_info.generate(r,Path(sys.argv[4])));'
        '(o/"common_actor_states.cpp").write_text(common_actor_states_private.generate_batch(r,False));'
        '[(o/f"native_actor_overlay{b}.cpp").write_text(native_actor_overlay_imports.generate(r,b,m)) '
        'for b in (2,12,13,15,79)]'
    )
    gen_cmd = ['rtk','proxy',shutil.which('python3') or 'python3','-c',generation,
               str(root),str(prepared),str(output),str(rom)]
    process = subprocess.run(gen_cmd,env=env,text=True,capture_output=True)
    (output/'logs/generate.log').write_text(process.stdout+process.stderr)
    result = {'status':'BUILDING_PRIVATE','generation_command':gen_cmd,
              'generation_exit':process.returncode,'rows':[],'replacements':[],
              'baseline':str(baseline),
              'baseline_inputs_sha256':sha(baseline/'inputs.json'),
              'manifest_sha256':sha(prepared/'tools/native_actor_overlay_manifest.json'),
              'common_states_original_tool_sha256':sha(common_tool),
              'common_states_private_tool_sha256':sha(output/'common_actor_states_private.py'),
              'limits':['No original actor runtime or gameplay admission is established by this link.',
                        'Resident bank79 is initially limited to provider15.',
                        'Production CMake, GPU, cadence and user-build are untouched.']}

    def save():
        (output/'report.json').write_text(json.dumps(result,indent=2)+'\n')

    if process.returncode:
        result['status']='GENERATION_FAIL';save();return result
    additions=[]
    def install(obj, suffix=None):
        matches=[row for name,row in by_original.items() if suffix and name.endswith(suffix)]
        if len(matches)>1 or (suffix and not matches):
            raise ValueError('Replacement identity not unique: '+str(suffix))
        if matches:
            prior=matches[0]['snapshot_path']
            if command.count(prior)!=1:
                raise ValueError('Frozen replacement not linked exactly once')
            command[command.index(prior)]=str(obj)
            result['replacements'].append({'previous':prior,'replacement':str(obj)})
        else:
            additions.append(str(obj))

    with tempfile.TemporaryDirectory(prefix='resident-actor-adapters-') as staging:
        for source,expected in preparation['original_tu_sha256'].items():
            if sha(root/source)!=expected or sha(prepared/'original-tu'/source)!=expected:
                raise ValueError('Original frozen source differs: '+source)
            row=reused[source]
            relative,text,stores=generate(root,source,Path(staging))
            digest=hashlib.sha256(text.encode()).hexdigest()
            if digest!=row['generated_sha256'] or sha(Path(row['object']))!=row['object_sha256']:
                raise ValueError('Adapter/object reuse mismatch: '+source)
            obj=Path(row['object']).resolve()
            suffixes=['/'+directory+'/'+str(Path(source).with_suffix('.o'))
                      for directory in ('native-actors','host-src')]
            existing=[suffix for suffix in suffixes if any(name.endswith(suffix) for name in by_original)]
            if len(existing)>1:
                raise ValueError('Original TU has multiple baseline owners: '+source)
            install(obj,existing[0] if existing else None)
            result['rows'].append({'source':source,'generated_sha256':digest,
                'object':str(obj),'object_sha256':sha(obj),'reuse':True,
                'vtable_store_adaptations':stores})
    compile_sources=[
        (output/'all_level_data.cpp','/all_level_data.o'),
        (output/'native_overlay_info.cpp','/native_overlay_info.o'),
        (output/'common_actor_states.cpp','/common_actor_states.o'),
        (prepared/'native_actor_lifecycles.cpp','/native_actor_lifecycles.o'),
        (prepared/'common_scenery_profiles.cpp','/common_scenery_profiles.o'),
        (prepared/'port/unmatched/native_actor_lifecycle.cpp','/unmatched/native_actor_lifecycle.o'),
        *[(output/f'native_actor_overlay{bank}.cpp',
           '/native_actor_overlay2.o' if bank==2 else None) for bank in (2,12,13,15,79)],
        *[(prepared/name,None) for name in ('resident-relocation-prototype.cpp',
                                         'resident_groups.cpp','resident_bindings.cpp')]]
    compiler='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
    flags=[value for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS')
           for value in shlex.split(re.search(r'^'+key+r' = (.*)$',flags_text,re.M)[1])]
    for source,suffix in compile_sources:
        obj=output/(source.stem+'.o')
        argv=['rtk','proxy',compiler,'-I'+str(prepared/'port'),'-I'+str(prepared),
              '-I'+str(Path(__file__).resolve().parents[1] / 'unmatched'),
              '-I'+str(Path(__file__).resolve().parents[1] / 'platform/3ds'),*flags,'-c',str(source),'-o',str(obj)]
        process=subprocess.run(argv,text=True,capture_output=True)
        log=output/'logs'/(source.stem+'.log')
        log.write_text(process.stdout+process.stderr)
        result['rows'].append({'source':str(source),'source_sha256':sha(source),
            'command':argv,'exit_code':process.returncode,'log':str(log),
            'object':str(obj),'object_sha256':sha(obj) if obj.exists() else None})
        print(('PASS ' if process.returncode==0 else 'FAIL ')+source.name,flush=True)
        if process.returncode:
            result['status']='COMPILE_FAIL';save();return result
        install(obj,suffix)
    # Keep all explicit library/archive ordering from the original link. New
    # strong objects are inserted before those libraries, not appended after.
    archive_index=next(i for i,arg in enumerate(command) if arg.endswith('.a'))
    command[archive_index:archive_index]=additions
    output_index=command.index('-o')+1
    command[output_index]=str(output/'resident_actor_closure.elf')
    command += ['-Wl,-Map='+str(output/'resident_actor_closure.map')]
    (output/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
    process=subprocess.run(['rtk','proxy',*command],cwd=baseline,text=True,capture_output=True)
    (output/'logs/link.log').write_text(process.stdout+process.stderr)
    result |= {'link_exit':process.returncode,'status':'LINK_PASS_NOT_EXECUTED' if process.returncode==0 else 'LINK_FAIL',
               'elf_sha256':sha(output/'resident_actor_closure.elf') if process.returncode==0 else None}
    save()
    shutil.copy2(Path(__file__),output/Path(__file__).name)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('--prepared',type=Path,required=True)
    parser.add_argument('--reuse-report',type=Path,action='append',required=True)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,
                        help='Reuse an existing frozen baseline without copying its inputs')
    args=parser.parse_args()
    try:
        report=build(args.root.resolve(),args.prepared.resolve(),
                     [p.resolve() for p in args.reuse_report],args.rom.resolve(),args.output.resolve(),
                     args.baseline.resolve() if args.baseline else None)
        print(report['status'])
    except Exception as error:
        if args.output.exists():
            (args.output/'exception.json').write_text(json.dumps({'status':'BUILD_EXCEPTION',
                'error':str(error)},indent=2)+'\n')
        raise
