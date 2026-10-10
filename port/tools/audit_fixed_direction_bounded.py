#!/usr/bin/env python3
"""Freeze and verify a direction-matrix candidate without shared build mutations.

The GX snapshots run the existing independent lighting/specular/texgen oracles.
The ARM11 executable is standalone and is only built here, never launched.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'port'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / 'build3ds/validation/fixed-direction-bounded-matrix')
parser.add_argument('--integrated', action='store_true', help='Also check the actual production class and compile the production GX ARM11 object')
options = parser.parse_args()
OUT = options.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'include/ntr').mkdir(parents=True, exist_ok=True)
commands = []


def run(args, name):
    commands.append(args)
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{name}: exit {result.returncode}\n{result.stderr[-5000:]}')
    return result.stdout


def snapshot(source, destination):
    shutil.copy2(source, OUT / destination)


snapshot(PORT / 'ntr/gx.cpp', 'gx-current.cpp')
snapshot(PORT / 'ntr/include/ntr/fixed_direction_matrix.h', 'include/ntr/fixed_direction_matrix.h')
snapshot(PORT / 'tests/fixed_direction_bounded_candidate.h', 'include/fixed_direction_bounded_candidate.h')
snapshot(PORT / 'tests/test_fixed_direction_bounded_vectors.cpp', 'test_fixed_direction_bounded_vectors.cpp')
snapshot(PORT / 'platform/3ds/cpu_mode.h', 'include/cpu_mode.h')
snapshot(Path(__file__), 'audit_fixed_direction_bounded.py')
snapshot(ROOT / 'build3ds/validation/fixed-direction-bounded/report.json', 'conversion-prior-proof.json')
for name in ['texture', 'vram_bus', 'vram_banks']:
    snapshot(PORT / ('ntr/' + name + '.cpp'), name + '.cpp')

source = (OUT / 'gx-current.cpp').read_text()
begin = source.index('void lighting_vector(uint32_t packed, int out[3], bool negate) {')
end = source.index('\n}\n', begin) + 2
assert source[begin:end].count('{') == source[begin:end].count('}')
replacement = '''void lighting_vector(uint32_t packed, int out[3], bool negate) {
    static FixedDirectionBoundedCandidate<false> fixed_matrix;
    fixed_matrix.transform(g.vec.m, packed, out, negate);
}'''
candidate = source[:begin] + replacement + source[end:]
assert candidate.count('#include "ntr/fixed_direction_matrix.h"') == 1
candidate = candidate.replace('#include "ntr/fixed_direction_matrix.h"',
                              '#include "fixed_direction_bounded_candidate.h"')
(OUT / 'gx-candidate.cpp').write_text(candidate)

host_flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
              '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-I' + str(OUT / 'include'), '-I' + str(PORT / 'ntr/include')]
vector_flags = ['-DNTR_TEST_INTEGRATED_DIRECTION', '-DNTR_DIRECTION_CORRECTNESS_ONLY'] if options.integrated else []
run(['clang++', *host_flags, *vector_flags, str(OUT / 'test_fixed_direction_bounded_vectors.cpp'),
     '-o', str(OUT / 'test-vectors-host')], 'host-vectors-build')
vectors = run([str(OUT / 'test-vectors-host')], 'host-vectors')
gx_flags = [*host_flags, '-D__3DS__']
for name in ['gx-current', 'gx-candidate', 'texture', 'vram_bus', 'vram_banks']:
    run(['clang++', *gx_flags, '-MMD', '-MF', str(OUT / (name + '.d')),
         '-c', str(OUT / (name + '.cpp')), '-o', str(OUT / (name + '.host.o'))],
        name + '-build')
oracle_results = {}
for oracle in ['lighting', 'specular', 'texgen']:
    raw = ROOT / ('build3ds/reference-ds/flower-lighting/test_' + oracle + '.cpp')
    snapshot(raw, 'test_' + oracle + '-original.cpp')
    # Allocation cleanup is the only oracle-source change, to permit LeakSan.
    code = raw.read_text()
    assert code.count('return failures?1:0;') == 1
    code = code.replace('return failures?1:0;',
                        'for(unsigned i=2;i<8;++i){std::free(ntr::g_region[i]);'
                        'ntr::g_region[i]=nullptr;}return failures?1:0;')
    path = OUT / ('test_' + oracle + '.cpp')
    path.write_text(code)
    run(['clang++', *gx_flags, '-c', str(path), '-o', str(OUT / (oracle + '.host.o'))],
        oracle + '-build')
    oracle_results[oracle] = {}
    for policy in ['current', 'candidate']:
        binary = OUT / (oracle + '-' + policy + '-host')
        objects = [OUT / (name + '.host.o') for name in
                   ['gx-' + policy, 'texture', 'vram_bus', 'vram_banks', oracle]]
        run(['clang++', *host_flags, *map(str, objects), '-o', str(binary)],
            oracle + '-' + policy + '-link')
        result = run([str(binary)], oracle + '-' + policy)
        assert 'failures=0' in result, result
        oracle_results[oracle][policy] = result.strip()
    assert oracle_results[oracle]['current'] == oracle_results[oracle]['candidate']

flags_path = ROOT / 'build3ds/cmake/CMakeFiles/ntr.dir/flags.make'
snapshot(flags_path, 'native-flags.make')
flags_text = flags_path.read_text()
native_flags = []
for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
    native_flags += shlex.split(re.search(r'^' + key + r' = (.*)$', flags_text, re.M)[1])
native_flags = ['-I' + str(OUT / 'include'), *native_flags]
compiler = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
obj = OUT / 'test_fixed_direction_bounded_vectors.o'
elf = OUT / 'test_fixed_direction_bounded_vectors.elf'
run([compiler, *native_flags, *vector_flags, '-MMD', '-MF', str(OUT / 'native.d'), '-c',
     str(OUT / 'test_fixed_direction_bounded_vectors.cpp'), '-o', str(obj)], 'native-compile')
run([compiler, '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
     '-specs=3dsx.specs', '-Wl,--gc-sections', str(obj), '-L/opt/devkitpro/libctru/lib',
     '-lctru', '-lm', '-o', str(elf)], 'native-link')
run(['/opt/devkitpro/tools/bin/3dsxtool', str(elf),
     str(OUT / 'test_fixed_direction_bounded_vectors.3dsx')], 'native-3dsx')
run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump', '-d', '-C', str(elf)],
    'native-disassembly')
if options.integrated:
    run([compiler, *native_flags, '-MMD', '-MF', str(OUT / 'gx-integrated-native.d'),
         '-c', str(OUT / 'gx-current.cpp'), '-o', str(OUT / 'gx-integrated-native.o')],
        'gx-integrated-native-compile')
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump', '-dr', '-C',
         str(OUT / 'gx-integrated-native.o')], 'gx-integrated-native-disassembly')

dependencies = {}
for depfile in OUT.glob('*.d'):
    for token in shlex.split(depfile.read_text().replace('\\\n', ' ').split(':', 1)[1]):
        path = Path(token)
        if path.is_file():
            dependencies[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
archive_hashes = {str(p.relative_to(OUT)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'report.json'}
report = {
    'status': 'PASS host ASan/UBSan vectors and real GX oracles; ARM11 linked; native NOT_EXECUTED',
    'production_changed': False,
    'integrated_production_under_test': options.integrated,
    'vectors': vectors.splitlines()[0],
    'oracles': oracle_results,
    'native': {'status': 'NOT_EXECUTED', 'shared_objects_used': False,
               'binary': str(OUT / 'test_fixed_direction_bounded_vectors.3dsx')},
    'timing_scope': 'Host timings are not native evidence. Standalone microbench excludes GX submission, menu, audio, presentation and VBlank.',
    'legacy_undefined_inputs': 'Non-finite consumed coefficients and values overflowing legacy arithmetic are not executed; they retain the legacy fallback contract.',
    'commands': commands, 'dependencies_sha256': dependencies, 'archive_sha256': archive_hashes,
}
if options.integrated:
    report['status'] = 'PASS host ASan/UBSan actual integrated vectors and GX oracles; integrated GX ARM11 compiled; standalone native NOT_EXECUTED'
    report['production_changed'] = True
    report['timing_scope'] = 'No new timing claim. Actual integrated class checked against cached64 reference and frozen candidate; GX oracle runs use frozen production GX/header. Prior isolated and true-menu A/B evidence remains separate.'
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(report['status'])
print(json.dumps(oracle_results, indent=2))
print(OUT)
