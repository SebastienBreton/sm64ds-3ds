#!/usr/bin/env python3
"""Freeze one actual-menu binary for current64/bounded32 direction-cache A/B."""
from pathlib import Path
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SHARED = ROOT / 'build3ds/cmake'
PORT = ROOT / 'port'
PROOF = ROOT / 'build3ds/validation/fixed-direction-bounded-matrix-bit-neighbours'
OUT = ROOT / 'build3ds/validation/fixed-direction-menu-ab'
OUT.mkdir(parents=True, exist_ok=True)
GCC = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
AR = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def flags(target):
    content = (SHARED / f'CMakeFiles/{target}.dir/flags.make').read_text()
    return [token for key in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS')
            for token in shlex.split(re.search(r'^'+key+r' = (.*)$', content, re.M)[1])]


def run(command, name, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
    result.check_returncode()


assert digest(PORT / 'ntr/gx.cpp') == digest(PROOF / 'gx-current.cpp')
assert digest(PORT / 'tests/fixed_direction_bounded_candidate.h') == digest(
    PROOF / 'include/fixed_direction_bounded_candidate.h')
shutil.copy2(PROOF / 'include/fixed_direction_bounded_candidate.h', OUT)
shutil.copy2(PROOF / 'report.json', OUT / 'independent-proof.json')
source = (PORT / 'ntr/gx.cpp').read_text()
(OUT / 'gx-current.cpp').write_text(source)
policy = 'sm64ds_bounded_direction_diagnostic_enabled'
source = source.replace('#include "ntr/fixed_direction_matrix.h"',
    '#include "ntr/fixed_direction_matrix.h"\n#include "fixed_direction_bounded_candidate.h"')
anchor = '\nnamespace ntr {\n'
assert source.count(anchor) >= 1
source = source.replace(anchor, '\nextern "C" { bool '+policy+' = false; }\n'+anchor, 1)
anchor = 'void lighting_vector(uint32_t packed, int out[3], bool negate) {'
assert source.count(anchor) == 1
source = source.replace(anchor, anchor + '\n    if ('+policy+') {\n'
    '        static FixedDirectionBoundedCandidate<false> bounded_matrix;\n'
    '        bounded_matrix.transform(g.vec.m, packed, out, negate);\n'
    '        return;\n    }')
(OUT / 'gx-policy.cpp').write_text(source)
probe = (PORT / 'tests/fps_menu_game_profile.cpp').read_text()
probe = probe.replace('int main(){', 'extern "C" bool '+policy+';\nint main(){', 1)
anchor = '    setvbuf(stdout,nullptr,_IONBF,0);'
assert probe.count(anchor) == 1
probe = probe.replace(anchor, anchor + '\n    '+policy+' = '
    'std::getenv("PLAYER_BOUNDED_DIRECTION") != nullptr;\n'
    '    std::printf("Direction policy=%s; startup-only selection\\n", '
    +policy+' ? "bounded32" : "current64");')
assert 'PLAYER_BOUNDED_DIRECTION' not in source
probe_path = OUT / 'fps_menu_direction_benchmark.cpp'
probe_path.write_text(probe)
run([GCC, *flags('ntr'), '-I'+str(OUT), '-c', str(OUT/'gx-policy.cpp'),
     '-o', str(OUT/'gx.o')], 'compile-gx')
run([GCC, *flags('smoke_player'), '-c', str(probe_path),
     '-o', str(OUT/'probe.o')], 'compile-probe')

original = OUT / 'libntr-current.a'
replacement = OUT / 'libntr-policy.a'
shutil.copy2(SHARED / 'libntr.a', original)
shutil.copy2(original, replacement)
members = subprocess.check_output([AR, 't', str(original)], text=True).splitlines()
assert members.count('gx.o') == 1
member_hashes = lambda path: {member: hashlib.sha256(subprocess.check_output(
    [AR, 'p', str(path), member])).hexdigest() for member in members}
before_members = member_hashes(original)
run([AR, 'r', str(replacement), str(OUT/'gx.o')], 'replace-archive')
run([AR, 's', str(replacement)], 'index-archive')
assert subprocess.check_output([AR, 't', str(replacement)], text=True).splitlines() == members
after_members = member_hashes(replacement)
assert [m for m in members if before_members[m] != after_members[m]] == ['gx.o']
command = shlex.split((SHARED / 'CMakeFiles/smoke_player.dir/link.txt').read_text())
entry = 'CMakeFiles/smoke_player.dir/tests/smoke_player.o'
assert command.count(entry) == 1
command[command.index(entry)] = str(OUT/'probe.o')
elf = OUT / 'fps_menu_direction_benchmark.elf'
binary = elf.with_suffix('.3dsx')
command[command.index('-o')+1] = str(elf)
assert command.count('libntr.a') >= 1
command = [str(replacement) if t == 'libntr.a' else t for t in command]
command = [str(SHARED/t) if not t.startswith('-') and (SHARED/t).is_file() else t for t in command]
boot = (SHARED / 'CMakeFiles/sm64ds_sound_boot.dir/build.make').read_text()
extra = [SHARED/m[1] for m in re.finditer(r'^sm64ds_sound_boot: (.+\.o)$', boot, re.M)]
if not any('sm64ds_menu.dir' in t for t in command):
    extra += list((ROOT/'build3ds/menu-compile/objects').glob('*.o'))
existing = {Path(t).resolve() for t in command if t.endswith('.o')}
command[1:1] = [str(p) for p in extra if p.resolve() not in existing]
inputs = {Path(t) for t in command if t.endswith(('.o', '.a', '.ld'))}
input_hashes = {str(p): digest(p) for p in inputs}
run(command, 'link', cwd=OUT)
assert input_hashes == {str(p): digest(p) for p in inputs}
run(['/opt/devkitpro/tools/bin/3dsxtool', str(elf), str(binary)], '3dsx')
nm = subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm', str(elf)], text=True)
assert len([line for line in nm.splitlines() if line.endswith(' '+policy)]) == 1
assert b'GX fine measured120' not in elf.read_bytes()
(OUT/'inputs.json').write_text(json.dumps(input_hashes, indent=2)+'\n')
(OUT/'link-command.json').write_text(json.dumps(command, indent=2)+'\n')
paths = [elf, binary, probe_path, OUT/'gx-current.cpp', OUT/'gx-policy.cpp',
         OUT/'fixed_direction_bounded_candidate.h', original, replacement, Path(__file__).resolve()]
(OUT/'build.json').write_text(json.dumps({
    'status': 'PASS native link; runtime NOT_EXECUTED', 'same_binary': True,
    'startup_policy': 'PLAYER_BOUNDED_DIRECTION absent/current64 vs1/bounded32',
    'archive_changed_members': ['gx.o'], 'timed_candidate_counters': False,
    'scope': 'Real original menu; candidate exact helper from independent host/ARM proof, no production changes',
    'sha256': {str(p.relative_to(ROOT)): digest(p) for p in paths},
    'link_input_count': len(inputs)}, indent=2)+'\n')
print(binary)
