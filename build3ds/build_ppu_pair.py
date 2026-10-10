#!/usr/bin/env python3
"""Build matched original-loop probes with two PPU sources.

Run after the shared CMake build stops. No shared objects are modified.
Reserve Azahar separately before using run.py on the resulting binaries.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / 'build3ds/cmake'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
compiler = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
ar = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar'

def flags(target, keys):
    source = (build / f'CMakeFiles/{target}.dir/flags.make').read_text()
    result = []
    for key in keys:
        result += shlex.split(re.search(r'^'+key+r' = (.*)$', source, re.M)[1])
    return result

def run(command):
    subprocess.run(command, cwd=build, check=True)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

probe = output / 'original-loop.o'
run([compiler, *flags('smoke_player', ['CXX_INCLUDES', 'CXX_FLAGS']),
     '-Dmain=smoke_main', '-c', str(root/'port/tests/smoke_original_loop.cpp'),
     '-o', str(probe)])
link = shlex.split((build/'CMakeFiles/smoke_player.dir/link.txt').read_text())
original = 'CMakeFiles/smoke_player.dir/tests/smoke_player.o'
assert link.count(original) == 1 and link.count('libntr.a') == 1
link[link.index(original)] = str(probe)
link[1:1] = ['-Wl,--wrap='+s for s in ('func_02019390', 'func_02030e4c', 'func_0203df40')]
manifest = {'probe_sha256': digest(probe),
            'link_command': link,
            'common_ntr_sha256': digest(build/'libntr.a'), 'variants': {}}
for name, source in [('before', args.baseline.resolve()),
                     ('after', root/'port/ntr/ppu.cpp')]:
    directory = output / name
    directory.mkdir(exist_ok=True)
    # Preserve the source and compiled member's name: ar replaces ppu.o.
    snapshot = directory/'ppu.cpp'
    shutil.copy2(source, snapshot)
    obj = directory/'ppu.o'
    run([compiler, *flags('ntr', ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']),
         '-c', str(snapshot), '-o', str(obj)])
    library = directory/'libntr.a'
    shutil.copy2(build/'libntr.a', library)
    run([ar, 'r', str(library), str(obj)])
    elf = directory/'smoke_original_loop.elf'
    binary = directory/'smoke_original_loop.3dsx'
    elf.unlink(missing_ok=True)
    binary.unlink(missing_ok=True)
    command = list(link)
    command[command.index('libntr.a')] = str(library)
    command[command.index('-o')+1] = str(elf)
    run(command)
    run(['/opt/devkitpro/tools/bin/3dsxtool', str(elf), str(binary)])
    manifest['variants'][name] = {p.name: digest(p) for p in (snapshot, obj, elf, binary)}
(output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Built matched PPU pair:', output)
