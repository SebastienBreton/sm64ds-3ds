#!/usr/bin/env python3
"""Prepare a real rt_run cadence comparison on isolated, frozen inputs.

Both modes use indexed GX. Only geometry preparation between unchanged game
updates differs. No common build, menu-compile mutation or emulator operation.
"""
from pathlib import Path
import hashlib, json, re, shlex, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT/'port'
SHARED = ROOT/'build3ds/cmake'
PIPE = ROOT/'build3ds/validation/gx-direct-arena-pipeline'
VIEW = PIPE/'pacing-v2'
CADENCE = ROOT/'build3ds/validation/interactive-cadence-profile/capture-clock'
OUT = ROOT/'build3ds/validation/gx-retained-cadence/geometry-v2'
GCC = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


assert not OUT.exists(), 'Preserve previous probes; choose a new artifact version'
pipeline = json.loads((VIEW/'build.json').read_text())
for name, digest in pipeline['archive_sha256'].items():
    assert sha(VIEW/name) == digest, name
for name, digest in pipeline['link_inputs_sha256'].items():
    assert sha(name) == digest, 'Pipeline input changed: '+name
OUT.mkdir(parents=True)
commands = []


def run(command, name):
    commands.append(command)
    with (OUT/(name+'.log')).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)


source = CADENCE/'game_cadence_profile.cpp'
shutil.copy2(source, OUT/'game_cadence_original.cpp')
code = source.read_text()
code = code.replace('#include "ntr/gx.h"', '#include "ntr/gx.h"\n#include "gx_direct_pipeline_api.h"', 1)
anchor = ' setvbuf(stdout,nullptr,_IONBF,0);'
assert code.count(anchor) == 1
code = code.replace(anchor, anchor+'''
 ntr::gx_direct_configure(true);
 const bool retained=std::getenv("PLAYER_DIRECT_ARENA_REUSE")!=nullptr;
 player_direct_configure(retained);
 std::printf("Retained cadence indexed=1 geometry_reuse=%u; original rt_run audio IRQ pacing\\n",unsigned(retained));
''', 1)
entry = OUT/'game_retained_cadence.cpp'
entry.write_text(code)
for name in ('gx_direct_pipeline_api.h', 'gx_geometry_arena_candidate.h', 'gx_arena_resources_candidate.h',
             'gx_arena_release_builder.h', 'player_present-pipeline.cpp', 'gx-pipeline.cpp'):
    shutil.copy2(PIPE/name, OUT/name)
for name in ('geometry_arena_viewer.h', 'viewer-arena-candidate.cpp'):
    shutil.copy2(VIEW/name, OUT/name)
flags_path = SHARED/'CMakeFiles/smoke_player.dir/flags.make'
shutil.copy2(flags_path, OUT/'flags.make')
flags_text = flags_path.read_text()
flags = [v for key in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS')
         for v in shlex.split(re.search(r'^'+key+r' = (.*)$', flags_text, re.M)[1])]
run([GCC, *flags, '-I'+str(OUT), '-I'+str(PORT), '-c', str(entry), '-o', str(OUT/'game_retained_cadence.o')], 'compile-cadence')
shutil.copy2(PORT/'platform/3ds/game_entry.cpp', OUT/'game_entry.cpp')
run([GCC, *flags, '-I'+str(PORT/'platform/3ds'), '-c', str(OUT/'game_entry.cpp'), '-o', str(OUT/'game_entry.o')], 'compile-entry')

command = json.loads((CADENCE/'link-command.json').read_text())
replacements = {
    str(CADENCE/'game_entry.o'): str(OUT/'game_entry.o'),
    str(ROOT/'build3ds/menu-compile/game_cadence_profile.o'): str(OUT/'game_retained_cadence.o'),
    str(SHARED/'CMakeFiles/smoke_player.dir/platform/3ds/viewer_3ds.o'): str(VIEW/'viewer_arena.o'),
    str(ROOT/'build3ds/validation/unmapped-capture-clock/rt-artifact/player_present.o'): str(PIPE/'player_present.o'),
    str(ROOT/'build3ds/validation/unmapped-capture-clock/rt-artifact/libntr-rt-candidate.a'): str(PIPE/'libntr-pipeline.a'),
}
for old in replacements:
    assert old in command, 'Missing archived link input '+old
command = [replacements.get(t, t) for t in command]
command[command.index('-o')+1] = str(OUT/'sm64ds_retained_cadence.elf')
inputs = {t: sha(t) for t in command if t.endswith(('.o', '.a', '.ld'))}
run(command, 'link')
assert all(sha(t) == digest for t, digest in inputs.items()), 'Inputs changed during link'
run(['/opt/devkitpro/tools/bin/3dsxtool', str(OUT/'sm64ds_retained_cadence.elf'), str(OUT/'sm64ds_retained_cadence.3dsx')], '3dsx')
shutil.copy2(Path(__file__), OUT/Path(__file__).name)
fixture = ROOT/'build3ds/validation/castle-interior-walk/before.sav'
assert sha(fixture) == '54738d9ea2d7e6a56794c151d23f066bcbcf25caeaefc67ca538a6be4904ac94'
shutil.copy2(fixture, OUT/'before.sav')
report = {'status': 'ARM11 real rt_run retained geometry probe link PASS; native runtime NOT_EXECUTED',
    'production_modified': False,
    'commands': commands, 'link_inputs_sha256': inputs,
    'binary_sha256': sha(OUT/'sm64ds_retained_cadence.3dsx'),
    'pipeline_proof': str(VIEW/'build.json'), 'pipeline_proof_sha256': sha(VIEW/'build.json'),
    'modes': {'0': 'unset PLAYER_DIRECT_ARENA_REUSE: direct rebuild each VBlank',
              '1': 'PLAYER_DIRECT_ARENA_REUSE=1: retain geometry only with exact generation/resource/lease guard'},
    'save': {'fixture': str(OUT/'before.sav'), 'sha256': sha(fixture), 'provenance': 'Legitimately earned castle key; no progression writes in this probe'},
    'limits': ['Consumer v2 still redraws/composes every VBlank', 'No complete-composition reuse in this probe',
               'Original rt_run/audio/IRQ/inputs and pacing policy retained; workload must be verified at runtime',
               'No native speed, pixel, stereo or update-cadence claim until paired runs']}
report['archive_sha256'] = {p.name: sha(p) for p in sorted(OUT.iterdir()) if p.is_file()}
(OUT/'build.json').write_text(json.dumps(report, indent=2)+'\n')
print(report['status'])
print(report['binary_sha256'])
