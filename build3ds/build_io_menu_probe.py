#!/usr/bin/env python3
"""Freeze one true-menu binary with a startup-only I/O dispatch policy switch."""
from pathlib import Path
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SHARED = ROOT/'build3ds/cmake'
PORT = ROOT/'port'
OUT = ROOT/'build3ds/validation/menu-cpu-ingress-ab'
OUT.mkdir(parents=True, exist_ok=True)
GCC = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
AR = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar'


def flags(target):
    text = (SHARED/f'CMakeFiles/{target}.dir/flags.make').read_text()
    args = []
    for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
        args += shlex.split(re.search(r'^'+key+r' = (.*)$', text, re.M)[1])
    return args


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


source = (PORT/'ntr/io.cpp').read_text()
condition = '    if (width == 4 && (addr == 0x04000400u ||'
assert source.count(condition) == 1
policy = 'sm64ds_geometry_fastpath_diagnostic_enabled'
toggle = source.replace(condition, f'    if ({policy} && width == 4 && (addr == 0x04000400u ||')
anchor = '\nnamespace ntr {\n'
assert toggle.count(anchor) >= 1
toggle = toggle.replace(anchor, '\nextern "C" { bool '+policy+' = true; }\n'+anchor, 1)
(OUT/'io-current.cpp').write_text(source)
(OUT/'io-policy.cpp').write_text(toggle)

probe = (PORT/'tests/fps_menu_game_profile.cpp').read_text()
probe = probe.replace('int main(){', 'extern "C" bool '+policy+';\nint main(){', 1)
initialization = '    setvbuf(stdout,nullptr,_IONBF,0);'
assert probe.count(initialization) == 1
probe = probe.replace(initialization, initialization+f'''
    {policy} = std::getenv("PLAYER_LEGACY_GEOMETRY_DISPATCH") == nullptr;
    std::printf("Geometry dispatch policy=%s; startup-only environment selection\\n",
        {policy} ? "fast32" : "legacy");''')
assert toggle.count('PLAYER_LEGACY_GEOMETRY_DISPATCH') == 0
assert probe.count('std::getenv("PLAYER_LEGACY_GEOMETRY_DISPATCH")') == 1
probe_path = OUT/'fps_menu_geometry_dispatch_benchmark.cpp'
probe_path.write_text(probe)
compile_commands = [
    [GCC, *flags('ntr'), '-c', str(OUT/'io-policy.cpp'), '-o', str(OUT/'io.o')],
    [GCC, *flags('smoke_player'), '-c', str(probe_path), '-o', str(OUT/'probe.o')],
]
for i, command in enumerate(compile_commands):
    result = subprocess.run(command, capture_output=True, text=True)
    (OUT/f'compile-{i}.log').write_text(result.stdout+result.stderr)
    result.check_returncode()

# Replace the single io.o archive member. No extra I/O object is linked before
# the library and no duplicate definitions of its globals/initializers exist.
original_archive = OUT/'libntr-current.a'
policy_archive = OUT/'libntr-policy.a'
shutil.copy2(SHARED/'libntr.a', original_archive)
shutil.copy2(original_archive, policy_archive)
members = subprocess.check_output([AR, 't', str(original_archive)], text=True).splitlines()
assert members.count('io.o') == 1
old_hashes = {m: hashlib.sha256(subprocess.check_output([AR, 'p', str(original_archive), m])).hexdigest() for m in members}
subprocess.run([AR, 'r', str(policy_archive), str(OUT/'io.o')], check=True)
subprocess.run([AR, 's', str(policy_archive)], check=True)
new_members = subprocess.check_output([AR, 't', str(policy_archive)], text=True).splitlines()
assert new_members == members
new_hashes = {m: hashlib.sha256(subprocess.check_output([AR, 'p', str(policy_archive), m])).hexdigest() for m in members}
changed_members = [m for m in members if old_hashes[m] != new_hashes[m]]
assert changed_members == ['io.o']

