#!/usr/bin/env python3
"""One frozen same-binary direct-arena A/B after exclusive Azahar handoff.

The independent PICA pacing gate is required. run.py kills between/after runs,
checks classic hardware and fresh unmapped accesses, and restores config.
"""
from pathlib import Path
import argparse, hashlib, json, re, shutil, signal, subprocess, sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'build3ds'))
from runner_journal import save as save_journal, recover

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive', type=Path, default=ROOT/'build3ds/validation/gx-direct-arena-pipeline/pacing-v2')
parser.add_argument('--pica-run', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
archive, output = args.archive.resolve(), args.output.resolve()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


assert not output.exists(), 'Never overwrite a previous A/B'
build = json.loads((archive/'build.json').read_text())
for relative, digest in build['archive_sha256'].items():
    assert sha(archive/relative) == digest, relative
producer = Path(build['producer_proof'])
assert sha(producer) == build['producer_proof_sha256']
for absolute, digest in build['link_inputs_sha256'].items():
    assert sha(absolute) == digest, 'Frozen link input changed: '+absolute
pica = json.loads((args.pica_run/'run.json').read_text())
probe = json.loads((ROOT/'build3ds/validation/gx-direct-arena-viewer/artifact-v2/probe-build.json').read_text())
assert pica['result'] == 'PASS' and pica['unmapped_access_count'] == 0
assert pica['emulator_log_fresh'] and pica['temporary_settings_restored']
assert pica['requested_3ds_model'] == 'old' and not pica['model_verification_failure']
expected_pica = sha(ROOT/'build3ds/validation/gx-direct-arena-viewer/artifact-v2/test_geometry_arena_pica.3dsx')
assert pica['sha256'] == expected_pica, 'Require the corrected pacing PICA v2 gate'
assert sha(archive/'viewer_arena.o') == 'f08558b442054a45f9d03e9f885fc3b1c5a3b1da2607fd5fdecaa1e82dad9463'
binary = archive/'fps_menu_direct_arena.3dsx'
assert sha(binary) == build['binary_sha256']
output.mkdir(parents=True)
shutil.copy2(Path(__file__), output/Path(__file__).name)
shutil.copy2(archive/'build.json', output/'frozen-build.json')
shutil.copy2(args.pica_run/'run.json', output/'pica-gate-run.json')
shutil.copy2(args.pica_run/'log.txt', output/'pica-gate-log.txt')
save = Path.home()/'Library/Application Support/Azahar/sdmc/sm64ds/fps-menu-direct-arena.sav'
journal = output/'validation-save-restore.json'
recover(journal, [save])
original = save.read_bytes() if save.exists() else None
save_journal(journal, {save: original})
fixture = {'path': str(save), 'start_fixture': 'absent before both runs',
    'original_present': original is not None,
    'original_sha256': hashlib.sha256(original).hexdigest() if original is not None else None}
environment = ['PLAYER_RESIDENT_COMPOSITE=1', 'PLAYER_RESIDENT_OBJECTS=1',
    'PLAYER_RESIDENT_NO_VBLANK_WAIT=1', 'SM64DS_FORCE_3D=1']
child = None
records = []


def interrupted(signum, frame):
    raise KeyboardInterrupt(f'Signal {signum}')


signal.signal(signal.SIGTERM, interrupted)
try:
    for name, direct in (('current', False), ('candidate', True)):
        save.unlink(missing_ok=True)
        destination = output/name
        command = ['rtk', 'proxy', sys.executable, str(ROOT/'build3ds/run.py'), str(binary),
            '--3ds-model', 'old', '--timeout', '240', '--output', str(destination)]
        for item in environment + (['PLAYER_DIRECT_ARENA=1'] if direct else []):
            command += ['--env', item]
        print('Exclusive Azahar: direct-arena '+name, flush=True)
        with (output/(name+'-runner.log')).open('wb') as log:
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            code = child.wait()
            child = None
        if save.exists():
            shutil.copy2(save, output/(name+'-generated.sav'))
        if code:
            raise RuntimeError(f'{name}: runner exit {code}; archived log retained')
        run = json.loads((destination/'run.json').read_text())
        assert run['result'] == 'PASS' and run['unmapped_access_count'] == 0
        assert run['emulator_log_fresh'] and run['temporary_settings_restored']
        assert run['requested_3ds_model'] == 'old' and not run['model_verification_failure']
        assert run['original_config_sha256'] == run['restored_config_sha256']
        assert run['sha256'] == build['binary_sha256']
        assert run['environment_overrides'] == environment + (['PLAYER_DIRECT_ARENA=1'] if direct else []) + ['SM64DS_REQUIRE_3DS_MODEL=old']
        text = (destination/'log.txt').read_text()
        assert 'new3ds=0' in text and 'frame=959 state=0 substate=0' in text
        assert f'Direct arena mode={int(direct)} retained=0;' in text
        match = re.search(r'Menu FPS measured120 ms/frame: game=([\d.]+) audio=([\d.]+) presentation=([\d.]+) total=([\d.]+) fps=([\d.]+)', text)
        assert match
        records.append({'mode': name, 'command': command, 'run': run,
            'timings': dict(zip(('game_ms', 'audio_ms', 'presentation_ms', 'total_ms', 'updates_per_second'), map(float, match.groups()))),
            'phases': {k: float(v) for k, v in re.findall(r'Menu game phase measured120 ms/frame: (.+)=([\d.]+)', text)},
            'workload': re.search(r'Menu game workloads: (.+)', text)[1]})
        print('PASS '+name+': classic, fresh log, zero Unmapped, restored config', flush=True)
finally:
    if child is not None:
        child.terminate()
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
    recover(journal, [save])
    fixture['restored'] = (save.read_bytes() if save.exists() else None) == original
    (output/'save-fixture.json').write_text(json.dumps(fixture, indent=2)+'\n')
    assert fixture['restored']
    print('Validation save restored; run.py stopped Azahar', flush=True)

assert records[0]['workload'] == records[1]['workload']
images = []
for before in sorted((output/'current').glob('*.bmp')):
    after = output/'candidate'/before.name
    left, right = Image.open(before).convert('RGB'), Image.open(after).convert('RGB')
    assert left.size == right.size
    images.append({'name': before.name, 'size': left.size,
        'different_rgb_pixels': sum(a != b for a, b in zip(left.get_flattened_data(), right.get_flattened_data())),
        'current_sha256': sha(before), 'candidate_sha256': sha(after)})
assert len(images) == 6
correct = all(i['current_sha256'] == i['candidate_sha256'] for i in images)
before, after = [r['timings'] for r in records]
report = {'status': 'PASS six byte-exact captures and identical workload' if correct else 'FAIL captured correctness',
    'binary_sha256': build['binary_sha256'], 'records': records, 'images': images, 'save': fixture,
    'elapsed_ms_saved': before['total_ms']-after['total_ms'],
    'throughput_change_percent': 100*(after['updates_per_second']/before['updates_per_second']-1),
    'geometry_region_ms_saved': (records[0]['phases']['Render/GX decode']+before['presentation_ms'])-(records[1]['phases']['Render/GX decode']+after['presentation_ms']),
    'limits': ['Single A/B pair; timing variance not characterized', '120 original menu updates, one presentation per update',
        'NO_VBLANK_WAIT=1 identical diagnostic in both modes; production normal pacing remains unchanged',
        'Retained presentation disabled; not a whole-game cadence or physical 3DS result', 'Audio render count and queue workload checked; no per-sample PCM differential in this benchmark']}
(output/'runtime-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(f"Menu elapsed {before['total_ms']:.3f} -> {after['total_ms']:.3f} ms; game updates/s {before['updates_per_second']:.3f} -> {after['updates_per_second']:.3f}; captures exact={correct}")
if not correct:
    raise SystemExit(1)
