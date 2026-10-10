#!/usr/bin/env python3
"""Install the exact proved private arena layout after all final native gates.

No configure/build, renderer arithmetic, matched source or PPU changes here.
"""
import argparse, hashlib, json, shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--pica',type=Path,required=True)
    args=parser.parse_args()
    copy=args.copy.resolve();report_path=copy/'integration.json'
    assert not report_path.exists(),'Preserve integration provenance'
    prep=json.loads((copy/'prepare.json').read_text())
    proof=json.loads((copy/'proof/report.json').read_text())
    run=json.loads(args.runtime.read_text());pica=json.loads(args.pica.read_text())
    assert proof['status'].startswith('PASS actual production-layout')
    assert run['correctness_status']=='PASS' and run['performance_status']=='IMPROVEMENT'
    assert run['comparison']=='Final production arena+held geometry defaults versus exact legacy'
    assert pica['result']=='PASS' and pica['unmapped_access_count']==0
    assert pica['emulator_log_fresh'] and pica['temporary_settings_restored']
    assert pica['sha256']=='79aae7025625e41967ebce1e11adc49689ea8a1ccf7bd6383a48e3620f6d3f89'
    assert run['binary_sha256']=='6368193388dcf3c428eedc1e06eb26cd638b4da4df79bde206ebc5d3a78432ad'
    assert all(sha(copy/n)==h for n,h in prep['files_sha256'].items())
    assert proof['actual_copy_sha256']==prep['files_sha256']
    assert all(sha(p)==h for p,h in prep['baseline_sha256'].items())
    assert sha(PORT/'platform/3ds/game_main.cpp')==prep['game_main_baseline_sha256']
    for record in run['records']:
        native=record['run']
        assert native['result']=='PASS' and native['unmapped_access_count']==0
        assert native['requested_3ds_model']=='old' and not native['model_verification_failure']
        assert native['temporary_settings_restored'] and native['emulator_log_fresh']
        assert not any(e.startswith('PLAYER_RESIDENT_NO_VBLANK_WAIT=') for e in native['environment_overrides'])
    assert run['save']['restored']
    assert all(v['current_sha256']==v['candidate_sha256'] for v in run['images'].values())
    assert len(run['images'])>=6
    files={PORT/Path(n).relative_to('port'):copy/n for n in prep['files_sha256']}
    tests=('gx_geometry_capture_test_api.h','test_gx_geometry_capture.cpp',
        'test_gx_geometry_arena.cpp','test_gx_arena_resources.cpp','test_gx_arena_allocator.cpp')
    for name in tests:
        source=copy/'proof'/name
        assert sha(source)==proof['archive_sha256'][name]
        files[PORT/'tests'/name]=source
    files[PORT/'tests/test_player_geometry_startup.cpp']=copy/'proof/test-startup.cpp'
    assert sha(files[PORT/'tests/test_player_geometry_startup.cpp'])==proof['archive_sha256']['test-startup.cpp']
    before={str(p):sha(p) if p.exists() else None for p in files}
    backup=copy/'production-before'
    for destination in files:
        if destination.exists():
            path=backup/destination.relative_to(PORT);path.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(destination,path)
    for destination,source in files.items():
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)
        assert sha(destination)==sha(source)
    result={'status':'PASS exact proved arena layout installed; common rebuild pending',
        'copy_sha256':sha(copy/'prepare.json'),'proof_sha256':sha(copy/'proof/report.json'),
        'native_runtime_sha256':sha(args.runtime),'pica_run_sha256':sha(args.pica),
        'before_sha256':before,'installed_sha256':{str(p):sha(p) for p in files},
        'startup':'Absent/1 arena and held geometry enabled; literal0 disabled before first GX frame',
        'excluded':['Full composition reuse','PPU/OBJ epochs','Pacing changes','GX list IR',
            'Original src/include mutations','Shared CMake/build mutations'],
        'limits':['Measured one-pair old-model emulation, not physical hardware or full-game coverage',
            'Parent performs one coordinated common rebuild with independent camera adapter']}
    report_path.write_text(json.dumps(result,indent=2)+'\n')
    print(result['status']);print('Installed',len(files),'files; no build launched')


if __name__=='__main__':main()
