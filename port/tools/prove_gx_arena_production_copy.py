#!/usr/bin/env python3
"""Prove actual production-layout GX/arena copies without a shared build."""
import argparse, hashlib, json, re, shlex, shutil, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'
GCC='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy',type=Path,required=True)
    parser.add_argument('--resume-arm',action='store_true',help='Resume after successful host gates; preserve initial failed ARM diagnostics')
    args=parser.parse_args()
    copy=args.copy.resolve();out=copy/'proof'
    assert not out.exists() or args.resume_arm,'Keep previous proof attempts'
    out.mkdir(exist_ok=args.resume_arm)
    prep=json.loads((copy/'prepare.json').read_text())
    assert all(sha(copy/n)==h for n,h in prep['files_sha256'].items())
    assert all(sha(p)==h for p,h in prep['baseline_sha256'].items())
    original=ROOT/'build3ds/validation/gx-direct-arena-host'
    pipeline=ROOT/'build3ds/validation/gx-direct-arena-pipeline'
    old=json.loads((original/'report.json').read_text())
    assert all(sha(original/n)==h for n,h in old['archive_sha256'].items())
    commands=json.loads((out/'commands.json').read_text()) if args.resume_arm else []
    if args.resume_arm:
        assert not (out/'report.json').exists()
        shutil.copy2(out/'failure.json',out/'initial-failure.json')
        shutil.copy2(out/'arm-viewer_3ds.log',out/'arm-viewer_3ds-initial-failure.log')

    def run(command,name):
        commands.append(command)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        p=subprocess.run(command,text=True,capture_output=True)
        (out/(name+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode:
            (out/'failure.json').write_text(json.dumps({'status':'FAIL','step':name,'command':command,'exit':p.returncode},indent=2)+'\n')
        p.check_returncode()
        return p.stdout.strip()

    def names(code):
        code=code.replace('gx_arena_candidate','gx_geometry').replace('ReleaseBuilder','Builder')
        for before,after in {
            '"gx_geometry_arena_candidate.h"':'"platform/3ds/gx_geometry_arena.h"',
            '"gx_arena_release_builder.h"':'"platform/3ds/gx_geometry_arena.h"',
            '"gx_arena_resources_candidate.h"':'"platform/3ds/gx_geometry_resources.h"',
            '"gx_direct_pipeline_api.h"':'"ntr/gx_direct.h"',
        }.items():code=code.replace(before,after)
        return code

    for name in ('gx_geometry_capture_test_api.h','test_gx_geometry_arena.cpp','test_gx_geometry_capture.cpp'):
        (out/name).write_text(names((pipeline/name).read_text()))
    for name in ('test_gx_arena_resources.cpp','test_gx_arena_allocator.cpp'):
        (out/name).write_text(names((PORT/'tests'/name).read_text()))
    public=set(re.findall(r'\b(gx_\w+)\s*\(',(PORT/'ntr/include/ntr/gx.h').read_text()))|{'gx_stream_note'}
    observer=(original/'gx-candidate.cpp').read_text()
    observer=observer[observer.index('void gx_test_state_candidate('):observer.rfind('}  // namespace ntr')]
    code=(copy/'port/ntr/gx.cpp').read_text()
    code=code.replace('}  // namespace ntr',observer+
        '\nconst gx_geometry::Packet& gx_test_packet(){return gx_direct_packet();}\n}  // namespace ntr')
    code=''.join('#define '+n+' '+n+'_candidate\n' for n in sorted(public))+code
    (out/'gx-observed.cpp').write_text(code)
    common=['-I'+str(copy/'port'),'-I'+str(copy/'port/ntr/include'),'-I'+str(out),
        '-I'+str(PORT),'-I'+str(PORT/'ntr/include'),'-I'+str(PORT/'platform/3ds')]
    host=['-std=c++17','-O2','-g','-D__3DS__',*common,'-fsanitize=address,undefined','-fno-sanitize-recover=all']
    if not args.resume_arm:
        run(['clang++',*host,'-fno-sanitize=shift-base','-MMD','-MF',str(out/'gx-host.d'),'-c',str(out/'gx-observed.cpp'),'-o',str(out/'gx-host.o')],'host-gx')
    objs=[original/'baseline.host.o',out/'gx-host.o',original/'texture.host.o',original/'vram_bus.host.o',original/'vram_banks.host.o']
    binary=out/'test-corpus'
    if not args.resume_arm:
        run(['clang++',*host,*map(str,objs),str(out/'test_gx_geometry_capture.cpp'),'-o',str(binary)],'host-link')
    results={}
    for mode in ('0','1'):
        results['corpus_mode'+mode]=(out/('corpus-mode'+mode+'.log')).read_text().strip() if args.resume_arm else run([str(binary),str(original/'retail-bmd-lists.bin'),mode],'corpus-mode'+mode)
    for name in ('test_gx_geometry_arena','test_gx_arena_resources','test_gx_arena_allocator'):
        binary=out/name
        if not args.resume_arm:
            run(['clang++',*host,str(out/(name+'.cpp')),'-o',str(binary)],'compile-'+name)
        results[name]=(out/('run-'+name+'.log')).read_text().strip() if args.resume_arm else run([str(binary)],'run-'+name)
    startup=out/'test-startup.cpp'
    startup.write_text('''#include "platform/3ds/player_geometry.h"
#include <cstdio>
static bool arena=false,reuse=false;
namespace ntr {void gx_direct_configure(bool value){arena=value;}}
void player_direct_configure(bool value){reuse=value;}
int main(){
    const char* values[]={nullptr,"0","1"};
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){
        if(values[a])setenv("PLAYER_DIRECT_ARENA",values[a],1);else unsetenv("PLAYER_DIRECT_ARENA");
        if(values[b])setenv("PLAYER_DIRECT_ARENA_REUSE",values[b],1);else unsetenv("PLAYER_DIRECT_ARENA_REUSE");
        player_geometry::startup();
        const bool expected=a!=1,held=expected && b!=1;
        if(arena!=expected || reuse!=held)return 1;
    }
    std::puts("PASS startup absent/0/1 in 9 combinations; arena off disables retained");
}
''')
    if not args.resume_arm:
        run(['clang++',*host,str(startup),'-o',str(out/'test-startup')],'compile-startup')
    results['startup']=(out/'run-startup.log').read_text().strip() if args.resume_arm else run([str(out/'test-startup')],'run-startup')
    assert all(value.startswith('PASS') for value in results.values())

    def flags(target):
        p=ROOT/f'build3ds/cmake/CMakeFiles/{target}.dir/flags.make'
        shutil.copy2(p,out/(target+'-flags.make'))
        t=p.read_text()
        return [v for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS') for v in shlex.split(re.search(r'^'+key+r' = (.*)$',t,re.M)[1])]

    arm_sources=[('ntr/gx.cpp','ntr'),('platform/3ds/player_present.cpp','smoke_player'),
        ('platform/3ds/viewer_3ds.cpp','sm64ds_viewer'),('platform/3ds/game_main.cpp','smoke_player')]
    for name,target in arm_sources:
        path=copy/'port'/name
        extra=['-DSM64DS_GPU_COMPOSITE_CONFIG','-I'+str(ROOT/'build3ds/cmake/.dkp-generated/presentation'),
            '-I'+str(ROOT/'build3ds/cmake/.dkp-generated/gpu_composite_shaders_bin')]
        run([GCC,*common,*flags(target),*extra,'-MMD','-MF',str(out/(path.stem+'.arm.d')),'-c',str(path),'-o',str(out/(path.stem+'.arm.o'))],'arm-'+path.stem)
    for name in ('test_gx_geometry_capture.cpp','test_gx_geometry_arena.cpp','test_gx_arena_resources.cpp','test_gx_arena_allocator.cpp','test-startup.cpp'):
        run([GCC,*common,*flags('ntr'),'-c',str(out/name),'-o',str(out/(name+'.arm.o'))],'arm-'+name)
    deps={}
    for p in out.glob('*.d'):
        for token in shlex.split(p.read_text().replace('\\\n',' ').split(':',1)[1]):
            q=Path(token)
            if q.is_file():deps[str(q.resolve())]=sha(q)
    assert all(sha(copy/n)==h for n,h in prep['files_sha256'].items())
    assert all(sha(p)==h for p,h in prep['baseline_sha256'].items())
    report={'status':'PASS actual production-layout corpus/geometry/resources/startup HostSAN and ARM11 compile; native final-layout runtime NOT_EXECUTED',
        'production_modified':False,'results':results,'commands':commands,'dependencies_sha256':deps,
        'actual_copy_sha256':prep['files_sha256'],'baseline_sha256':prep['baseline_sha256'],
        'corpus_lists':old['retail_bmd_lists'],'corpus_words':old['retail_bmd_words'],
        'sanitizers':'ASan+UBSan; original VTX_10 negative signed shift-base exclusion retained for GX only',
        'unchanged_independent_oracles':'Archived diffuse/specular/texgen bodies and original GX math unchanged; not repeated by this mechanical split',
        'limits':['Final-layout PICA consumer gate and same-binary cadence A/B pending',
            'No shared rebuild or production integration','No physical hardware or full-game claim']}
    shutil.copy2(Path(__file__),out/Path(__file__).name)
    report['archive_sha256']={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p.name!='report.json'}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['status'])
    for name,value in results.items():print(name+': '+value)


if __name__=='__main__':
    main()
