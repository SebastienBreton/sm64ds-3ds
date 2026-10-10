#!/usr/bin/env python3
"""Freeze a real-cadence same-binary full presentation reuse comparison."""
from pathlib import Path
import hashlib, json, re, shlex, shutil, subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT/'port'
BASE = ROOT/'build3ds/validation/gx-retained-cadence/geometry-v2'
VIEW = ROOT/'build3ds/validation/gx-direct-arena-viewer/retained/artifact'
OUT = ROOT/'build3ds/validation/gx-retained-cadence/presentation-v1'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


assert not OUT.exists(), 'Never overwrite a frozen artifact'
base = json.loads((BASE/'build.json').read_text())
for name, digest in base['archive_sha256'].items():
    assert sha(BASE/name) == digest, name
for name, digest in base['link_inputs_sha256'].items():
    assert sha(name) == digest, name
assert sha(VIEW/'viewer_arena.o') == 'e11037fa98f1a42554c792e2c3b12495ad5650c6718734dfeb17ff16199458c7'
gate_path = VIEW.parent/'native-old-lcd-transfer'
gate = json.loads((gate_path/'run.json').read_text())
assert gate['result'] == 'PASS' and gate['unmapped_access_count'] == 0
assert gate['sha256'] == sha(VIEW/'test_geometry_arena_pica.3dsx')
OUT.mkdir(parents=True)
for name in ('viewer_arena.o', 'geometry_arena_viewer.h', 'viewer-arena-candidate.cpp',
             'gx_geometry_arena_candidate.h', 'presentation_reuse_key.h', 'build-report.json'):
    shutil.copy2(VIEW/name, OUT/name)
for name in ('gx_direct_pipeline_api.h', 'before.sav', 'flags.make'):
    shutil.copy2(BASE/name, OUT/name)
for name in ('run.json', 'log.txt'):
    shutil.copy2(gate_path/name, OUT/('pica-'+name))
code = (BASE/'game_retained_cadence.cpp').read_text()
code = code.replace('#include "gx_direct_pipeline_api.h"', '#include "gx_direct_pipeline_api.h"\n#include "geometry_arena_viewer.h"', 1)
code = code.replace(' const bool retained=std::getenv("PLAYER_DIRECT_ARENA_REUSE")!=nullptr;', ' const bool retained=true;')
assert code.count(' const bool retained=true;') == 1
anchor = ' if(++cadence_warm>120 && !capturing){'
assert code.count(anchor) == 1
code = code.replace(anchor, ' if(cadence_warm+1==120)v3ds_arena::presentation_reuse_stats(true);\n'+anchor, 1)
anchor = '   cadence[0]=CadenceBucket{};cadence[1]=CadenceBucket{};cadence_count=0;'
assert code.count(anchor) == 1
code = code.replace(anchor, '''   const auto reuse_stats=v3ds_arena::presentation_reuse_stats(true);
   std::printf("Retained presentation scene=%u queries=%llu hits=%llu draws=%llu compared_bytes=%llu snapshotted_bytes=%llu\\n",
    cadence_scene,(unsigned long long)reuse_stats.queries,(unsigned long long)reuse_stats.hits,
    (unsigned long long)reuse_stats.draws,(unsigned long long)reuse_stats.plane_bytes_compared,
    (unsigned long long)reuse_stats.plane_bytes_snapshotted);
'''+anchor, 1)
anchor = ' player_direct_configure(retained);'
code = code.replace(anchor, anchor+'\n std::printf("Retained composition mode=%u normal_pacing=%u\\n",unsigned(std::getenv("PLAYER_DIRECT_PRESENT_REUSE")!=nullptr),unsigned(std::getenv("PLAYER_RESIDENT_NO_VBLANK_WAIT")==nullptr));', 1)
entry = OUT/'game_retained_presentation.cpp'
entry.write_text(code)
text = (OUT/'flags.make').read_text()
flags = [v for key in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS') for v in shlex.split(re.search(r'^'+key+r' = (.*)$', text, re.M)[1])]
compile = ['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++', *flags, '-I'+str(OUT), '-I'+str(PORT/'platform/3ds'), '-c', str(entry), '-o', str(OUT/'game_retained_presentation.o')]
with (OUT/'compile.log').open('w') as log:
    subprocess.run(compile, stdout=log, stderr=subprocess.STDOUT, check=True)
command = next(c for c in base['commands'] if str(BASE/'sm64ds_retained_cadence.elf') in c)
command = [str(OUT/'game_retained_presentation.o') if t == str(BASE/'game_retained_cadence.o') else t for t in command]
old_view = ROOT/'build3ds/validation/gx-direct-arena-pipeline/pacing-v2/viewer_arena.o'
assert str(old_view) in command
command = [str(OUT/'viewer_arena.o') if t == str(old_view) else t for t in command]
command[command.index('-o')+1] = str(OUT/'sm64ds_retained_presentation.elf')
inputs = {t: sha(t) for t in command if t.endswith(('.o', '.a', '.ld'))}
with (OUT/'link.log').open('w') as log:
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
assert all(sha(t) == digest for t, digest in inputs.items())
convert = ['/opt/devkitpro/tools/bin/3dsxtool', str(OUT/'sm64ds_retained_presentation.elf'), str(OUT/'sm64ds_retained_presentation.3dsx')]
subprocess.run(convert, check=True)
shutil.copy2(Path(__file__), OUT/Path(__file__).name)
report = {'status': 'ARM11 retained full presentation link PASS; PICA consumer PASS; rt_run A/B NOT_EXECUTED',
    'production_modified': False, 'commands': [compile, command, convert], 'link_inputs_sha256': inputs,
    'binary_sha256': sha(OUT/'sm64ds_retained_presentation.3dsx'),
    'geometry_proof': str(BASE/'build.json'), 'geometry_proof_sha256': sha(BASE/'build.json'),
    'viewer_object_sha256': sha(OUT/'viewer_arena.o'), 'pica_run': gate,
    'modes': {'0': 'Retained geometry enabled; unset PLAYER_DIRECT_PRESENT_REUSE; redraw/compose every VBlank',
              '1': 'Retained geometry enabled; PLAYER_DIRECT_PRESENT_REUSE=1; exact-key hit retains composition but preserves LCD transfer/swap'},
    'pacing': 'No PLAYER_RESIDENT_NO_VBLANK_WAIT in either mode; original SYNCDRAW',
    'save': base['save'],
    'limits': ['Lease resources immutable under producer exact-byte guard; physical capture excludes resident path',
               'Original rt_run/audio/IRQ/inputs unchanged; update/VBlank counts require native comparison',
               'Window counters are diagnostic and shared; no per-command timers', 'No runtime performance claim before paired gate']}
report['archive_sha256'] = {p.name: sha(p) for p in sorted(OUT.iterdir()) if p.is_file()}
(OUT/'build.json').write_text(json.dumps(report, indent=2)+'\n')
print(report['status'])
print(report['binary_sha256'])
