#!/usr/bin/env python3
"""Freeze a functional Stage probe of the private resident actor closure.

Uses the validated direct Stage entry, original destination/save routines and
the unmodified gameplay scheduler. Cold function counters identify exercised
original TUs; this binary is never performance evidence.
"""
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
from pathlib import Path


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def build(root,prepared,closure,entry,output):
    if output.exists():raise ValueError('Preserve previous probe builds')
    output.mkdir(parents=True);(output/'objects').mkdir();(output/'logs').mkdir()
    report=json.loads((closure/'report.json').read_text())
    assert report['status']=='LINK_PASS_NOT_EXECUTED'
    command=json.loads((closure/'link-command.json').read_text())
    flags_text=(prepared/'baseline/flags.make').read_text()
    def flags(language):
        return [v for k in (language+'_DEFINES',language+'_INCLUDES',language+'_FLAGS')
                for v in shlex.split(re.search('^'+k+' = (.*)$',flags_text,re.M)[1])]
    rows=[];observed={}
    for row in report['rows']:
        if not row.get('reuse'):continue
        obj=Path(row['object']);assert sha(obj)==row['object_sha256']
        # The archived generated source is beside its archived object tree.
        archive=obj.parents[len(Path(row['source']).parts)]
        relative=Path(row['source']).with_suffix('.c' if obj.parent.name=='src' and Path(row['source']).suffix=='.c' else '.cpp')
        source=archive/'generated'/Path(row['source'])
        if not source.exists():source=source.with_suffix('.cpp')
        assert sha(source)==row['generated_sha256'],source
        language='C' if source.suffix=='.c' else 'CXX'
        compiler='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'+('gcc' if language=='C' else 'g++')
        target=output/'objects'/Path(row['source']).with_suffix('.o');target.parent.mkdir(parents=True,exist_ok=True)
        argv=['rtk','proxy',compiler,*flags(language),'-I'+str(Path(__file__).resolve().parents[1] / 'platform/3ds'),
              '-finstrument-functions','-finstrument-functions-exclude-file-list=/include/,/opt/devkitpro/',
              '-c',str(source),'-o',str(target)]
        proc=subprocess.run(argv,text=True,capture_output=True)
        log=output/'logs'/Path(row['source']).with_suffix('.log');log.parent.mkdir(parents=True,exist_ok=True);log.write_text(proc.stdout+proc.stderr)
        if proc.returncode:raise RuntimeError('Instrumented original compile failed: '+str(log))
        assert command.count(str(obj))==1
        command[command.index(str(obj))]=str(target)
        symbols=subprocess.check_output(['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g','--defined-only',str(target)],text=True)
        for at,kind,name in re.findall(r'^([0-9a-f]+) ([TW]) (\S+)$',symbols,re.M):
            if kind=='T' and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',name):
                if name in observed:raise ValueError('Repeated strong body identity: '+name)
                observed[name]=row['source']
        rows.append({'original_source':row['source'],'generated':str(source),'generated_sha256':sha(source),
                     'command':argv,'object':str(target),'object_sha256':sha(target),'log':str(log)})
    counter=['#include <cstdint>','#include <cstdio>','struct ObservedBody {void*address;const char*name;const char*source;unsigned calls;};',
             'extern "C" {']
    for index,name in enumerate(observed):counter.append(f'extern unsigned char observed_body_{index}[] asm("{name}");')
    counter+=['}','static ObservedBody observed_bodies[]={']
    for index,(name,source) in enumerate(observed.items()):counter.append(f'{{observed_body_{index},"{name}","{source}",0}},')
    counter+=['};','extern "C" void __cyg_profile_func_enter(void*fn,void*) {',
              ' for(auto&b:observed_bodies)if(b.address==fn){if(!b.calls)std::printf("BODY_FIRST symbol=%s source=%s\\n",b.name,b.source);++b.calls;return;}','}',
              'extern "C" void __cyg_profile_func_exit(void*,void*){}',
              'extern "C" void report_exercised_bodies(){unsigned exercised=0;for(const auto&b:observed_bodies){',
              'std::printf("BODY_COUNT symbol=%s calls=%u source=%s\\n",b.name,b.calls,b.source);exercised+=b.calls!=0;}',
              'std::printf("BODY_SUMMARY exercised=%u linked=%u\\n",exercised,unsigned(sizeof(observed_bodies)/sizeof(observed_bodies[0])));}','']
    (output/'body_counters.cpp').write_text('\n'.join(counter))
    text=entry.read_text()
    changes={
        ' REQUIRE(check_bob_lifecycle()==0);':' // Existing native bank cycles are a separate proof; Stage owns this gate\'s loads.',
        'sm64ds_check_common_profiles()==39':'sm64ds_check_common_profiles()==46',
        'COMMON_PHASE stage=39 scope=2':'COMMON_PHASE stage=46 scope=2',
        'LoadLevelNoReturn(6,2,1,0)':'LoadLevelNoReturn(target_level,0,1,0)',
        'data_02092124[0]=6':'data_02092124[0]=target_level',
        'REQUIRE(data_0209f2f8==6)':'REQUIRE(data_0209f2f8==target_level)',
        ' REQUIRE(total>10);':' REQUIRE(total>0);',
    }
    for old,new in changes.items():
        if old not in text:raise ValueError('Direct entry adapter site changed: '+old)
        text=text.replace(old,new)
    at=' PORT_INSTALL_FAULT_PROBE();setvbuf(stdout,nullptr,_IONBF,0);'
    text=text.replace(at,at+'\n const int target_level=std::getenv("RESIDENT_STAGE_LEVEL")?std::atoi(std::getenv("RESIDENT_STAGE_LEVEL")):7;\n REQUIRE(target_level==4 || target_level==5 || target_level==7);')
    old=' const auto* level=static_cast<const unsigned char*>(sm64ds_resolve_level_address(14,0x02113434,16));REQUIRE(level);\n REQUIRE(*reinterpret_cast<const unsigned short*>(level+8)==1765);\n REQUIRE(*reinterpret_cast<const unsigned short*>(level+10)==1763);'
    if old not in text:raise ValueError('Old Bob level record assertion changed')
    text=text.replace(old,' // Level identity is supplied by the original loader; no Bob-specific record is assumed.')
    text=text.replace(' std::printf("stage6_direct: PASS frames=%u level=6 actors=%u pending=%u dying=%u; direct init only, no natural-transition claim\\n",frames,total,pending,dying);',
        ' report_exercised_bodies();\n std::printf("resident_stage_direct: PASS frames=%u level=%d actors=%u pending=%u dying=%u; direct init only, no natural-transition claim\\n",frames,target_level,total,pending,dying);')
    text='extern "C" void report_exercised_bodies();\n'+text
    (output/'test_resident_stage.cpp').write_text(text)
    for name in ('body_counters.cpp','test_resident_stage.cpp'):
        source=output/name;obj=source.with_suffix('.o')
        argv=['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags('CXX'),
              '-I'+str(Path(__file__).resolve().parents[1] / 'platform/3ds'),'-I'+str(Path(__file__).resolve().parents[1] / 'tests'),'-c',str(source),'-o',str(obj)]
        proc=subprocess.run(argv,text=True,capture_output=True);(output/'logs'/(source.stem+'.log')).write_text(proc.stdout+proc.stderr)
        if proc.returncode:raise RuntimeError('Entry compile failed: '+name)
    prior=next(row['snapshot_path'] for row in json.loads((prepared/'baseline/inputs.json').read_text())
               if row['original_path'].endswith('/tests/smoke_player.o'))
    assert command.count(prior)==1;command[command.index(prior)]=str(output/'test_resident_stage.o')
    command.insert(next(i for i,x in enumerate(command) if x.endswith('.a')),str(output/'body_counters.o'))
    command[command.index('-o')+1]=str(output/'test_resident_stage.elf')
    command=[x for x in command if not x.startswith('-Wl,-Map=')]
    (output/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
    proc=subprocess.run(['rtk','proxy',*command],text=True,capture_output=True);(output/'logs/link.log').write_text(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError('Stage probe link failed')
    package=['rtk','proxy','/opt/devkitpro/tools/bin/3dsxtool',str(output/'test_resident_stage.elf'),str(output/'test_resident_stage.3dsx')]
    proc=subprocess.run(package,text=True,capture_output=True);(output/'logs/package.log').write_text(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError('3DSX package failed')
    result={'status':'BUILD_PASS_NOT_EXECUTED','instrumented_original_tus':rows,'observed_body_sources':observed,
        'entry_original_sha256':sha(entry),'entry_source_sha256':sha(output/'test_resident_stage.cpp'),
        'counter_source_sha256':sha(output/'body_counters.cpp'),
        'elf_sha256':sha(output/'test_resident_stage.elf'),'binary_sha256':sha(output/'test_resident_stage.3dsx'),
        'limits':['Diagnostic counters change CPU cost; functional evidence only.',
                  'Direct original Stage construction is not natural transition proof.',
                  'Only functions with recorded calls are exercised; others remain unexecuted.']}
    (output/'build.json').write_text(json.dumps(result,indent=2)+'\n');shutil.copy2(Path(__file__),output/Path(__file__).name)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('--prepared',type=Path,required=True)
    p.add_argument('--closure',type=Path,required=True);p.add_argument('--entry',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    try:
        result=build(a.root.resolve(),a.prepared.resolve(),a.closure.resolve(),a.entry.resolve(),a.output.resolve());print(result['status'])
    except Exception as error:
        if a.output.exists():(a.output/'exception.json').write_text(json.dumps({'status':'BUILD_EXCEPTION','error':str(error)},indent=2)+'\n')
        raise
