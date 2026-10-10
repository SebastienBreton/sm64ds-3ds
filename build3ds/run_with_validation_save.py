#!/usr/bin/env python3
"""Run an owned fixture with a specified save, restoring it after emulator exit.

Caller must hold the team's exclusive emulator slot. run.py handles model,
emulator lifetime and configuration restoration. This wrapper owns only the
named validation-save fixture, including recovery after interrupted runs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import signal
import subprocess
import sys
from runner_journal import save, recover

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--save-name',choices=['architecture-castle-validation.sav'],required=True)
parser.add_argument('--env',action='append',default=[])
parser.add_argument('--timeout',type=int,default=180)
parser.add_argument('--movie-play',type=Path,help='Recorded original HID input passed to the emulator runner')
parser.add_argument('--input-save',type=Path,
                    help='Read-only starting save; omitted means an absent save')
args=parser.parse_args()
output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
fixture=Path.home()/'Library/Application Support/Azahar/sdmc/sm64ds'/args.save_name
journal=output/'validation-save-restore.json'
recover(journal,[fixture])
original=fixture.read_bytes() if fixture.exists() else None
initial=args.input_save.resolve().read_bytes() if args.input_save else None
save(journal,{fixture:original})
child=None


def interrupted(signum,frame):
    raise KeyboardInterrupt(f'Signal {signum}')


signal.signal(signal.SIGTERM,interrupted)
try:
    fixture.unlink(missing_ok=True)
    if initial is not None:
        fixture.write_bytes(initial)
    command=[sys.executable,str(ROOT/'build3ds/run.py'),str(args.binary.resolve()),
        '--3ds-model','old','--timeout',str(args.timeout),'--output',str(output/'run'),
        '--env','SM64DS_SAVE_PATH=sdmc:/sm64ds/'+args.save_name]
    for setting in args.env:
        command+=['--env',setting]
    if args.movie_play:
        command+=['--movie-play',str(args.movie_play.resolve(strict=True))]
    with (output/'runner.log').open('wb') as log:
        child=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
        result=child.wait();child=None
finally:
    if child is not None:
        child.terminate()
        try:child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill();child.wait()
    final=fixture.read_bytes() if fixture.exists() else None
    if final is not None:
        (output/'after.sav').write_bytes(final)
    recover(journal,[fixture])
    restored=fixture.read_bytes() if fixture.exists() else None
    assert restored==original
    (output/'save-fixture.json').write_text(json.dumps({
        'path':str(fixture),'initial_fixture':str(args.input_save.resolve()) if args.input_save else 'absent',
        'initial_sha256':hashlib.sha256(initial).hexdigest() if initial is not None else None,
        'final_sha256':hashlib.sha256(final).hexdigest() if final is not None else None,
        'original_present':original is not None,'restored':True,
        'original_sha256':hashlib.sha256(original).hexdigest() if original is not None else None},indent=2)+'\n')
    print('Validation save restored',flush=True)
if result:raise SystemExit(result)
manifest=json.loads((output/'run/run.json').read_text())
assert manifest['result']=='PASS' and manifest['unmapped_access_count']==0
assert manifest['temporary_settings_restored'] and manifest['requested_3ds_model']=='old'
print('PASS classic fixture; zero unmapped accesses; configuration restored')
