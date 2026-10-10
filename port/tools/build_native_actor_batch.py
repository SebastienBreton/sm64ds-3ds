#!/usr/bin/env python3
"""Compile a frozen actor batch with production source/ABI adapters, privately.

Report every failure. This does not establish link, data or gameplay closure.
"""
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path
from native_level_actor_sources import generate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(root, plan_path, flags_path, output):
    if output.exists():
        raise ValueError('Keep prior build archives immutable')
    plan = json.loads(plan_path.read_text())
    output.mkdir(parents=True)
    flags_text = flags_path.read_text()
    shutil.copy2(flags_path, output/'flags.make')
    generator_files = [Path(__file__).with_name(n) for n in (
        'hostgen.py', 'native_level_actor_sources.py', 'native_vtable_stores.py',
        'native_vtable_layouts.json', 'level_actor_cpp_adapters.py')]
    initial = {str(p): sha(p) for p in generator_files}
    for p in generator_files:
        shutil.copy2(p, output/p.name)
    rows = []
    with tempfile.TemporaryDirectory(prefix='private-actor-batch-') as staging:
        for source, expected in plan['original_tu_sha256'].items():
            if sha(root/source) != expected:
                raise ValueError('Original source changed: '+source)
            row = {'source': source, 'original_sha256': expected}
            try:
                relative, text, stores = generate(root, source, Path(staging))
                generated = output/'generated'/relative
                generated.parent.mkdir(parents=True, exist_ok=True)
                generated.write_text(text)
                language = 'C' if generated.suffix == '.c' else 'CXX'
                flags = [v for key in (language+'_DEFINES', language+'_INCLUDES', language+'_FLAGS')
                         for v in shlex.split(re.search(r'^'+key+r' = (.*)$', flags_text, re.M)[1])]
                compiler = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'+('gcc' if language == 'C' else 'g++')
                obj = output/'objects'/Path(source).with_suffix('.o')
                obj.parent.mkdir(parents=True, exist_ok=True)
                command = ['rtk', 'proxy', compiler, *flags,
                           '-I'+str(Path(__file__).resolve().parents[1] / 'platform/3ds'), '-c', str(generated), '-o', str(obj)]
                process = subprocess.run(command, text=True, capture_output=True)
                log = output/'logs'/Path(source).with_suffix('.log')
                log.parent.mkdir(parents=True, exist_ok=True)
                log.write_text(process.stdout+process.stderr)
                row |= {'language': language, 'generated': str(generated),
                        'generated_sha256': sha(generated), 'vtable_store_adaptations': stores,
                        'command': command, 'exit_code': process.returncode,
                        'log': str(log), 'object': str(obj),
                        'object_sha256': sha(obj) if obj.exists() else None}
            except Exception as error:
                row |= {'exit_code': None, 'generation_error': str(error)}
            rows.append(row)
            print(('PASS ' if row['exit_code'] == 0 else 'FAIL ')+source, flush=True)
    if any(sha(Path(p)) != value for p, value in initial.items()):
        raise ValueError('Generator inputs changed during private build')
    result = {'status': 'COMPILE_PASS_NOT_LINKED' if all(r['exit_code'] == 0 for r in rows)
              else 'COMPILE_FAIL', 'translation_units': len(rows),
              'compiled': sum(r['exit_code'] == 0 for r in rows), 'rows': rows,
              'plan_sha256': sha(plan_path), 'flags_sha256': sha(flags_path),
              'generator_sha256': initial,
              'limits': ['Compilation only; native data, linker, ABI and original runtime remain unproved.',
                         'No shared object, matched source, CMake or user binary modified.']}
    (output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    shutil.copy2(Path(__file__), output/Path(__file__).name)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--flags', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = build(args.root.resolve(), args.plan.resolve(), args.flags.resolve(), args.output.resolve())
    print(report['status'], report['compiled'], '/', report['translation_units'])
