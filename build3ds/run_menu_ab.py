#!/usr/bin/env python3
"""Run a frozen same-binary menu A/B after acquiring exclusive emulator ownership.

Both runs start with an absent validation save, and restore the pre-test save.
run.py owns emulator termination/model checks/settings restoration per run.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import signal
import subprocess
import sys
from PIL import Image
from runner_journal import save as save_journal, recover

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--output', type=Path, required=True)
comparison = parser.add_mutually_exclusive_group(required=True)
comparison.add_argument('--candidate-env')
comparison.add_argument('--repeat-baseline', action='store_true',
    help='Repeat the same configuration to measure baseline variation')
parser.add_argument('--env', action='append', default=[])
args = parser.parse_args()
binary, output = args.binary.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
validation_save = Path.home()/'Library/Application Support/Azahar/sdmc/sm64ds/fps-menu-validation.sav'
journal = output/'validation-save-restore.json'
recover(journal, [validation_save])
original = validation_save.read_bytes() if validation_save.exists() else None
save_journal(journal, {validation_save: original})
(output/'save-fixture.json').write_text(json.dumps({
    'path': str(validation_save), 'start_fixture': 'absent before both runs',
    'original_present': original is not None,
    'original_sha256': hashlib.sha256(original).hexdigest() if original is not None else None}, indent=2)+'\n')
child = None


def interrupted(signum, frame):
    raise KeyboardInterrupt(f'Signal {signum}')


signal.signal(signal.SIGTERM, interrupted)
records = []
try:
    for name in ('current', 'candidate'):
        validation_save.unlink(missing_ok=True)
        destination = output/name
        command = [sys.executable, str(ROOT/'build3ds/run.py'), str(binary),
            '--3ds-model', 'old', '--timeout', '180', '--output', str(destination)]
        for setting in args.env + ([args.candidate_env] if name == 'candidate' and args.candidate_env else []):
            command += ['--env', setting]
        print('Running same-binary menu '+name, flush=True)
        with (output/(name+'-runner.log')).open('wb') as log:
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            code = child.wait()
            child = None
        if code:
            raise RuntimeError(f'{name}: runner exit {code}; see preserved log')
        run = json.loads((destination/'run.json').read_text())
        assert run['result'] == 'PASS' and run['unmapped_access_count'] == 0
        assert run['temporary_settings_restored'] and run['requested_3ds_model'] == 'old'
        log = (destination/'log.txt').read_text()
        assert 'new3ds=0' in log and 'frame=959 state=0 substate=0' in log
        match = re.search(r'Menu FPS measured120 ms/frame: game=([\d.]+) audio=([\d.]+) '
            r'presentation=([\d.]+) total=([\d.]+) fps=([\d.]+)', log)
        assert match
        records.append({'mode': name, 'run': run,
            'timings': dict(zip(('game_ms','audio_ms','presentation_ms','total_ms','updates_per_second'),
                map(float, match.groups()))),
            'phases': {key: float(value) for key,value in re.findall(
                r'Menu game phase measured120 ms/frame: (.+)=([\d.]+)', log)},
            'workload': re.search(r'Menu game workloads: (.+)', log)[1]})
        print('PASS '+name+': fresh classic, zero unmapped accesses', flush=True)
finally:
    if child is not None:
        child.terminate()
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
    recover(journal, [validation_save])
    assert (validation_save.read_bytes() if validation_save.exists() else None) == original
    print('Validation save restored', flush=True)

assert records[0]['run']['sha256'] == records[1]['run']['sha256']
assert records[0]['workload'] == records[1]['workload']
images = []
for before in sorted((output/'current').glob('*.bmp')):
    after = output/'candidate'/before.name
    left, right = Image.open(before).convert('RGB'), Image.open(after).convert('RGB')
    assert left.size == right.size
    images.append({'image': before.name, 'size': left.size,
        'different_rgb_pixels': sum(a != b for a,b in zip(left.get_flattened_data(),right.get_flattened_data())),
        'byte_identical': before.read_bytes() == after.read_bytes(),
        'current_sha256': hashlib.sha256(before.read_bytes()).hexdigest(),
        'candidate_sha256': hashlib.sha256(after.read_bytes()).hexdigest()})
assert len(images) == 6
correct = all(item['different_rgb_pixels'] == 0 for item in images)
before, after = [record['timings'] for record in records]
report = {'status': 'PASS captured correctness' if correct else 'FAIL captured correctness',
    'comparison_kind': 'repeated baseline' if args.repeat_baseline else 'runtime policy A/B',
    'records': records, 'images': images,
    'elapsed_ms_saved': before['total_ms']-after['total_ms'],
    'throughput_change_percent': 100*(after['updates_per_second']/before['updates_per_second']-1),
    'scope': 'Same frozen binary, 120 original menu updates in Azahar classic; no physical hardware or full-game claim'}
(output/'runtime-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(f"Menu elapsed: {before['total_ms']:.3f} -> {after['total_ms']:.3f} ms; "
      f"rate: {before['updates_per_second']:.3f} -> {after['updates_per_second']:.3f}; images={correct}")
if not correct:
    raise SystemExit(1)
