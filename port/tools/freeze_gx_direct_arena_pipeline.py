#!/usr/bin/env python3
"""Relink the proven producer against the separately frozen pacing viewer.

No common build or emulator operation. Previous binaries are never overwritten.
"""
from pathlib import Path
import hashlib, json, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT/'build3ds/validation/gx-direct-arena-pipeline'
VIEW = ROOT/'build3ds/validation/gx-direct-arena-viewer/artifact-v2'
OUT = BASE/'pacing-v2'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


assert not OUT.exists(), 'Keep previous frozen artifacts; choose a new version'
prior = json.loads((BASE/'build.json').read_text())
for relative, digest in prior['archive_sha256'].items():
    assert sha(BASE/relative) == digest, relative
viewer = json.loads((VIEW/'build.json').read_text())
for absolute, digest in viewer['hashes'].items():
    path = VIEW/Path(absolute).name
    assert sha(path) == digest, str(path)
assert sha(VIEW/'geometry_arena_viewer.h') == sha(BASE/'geometry_arena_viewer.h'), 'ABI changed'
for absolute, digest in prior['common_inputs_at_freeze'].items():
    assert sha(absolute) == digest, 'Shared input changed: '+absolute

OUT.mkdir()
shutil.copy2(Path(__file__), OUT/Path(__file__).name)
for name in ('viewer_arena.o', 'viewer-arena-candidate.cpp', 'geometry_arena_viewer.h', 'gx_geometry_arena_candidate.h', 'build.json'):
    shutil.copy2(VIEW/name, OUT/('viewer-build.json' if name == 'build.json' else name))
command = next(c for c in prior['commands'] if str(BASE/'fps_menu_direct_arena.elf') in c)
command = [str(OUT/'viewer_arena.o') if t == str(BASE/'viewer_arena.o') else t for t in command]
command[command.index('-o')+1] = str(OUT/'fps_menu_direct_arena.elf')
inputs = {t: sha(t) for t in command if t.endswith(('.o', '.a', '.ld'))}
with (OUT/'link.log').open('w') as log:
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
assert all(sha(t) == digest for t, digest in inputs.items()), 'Inputs changed during link'
convert = ['/opt/devkitpro/tools/bin/3dsxtool', str(OUT/'fps_menu_direct_arena.elf'), str(OUT/'fps_menu_direct_arena.3dsx')]
with (OUT/'3dsx.log').open('w') as log:
    subprocess.run(convert, stdout=log, stderr=subprocess.STDOUT, check=True)
report = {
    'status': 'ARM11 full pipeline link PASS; corrected pacing consumer; native PICA/A-B NOT_EXECUTED',
    'production_modified': False,
    'producer_proof': str(BASE/'build.json'), 'producer_proof_sha256': sha(BASE/'build.json'),
    'viewer_proof': str(VIEW/'build.json'), 'viewer_proof_sha256': sha(VIEW/'build.json'),
    'binary_sha256': sha(OUT/'fps_menu_direct_arena.3dsx'),
    'link_inputs_sha256': inputs, 'commands': [command, convert],
    'shared_pipeline': 'Identical IO/runtime/MMIO/capture clock/PPU/audio in both modes',
    'first_comparison': 'Original expanded pipeline vs indexed GX/direct arena; retained mode disabled',
    'limits': ['PICA pacing v2 gate must pass before A/B', 'No runtime, physical hardware, retained cadence or speed claim'],
}
report['archive_sha256'] = {p.name: sha(p) for p in sorted(OUT.iterdir()) if p.is_file()}
(OUT/'build.json').write_text(json.dumps(report, indent=2)+'\n')
print(report['status'])
print(report['binary_sha256'])