command = shlex.split((SHARED/'CMakeFiles/smoke_player.dir/link.txt').read_text())
old = 'CMakeFiles/smoke_player.dir/tests/smoke_player.o'
assert command.count(old) == 1
command[command.index(old)] = str(OUT/'probe.o')
elf = OUT/'fps_menu_geometry_dispatch_benchmark.elf'
binary = elf.with_suffix('.3dsx')
command[command.index('-o')+1] = str(elf)
assert command.count('libntr.a') == 1
command[command.index('libntr.a')] = str(policy_archive)
command = [str(SHARED/token) if not token.startswith('-') and (SHARED/token).is_file() else token for token in command]
# Same legacy closure rule as build_menu_probe.py.
new = [] if any('sm64ds_menu.dir' in token for token in command) else list((ROOT/'build3ds/menu-compile/objects').glob('*.o'))
boot = (SHARED/'CMakeFiles/sm64ds_sound_boot.dir/build.make').read_text()
new += [SHARED/m[1] for m in re.finditer(r'^sm64ds_sound_boot: (.+\.o)$', boot, re.M)]
existing = {Path(token).resolve() for token in command if token.endswith('.o')}
command[1:1] = [str(p) for p in new if p.resolve() not in existing]
inputs = [Path(token) for token in command if token.endswith(('.o', '.a'))]
before = {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for p in inputs}
result = subprocess.run(command, cwd=OUT, capture_output=True, text=True)
(OUT/'link.log').write_text(result.stdout+result.stderr)
result.check_returncode()
assert before == {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for p in inputs}, 'Shared link inputs changed during link'
subprocess.run(['/opt/devkitpro/tools/bin/3dsxtool', str(elf), str(binary)], check=True)
nm = subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm', str(elf)], text=True)
definitions = [line for line in nm.splitlines() if line.endswith(' '+policy)]
assert len(definitions) == 1 and ' D ' in definitions[0]
assert len([line for line in nm.splitlines() if line.endswith(' _ZN3ntr8io_writeEmyj')]) == 1
(OUT/'policy-symbols.txt').write_text('\n'.join(definitions+[line for line in nm.splitlines() if line.endswith(' _ZN3ntr8io_writeEmyj')])+'\n')
(OUT/'link-command.json').write_text(json.dumps(command, indent=2)+'\n')
paths = [probe_path, OUT/'io-current.cpp', OUT/'io-policy.cpp', OUT/'io.o', OUT/'probe.o', original_archive, policy_archive, elf, binary, Path(__file__).resolve()]
cache = (SHARED/'CMakeCache.txt').read_text()
gpu = re.search(r'^SM64DS_EXPERIMENTAL_GPU_COMPOSITE:BOOL=(.*)$', cache, re.M)[1]
assert gpu == 'OFF', 'This frozen benchmark requires the coordinated CPU-composite graph'
assert b'GX fine measured120' not in elf.read_bytes(), 'Fine instrumentation unexpectedly linked'
report = {'schema':'sm64ds-menu-io-same-binary-build-v1', 'status':'LINKED; runtime and timings NOT_EXECUTED',
    'legacy_env':'PLAYER_LEGACY_GEOMETRY_DISPATCH=1', 'fast_env':'variable absent',
    'same_binary':True, 'environment_queries_per_run':1, 'policy_queries_per_write':0,
    'gpu_composite_graph':gpu, 'fine_profile_linked':False,
    'archive_changed_members':changed_members, 'original_member_hashes':old_hashes, 'policy_member_hashes':new_hashes,
    'source_sha256':digest(PORT/'ntr/io.cpp'), 'shared_input_stats_stable':True,
    'scope':'One true menu update and presentation per sample; VBlank/elapsed FPS cap remains visible; do not infer physical 3DS speed',
    'sha256':{str(p.relative_to(ROOT)):digest(p) for p in paths},
    'linked_input_sha256':{str(p):digest(p) for p in inputs}}
(OUT/'build.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ['sha256','linked_input_sha256','original_member_hashes','policy_member_hashes']}, indent=2))
