#!/usr/bin/env python3
"""Build/run the prepared legacy-vs-descriptor host differential, compile ARM11.

ARM compilation is reported independently; no native execution is inferred.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(prepared, output):
    if output.exists():
        raise ValueError('Keep prior build archives immutable')
    output.mkdir(parents=True)
    plan = json.loads((prepared/'preparation.json').read_text())
    sources = [{'source': str(prepared/p), 'definitions': []} for p in (
        'port/unmatched/native_actor_lifecycle.cpp', 'native_actor_lifecycles.cpp',
        'port/tests/test_native_actor_lifecycle.cpp')] + plan['legacy_objects']
    commands = []
    includes = ['-I'+str(prepared/'port'), '-I'+str(prepared)]
    host_objects = []
    for platform, compiler, flags in (
        ('host', shutil.which('clang++'), ['-std=c++17', '-O1', '-g',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all']),
        ('arm11', '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',
         ['-std=gnu++17', '-O2', '-march=armv6k', '-mtune=mpcore',
          '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations', '-D__3DS__'])):
        for index, row in enumerate(sources):
            obj = output/f'{platform}-{index}.o'
            command = ['rtk', 'proxy', compiler, *flags, *includes,
                       *['-D'+v for v in row['definitions']],
                       '-c', row['source'], '-o', str(obj)]
            process = subprocess.run(command, text=True, capture_output=True)
            log = output/f'{platform}-{index}.log'
            log.write_text(process.stdout+process.stderr)
            commands.append({'platform': platform, 'command': command,
                             'source_sha256': sha(Path(row['source'])),
                             'exit_code': process.returncode, 'log': str(log),
                             'object_sha256': sha(obj) if obj.exists() else None})
            if process.returncode:
                break
            if platform == 'host':
                host_objects.append(obj)
    host_ok = len(host_objects) == len(sources)
    host_run = {'status': 'NOT_EXECUTED'}
    if host_ok:
        executable = output/'test-native-actor-lifecycle'
        command = ['rtk', 'proxy', shutil.which('clang++'), '-fsanitize=address,undefined',
                   *map(str, host_objects), '-o', str(executable)]
        process = subprocess.run(command, text=True, capture_output=True)
        (output/'host-link.log').write_text(process.stdout+process.stderr)
        host_run = {'status': 'LINK_FAIL', 'link_command': command,
                    'link_exit_code': process.returncode}
        if process.returncode == 0:
            process = subprocess.run(['rtk', 'proxy', str(executable)], text=True, capture_output=True)
            (output/'host-run.log').write_text(process.stdout+process.stderr)
            host_run |= {'status': 'PASS' if process.returncode == 0 and process.stdout.startswith('PASS ')
                         else 'FAIL', 'run_exit_code': process.returncode,
                         'executable_sha256': sha(executable), 'output': process.stdout+process.stderr}
    arm_rows = [r for r in commands if r['platform'] == 'arm11']
    arm_ok = len(arm_rows) == len(sources) and all(r['exit_code'] == 0 for r in arm_rows)
    result = {'status': 'PASS_HOST_SAN_AND_ARM_COMPILE' if host_run['status'] == 'PASS' and arm_ok
              else 'FAIL', 'host': host_run, 'arm11_compile': 'PASS' if arm_ok else 'FAIL',
              'native_runtime': 'NOT_EXECUTED', 'commands': commands,
              'preparation_sha256': sha(prepared/'preparation.json'),
              'limits': plan['limits']}
    shutil.copy2(Path(__file__), output/Path(__file__).name)
    (output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.prepared.resolve(), args.output.resolve())
    print(result['status'], result['host']['status'], result['arm11_compile'])
