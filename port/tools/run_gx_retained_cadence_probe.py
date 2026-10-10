#!/usr/bin/env python3
"""Run one retained-geometry cadence A/B after exclusive Azahar authorization."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, signal, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'build3ds'))
from runner_journal import save as save_journal, recover

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive', type=Path, default=ROOT/'build3ds/validation/gx-retained-cadence/geometry-v2')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--presentation', action='store_true', help='Compare full composition reuse with geometry retained in both modes and normal pacing')
parser.add_argument('--normal-pacing', action='store_true', help='Preserve original SYNCDRAW pacing in the geometry-only pair')
parser.add_argument('--production-defaults', action='store_true', help='Compare exact legacy off against final startup arena+retained defaults, normal pacing')
parser.add_argument('--pica-gate', type=Path, help='Final-layout native PICA run.json required for production-defaults')
args = parser.parse_args()
archive, output = args.archive.resolve(), args.output.resolve()
normal_pacing = args.presentation or args.normal_pacing or args.production_defaults
assert not (args.presentation and args.production_defaults)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


assert not output.exists(), 'Preserve previous native results'
build = json.loads((archive/'build.json').read_text())
for name, digest in build['archive_sha256'].items():
    assert sha(archive/name) == digest, name
for name, digest in build['link_inputs_sha256'].items():
    assert sha(name) == digest, 'Frozen input changed: '+name
pica_path = ROOT/'build3ds/validation/gx-direct-arena-viewer/native-old-v2'
pica = json.loads((pica_path/'run.json').read_text())
assert pica['result'] == 'PASS' and pica['unmapped_access_count'] == 0
assert pica['sha256'] == 'ca2ba04f65f88aaa989f21c9b86aeaf61798416c0a8ad0a1f26a213f64e806da'
assert pica['emulator_log_fresh'] and pica['temporary_settings_restored']
if args.production_defaults:
    assert args.pica_gate, 'Require final-layout PICA proof before native defaults A/B'
    final_pica = json.loads(args.pica_gate.read_text())
    assert final_pica['result'] == 'PASS' and final_pica['unmapped_access_count'] == 0
    assert final_pica['emulator_log_fresh'] and final_pica['temporary_settings_restored']
    assert final_pica['sha256'] == '79aae7025625e41967ebce1e11adc49689ea8a1ccf7bd6383a48e3620f6d3f89'
binary = archive/('sm64ds_arena_defaults.3dsx' if args.production_defaults else
    'sm64ds_retained_presentation.3dsx' if args.presentation else 'sm64ds_retained_cadence.3dsx')
assert sha(binary) == build['binary_sha256']
fixture = archive/'before.sav'
assert sha(fixture) == build['save']['sha256']
output.mkdir(parents=True)
shutil.copy2(Path(__file__), output/Path(__file__).name)
shutil.copy2(archive/'build.json', output/'frozen-build.json')
shutil.copy2(fixture, output/'before.sav')
save = Path.home()/'Library/Application Support/Azahar/sdmc/sm64ds/architecture-castle-validation.sav'
journal = output/'validation-save-restore.json'
recover(journal, [save])
original = save.read_bytes() if save.exists() else None
save_journal(journal, {save: original})
save_report = {'path': str(save), 'fixture_sha256': sha(fixture), 'original_present': original is not None,
    'original_sha256': hashlib.sha256(original).hexdigest() if original is not None else None}
environment = ['SM64DS_SAVE_PATH=sdmc:/sm64ds/architecture-castle-validation.sav',
    'SM64DS_GAME_FRAME_LIMIT=3200', 'SM64DS_GAME_REQUIRE_FINAL_SCENE=3', 'SM64DS_GAME_CAPTURE_FRAME=3199',
    'SM64DS_PROFILE_STAGE_INPUT=1', 'SM64DS_FORCE_3D=1', 'PLAYER_RESIDENT_COMPOSITE=1',
    'PLAYER_RESIDENT_OBJECTS=1', 'PLAYER_RESIDENT_NO_VBLANK_WAIT=1', 'PLAYER_UNMAPPED_CAPTURE_TIMING=1']
if normal_pacing:
    environment.remove('PLAYER_RESIDENT_NO_VBLANK_WAIT=1')
if args.presentation:
    gate = build['pica_run']
    assert gate['result'] == 'PASS' and gate['unmapped_access_count'] == 0
    assert gate['sha256'] == '95ae891e5804570c86f3b9aef8e6c1c8afd2a30e5ab7f774e51bd76d8aad440d'
    assert build['viewer_object_sha256'] == 'e11037fa98f1a42554c792e2c3b12495ad5650c6718734dfeb17ff16199458c7'
records = []
child = None


def interrupted(signum, frame):
    raise KeyboardInterrupt(f'Signal {signum}')


signal.signal(signal.SIGTERM, interrupted)
try:
    for name, reuse in (('current', False), ('candidate', True)):
        save.write_bytes(fixture.read_bytes())
        destination = output/name
        command = ['rtk', 'proxy', sys.executable, str(ROOT/'build3ds/run.py'), str(binary), '--3ds-model', 'old',
            '--timeout', '240', '--output', str(destination)]
        knob = 'PLAYER_DIRECT_PRESENT_REUSE=1' if args.presentation else 'PLAYER_DIRECT_ARENA_REUSE=1'
        mode_environment = (['PLAYER_DIRECT_ARENA=0'] if not reuse else []) if args.production_defaults else ([knob] if reuse else [])
        for item in environment + mode_environment:
            command += ['--env', item]
        print('Exclusive Azahar: retained geometry '+name, flush=True)
        with (output/(name+'-runner.log')).open('wb') as log:
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            code = child.wait()
            child = None
        if save.exists():
            shutil.copy2(save, output/(name+'-after.sav'))
        if code:
            raise RuntimeError(f'{name}: runner exit {code}; preserved diagnostic log')
        run = json.loads((destination/'run.json').read_text())
        assert run['result'] == 'PASS' and run['unmapped_access_count'] == 0
        assert run['emulator_log_fresh'] and run['temporary_settings_restored']
        assert run['requested_3ds_model'] == 'old' and not run['model_verification_failure']
        assert run['original_config_sha256'] == run['restored_config_sha256']
        assert run['sha256'] == build['binary_sha256']
        text = (destination/'log.txt').read_text()
        expected_geometry = 1 if args.presentation else int(reuse)
        expected_indexed = int(reuse) if args.production_defaults else 1
        assert 'new3ds=0' in text and f'Retained cadence indexed={expected_indexed} geometry_reuse={expected_geometry};' in text
        if args.production_defaults:
            assert 'final production startup' in text
            assert not any(e.startswith('PLAYER_DIRECT_ARENA_REUSE=') or e.startswith('PLAYER_DIRECT_PRESENT_REUSE=') for e in run['environment_overrides'])
        if args.presentation:
            assert f'Retained composition mode={int(reuse)} normal_pacing=1' in text
        if normal_pacing:
            assert all(not e.startswith('PLAYER_RESIDENT_NO_VBLANK_WAIT=') for e in run['environment_overrides'])
            pacing = re.findall(r'Resident GPU parts ms: .*?vblankWait=(\d+)', text)
            assert pacing and all(value == '1' for value in pacing), 'Require original SYNCDRAW on every reported resident draw'
        assert 'stereo=1' in text, 'Require actual stereo geometry in the stage profile'
        stopped = re.search(r'Native game stopped: frames=(\d+) updates=(\d+) input_events=(\d+) scene=(\d+) failed=(\d+)', text)
        assert stopped and stopped[1] == '3200' and stopped[4] == '3' and stopped[5] == '0'
        windows = []
        for m in re.finditer(r'Interactive cadence scene=3 kind=(held-update|new-update) vblanks=(\d+) updates=(\d+) mean_ms resume=([\d.]+) audio_pump=([\d.]+) present=([\d.]+) total=([\d.]+)', text):
            windows.append(dict(zip(('kind', 'vblanks', 'updates', 'resume_ms', 'audio_ms', 'present_ms', 'total_ms'),
                (m[1], int(m[2]), int(m[3]), *map(float, m.groups()[3:])))))
        assert windows and len(windows) % 2 == 0
        totals = {}
        for kind in ('held-update', 'new-update'):
            group = [w for w in windows if w['kind'] == kind]
            n = sum(w['vblanks'] for w in group)
            totals[kind] = {'vblanks': n, 'updates': sum(w['updates'] for w in group),
                **{k: sum(w[k]*w['vblanks'] for w in group)/n for k in ('resume_ms', 'audio_ms', 'present_ms', 'total_ms')}}
        assert totals['held-update']['updates'] == 0
        assert totals['new-update']['updates'] == totals['new-update']['vblanks']
        updates = totals['new-update']['updates']
        per_update = sum(t['total_ms']*t['vblanks'] for t in totals.values())/updates
        records.append({'mode': name, 'command': command, 'run': run,
            'stopped_counts': dict(zip(('vblanks', 'updates', 'input_events', 'scene', 'failed'), map(int, stopped.groups()))),
            'windows': windows, 'totals': totals, 'total_ms_per_original_update': per_update,
            'original_updates_per_second': 1000/per_update})
        if args.presentation:
            stats = [dict(zip(('queries', 'hits', 'draws', 'compared_bytes', 'snapshotted_bytes'), map(int, m.groups())))
                for m in re.finditer(r'Retained presentation scene=3 queries=(\d+) hits=(\d+) draws=(\d+) compared_bytes=(\d+) snapshotted_bytes=(\d+)', text)]
            assert len(stats) == len(windows)//2
            records[-1]['presentation_reuse_stats'] = {k: sum(s[k] for s in stats) for k in stats[0]}
            if reuse:
                assert records[-1]['presentation_reuse_stats']['hits'] > 0
            else:
                assert records[-1]['presentation_reuse_stats']['queries'] == 0
                assert records[-1]['presentation_reuse_stats']['hits'] == 0
        print(f'PASS {name}: {per_update:.6f} ms/original update, classic, zero Unmapped', flush=True)
finally:
    if child is not None:
        child.terminate()
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
    recover(journal, [save])
    save_report['restored'] = (save.read_bytes() if save.exists() else None) == original
    (output/'save-fixture.json').write_text(json.dumps(save_report, indent=2)+'\n')
    assert save_report['restored']
    print('Save restored; run.py stopped Azahar', flush=True)

assert records[0]['stopped_counts'] == records[1]['stopped_counts']
for kind in ('held-update', 'new-update'):
    assert records[0]['totals'][kind]['vblanks'] == records[1]['totals'][kind]['vblanks']
left = {p.name: p for p in (output/'current').glob('*.bmp')}
right = {p.name: p for p in (output/'candidate').glob('*.bmp')}
assert left.keys() == right.keys() and len(left) >= 6
images = {name: {'current_sha256': sha(left[name]), 'candidate_sha256': sha(right[name])} for name in sorted(left)}
correct = all(v['current_sha256'] == v['candidate_sha256'] for v in images.values())
save_images = {name: sha(output/(name+'-after.sav')) for name in ('current', 'candidate')}
correct = correct and save_images['current'] == save_images['candidate']
before, after = [r['total_ms_per_original_update'] for r in records]
report = {'status': 'PASS captures/counts and original update cadence' if correct else 'FAIL captured correctness',
    'correctness_status': 'PASS' if correct else 'FAIL',
    'performance_status': 'IMPROVEMENT' if after < before else 'REGRESSION',
    'binary_sha256': build['binary_sha256'], 'records': records, 'images': images, 'save': save_report,
    'final_save_sha256': save_images,
    'total_ms_per_original_update_saved': before-after,
    'original_update_throughput_change_percent': 100*(before/after-1),
    'comparison': 'Final production arena+held geometry defaults versus exact legacy' if args.production_defaults else
        'Full composition reuse; retained geometry in both modes' if args.presentation else 'Retained geometry only',
    'limits': ['One pair; timing variance not characterized',
        'Original SYNCDRAW pacing preserved in both modes' if normal_pacing else 'NO_VBLANK_WAIT=1 identical diagnostic; normal production pacing unchanged',
        'LCD transfer/swap remains executed on hits; audio/IRQ/PPU continue' if args.presentation else 'Retained vertices only; all PPU/composition still executed',
        'No physical hardware or full-game coverage claim']}
(output/'runtime-report.json').write_text(json.dumps(report, indent=2)+'\n')
print(f'Retained cadence {before:.6f} -> {after:.6f} ms/original update; captures exact={correct}')
if not correct:
    raise SystemExit(1)
