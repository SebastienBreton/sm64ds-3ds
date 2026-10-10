#!/usr/bin/env python3
"""Link final-layout arena default/legacy cadence A/B using private objects only."""
import argparse, hashlib, json, re, shlex, shutil, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'
GCC='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy',type=Path,required=True)
    parser.add_argument('--output',type=Path,help='Fresh private build archive; preserves failed attempts')
    args=parser.parse_args()
    copy=args.copy.resolve();out=args.output.resolve() if args.output else copy/'native-cadence'
    assert not out.exists()
    prep=json.loads((copy/'prepare.json').read_text())
    proof=json.loads((copy/'proof/report.json').read_text())
    assert proof['status'].startswith('PASS')
    assert all(sha(copy/n)==h for n,h in prep['files_sha256'].items())
    source=ROOT/'build3ds/validation/gx-retained-cadence/geometry-v2'
    old=json.loads((source/'build.json').read_text())
    assert all(sha(source/n)==h for n,h in old['archive_sha256'].items())
    assert all(sha(p)==h for p,h in old['link_inputs_sha256'].items())
    out.mkdir()
    commands=[]

    def run(cmd,name):
        commands.append(cmd)
        p=subprocess.run(cmd,text=True,capture_output=True)
        (out/(name+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode:
            (out/'failure.json').write_text(json.dumps({'status':'FAIL','step':name,'command':cmd,'exit':p.returncode},indent=2)+'\n')
            raise RuntimeError(f'{name}: exit {p.returncode}; see {out/(name+".log")}')

    code=(source/'game_retained_cadence.cpp').read_text()
    code=code.replace('"gx_direct_pipeline_api.h"','"platform/3ds/player_geometry.h"')
    before=''' ntr::gx_direct_configure(true);
 const bool retained=std::getenv("PLAYER_DIRECT_ARENA_REUSE")!=nullptr;
 player_direct_configure(retained);
 std::printf("Retained cadence indexed=1 geometry_reuse=%u; original rt_run audio IRQ pacing\\n",unsigned(retained));'''
    after=''' player_geometry::startup();
 const bool retained=ntr::gx_direct_enabled() && player_geometry::enabled(std::getenv("PLAYER_DIRECT_ARENA_REUSE"));
 std::printf("Retained cadence indexed=%u geometry_reuse=%u; final production startup; original rt_run audio IRQ pacing\\n",unsigned(ntr::gx_direct_enabled()),unsigned(retained));'''
    assert code.count(before)==1
    code=code.replace(before,after)
    (out/'game_final_cadence.cpp').write_text(code)
    shutil.copy2(source/'before.sav',out/'before.sav')
    flag_path=ROOT/'build3ds/cmake/CMakeFiles/smoke_player.dir/flags.make'
    t=flag_path.read_text();flags=[v for key in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS')
        for v in shlex.split(re.search(r'^'+key+r' = (.*)$',t,re.M)[1])]
    shutil.copy2(flag_path,out/'flags.make')
    common=['-I'+str(copy/'port'),'-I'+str(copy/'port/ntr/include'),'-I'+str(PORT/'platform/3ds')]
    run([GCC,*common,*flags,'-MMD','-MF',str(out/'cadence.d'),'-c',str(out/'game_final_cadence.cpp'),'-o',str(out/'cadence.o')],'compile-cadence')
    # Shared entry owns gfx teardown/stacksize; this is the same tested v2
    # viewer policy, unlike a standalone viewer-only compile.
    run([GCC,*common,*flags,'-DSM64DS_GPU_COMPOSITE_CONFIG',
        '-I'+str(ROOT/'build3ds/cmake/.dkp-generated/presentation'),
        '-I'+str(ROOT/'build3ds/cmake/.dkp-generated/gpu_composite_shaders_bin'),
        '-MMD','-MF',str(out/'viewer.d'),'-c',str(copy/'port/platform/3ds/viewer_3ds.cpp'),'-o',str(out/'viewer.o')],'compile-shared-viewer')
    command=next(c.copy() for c in old['commands'] if '-o' in c and any(str(x).endswith('.elf') for x in c))
    command[command.index('-o')+1]=str(out/'sm64ds_arena_defaults.elf')
    changes={str(source/'game_retained_cadence.o'):str(out/'cadence.o'),
        str(ROOT/'build3ds/validation/gx-direct-arena-pipeline/player_present.o'):str(copy/'proof/player_present.arm.o'),
        str(ROOT/'build3ds/validation/gx-direct-arena-pipeline/pacing-v2/viewer_arena.o'):str(out/'viewer.o'),
        str(ROOT/'build3ds/validation/gx-direct-arena-pipeline/libntr-pipeline.a'):str(copy/'link/libntr-arena.a')}
    for before,after in changes.items():
        assert command.count(before)==(2 if before.endswith('.a') else 1),before
        command=[after if value==before else value for value in command]
    inputs={Path(v) for v in command if v.endswith(('.o','.a','.ld'))}
    inputs_sha={str(p):sha(p) for p in inputs}
    run(command,'link')
    assert inputs_sha=={str(p):sha(p) for p in inputs}
    run(['/opt/devkitpro/tools/bin/3dsxtool',str(out/'sm64ds_arena_defaults.elf'),str(out/'sm64ds_arena_defaults.3dsx')],'3dsx')
    shutil.copy2(Path(__file__),out/Path(__file__).name)
    report={'status':'PASS final-layout native cadence link; PICA and native A/B NOT_EXECUTED',
        'production_modified':False,'binary_sha256':sha(out/'sm64ds_arena_defaults.3dsx'),
        'link_inputs_sha256':inputs_sha,'commands':commands,
        'copy_manifest':str(copy/'prepare.json'),'copy_manifest_sha256':sha(copy/'prepare.json'),
        'host_arm_proof_sha256':sha(copy/'proof/report.json'),
        'viewer_object_sha256':sha(out/'viewer.o'),'viewer_policy':'PLAYER_SHARED_GFX; entry owns stacksize/gfx teardown; no lifetime counters',
        'save':old['save'],'modes':{'current':'PLAYER_DIRECT_ARENA=0: exact legacy output/presenter; retained off',
            'candidate':'absent geometry knobs: final startup defaults arena+held geometry retained on'},
        'pacing':'Original SYNCDRAW; NO_VBLANK_WAIT absent in planned paired gate',
        'composition_cache':'Absent; resident and OBJ/capture/PPU work evaluated every presentation',
        'limits':['Final-layout PICA gate and normal pacing actual-loop pair required before shared build',
            'Original scene3 route objects frozen before future camera adapter integration',
            'No Bob entry or full-game claim']}
    report['archive_sha256']={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p.name!='build.json'}
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['status']);print(report['binary_sha256'])


if __name__=='__main__':main()
