#!/usr/bin/env python3
"""Run one archived GX diagnostic mode, only with an exclusive emulator slot.

Each invocation delegates all emulator/model/config handling to build3ds/run.py,
archives/restores the private benchmark save, and checks exact baseline BMPs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
ARCHIVE=ROOT/'build3ds/validation/gx-architecture-diagnostic'
BASE=ROOT/'build3ds/validation/architecture-integrated-baseline/menu-repeat/current'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mode',type=int,choices=[-1,0,1,2,3,4],required=True)
parser.add_argument('--rate',type=int,default=64)
parser.add_argument('--offset',type=int,default=0)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert args.rate>0 and not args.rate&(args.rate-1) and args.rate<=4096
assert 0<=args.offset<args.rate
assert not (out/'run.json').exists(),'Do not overwrite a previous run'


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


build=json.loads((ARCHIVE/'build.json').read_text())
for relative,digest in build['archive_sha256'].items():
    assert sha(ARCHIVE/relative)==digest,relative
base_run=json.loads((BASE/'run.json').read_text())
assert base_run['result']=='PASS' and base_run['unmapped_access_count']==0
binary=ARCHIVE/'fps_menu_gx_architecture_diagnostic.3dsx'
sd=Path.home()/'Library/Application Support/Azahar/sdmc/sm64ds'
save=sd/'fps-menu-validation.sav'
before=save.read_bytes() if save.exists() else None
if before is not None:(out/'save-original.sav').write_bytes(before)
fixture={'path':str(save),'original_present':before is not None,
    'original_sha256':hashlib.sha256(before).hexdigest() if before is not None else None,
    'start_fixture':'absent, matching frozen real-menu baseline'}
(out/'save-fixture.json').write_text(json.dumps(fixture,indent=2)+'\n')
shutil.copy2(Path(__file__),out/Path(__file__).name)
environment=[e for e in base_run['environment_overrides'] if not e.startswith('SM64DS_REQUIRE_3DS_MODEL=')]
environment += [f'PLAYER_GX_DIAGNOSTIC_MODE={args.mode}',f'PLAYER_GX_SAMPLE_RATE={args.rate}',f'PLAYER_GX_SAMPLE_OFFSET={args.offset}']
command=[sys.executable,str(ROOT/'build3ds/run.py'),str(binary),'--timeout','180','--3ds-model','old','--output',str(out)]
for item in environment:command+=['--env',item]
print('Exclusive Azahar diagnostic mode',args.mode,'rate',args.rate,'offset',args.offset,flush=True)
try:
    save.unlink(missing_ok=True)
    result=subprocess.run(command,capture_output=True,text=True)
    (out/'runner-output.log').write_text(result.stdout+result.stderr)
    if save.exists():shutil.copy2(save,out/'save-generated.sav')
finally:
    if before is None:save.unlink(missing_ok=True)
    else:save.write_bytes(before)
    fixture['restored']=(save.read_bytes() if save.exists() else None)==before
    (out/'save-fixture.json').write_text(json.dumps(fixture,indent=2)+'\n')
result.check_returncode()
run=json.loads((out/'run.json').read_text())
assert run['result']=='PASS' and run['unmapped_access_count']==0
assert run['emulator_log_fresh'] and run['temporary_settings_restored'] and fixture['restored']
assert run['requested_3ds_model']=='old' and not run['model_verification_failure']
assert run['sha256']==sha(binary)
images={}
for p in BASE.glob('*.bmp'):
    assert (out/p.name).exists(),p.name
    images[p.name]={'baseline':sha(p),'diagnostic':sha(out/p.name)}
    assert images[p.name]['baseline']==images[p.name]['diagnostic'],p.name
assert len(images)==6,len(images)
assert 'GX diagnostic frames=120 ' in (out/'log.txt').read_text()
status={'status':'PASS native classic, zero Unmapped, six exact baseline BMPs, save/config restored',
    'mode':args.mode,'rate':args.rate,'offset':args.offset,'commands':command,
    'binary_sha256':sha(binary),'images_sha256':images,'save':fixture,
    'measurement_scope':'Diagnostic estimates include instrumentation overhead; no optimization throughput claim.'}
(out/'validation.json').write_text(json.dumps(status,indent=2)+'\n')
print(status['status'])
for line in (out/'log.txt').read_text().splitlines():
    if line.startswith(('GX diagnostic','Menu game phase measured120 ms/frame: Render','Menu FPS measured120')):print(line)
