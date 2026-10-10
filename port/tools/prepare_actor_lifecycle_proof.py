#!/usr/bin/env python3
"""Prepare a differential of generated descriptors versus frozen bank wrappers.

No compilation or emulator is invoked. The resulting fixture observes original
constructor order/scope/BSS and registry restoration, not original actor bodies.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from native_actor_lifecycle import generate, modules


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(root, output):
    if output.exists():
        raise ValueError('Keep previous proof archives immutable')
    manifest_path = Path(__file__).with_name('native_actor_overlay_manifest.json')
    manifest = json.loads(manifest_path.read_text())
    generated = generate(root, manifest)
    entries = modules(manifest)
    output.mkdir(parents=True)
    (output/'native_actor_lifecycles.cpp').write_text(generated)
    fixture = ['#include <cstdio>', '#include <cstdlib>', 'extern "C" {']
    commands = []
    hashes = {}
    for module in entries:
        ov = module['overlay']
        previous = Path(__file__).resolve().parents[1] / f'unmatched/overlay{ov}.cpp'
        target = output/f'legacy_overlay{ov}.cpp'
        shutil.copy2(previous, target)
        hashes[str(previous)] = sha(previous)
        source = previous.read_text()
        # Preserve and verify all descriptor fields from the original wrappers.
        expected = [f'if(bytes!={module["bss_bytes"]})return false;',
                    f'if(sm64ds_reset_level_data({ov})!={module["reset_bytes"]})return false;',
                    'const unsigned ids[]={'+','.join(str(a) for a, _ in module['profiles'])+'};',
                    'void*const profiles[]={'+','.join(n for _, n in module['profiles'])+'};']
        if not all(s in source for s in expected):
            raise ValueError(f'overlay{ov}: legacy descriptor differs')
        calls = re.findall(r'^    (__sinit_\w+)\(\);', source, re.M)
        if calls != module['constructors']:
            raise ValueError(f'overlay{ov}: legacy constructor order differs')
        fixture += [f'unsigned char sm64ds_overlay{ov}_bss_begin[{module["bss_bytes"]}];',
                    '#if defined(__APPLE__)',
                    f'asm(".global _sm64ds_overlay{ov}_bss_end\\n.set _sm64ds_overlay{ov}_bss_end,_sm64ds_overlay{ov}_bss_begin+{module["bss_bytes"]}");',
                    '#else',
                    f'asm(".global sm64ds_overlay{ov}_bss_end\\n.set sm64ds_overlay{ov}_bss_end,sm64ds_overlay{ov}_bss_begin+{module["bss_bytes"]}");',
                    '#endif']
        for _, name in module['profiles']:
            fixture.append(f'unsigned char {name}[28];')
        for index, name in enumerate(module['constructors']):
            fixture.append(f'void {name}(){{constructor({ov},{index});}}')
        for prefix in ('', 'legacy_'):
            fixture += [f'bool {prefix}sm64ds_load_overlay{ov}();',
                        f'bool {prefix}sm64ds_unload_overlay{ov}();']
        commands.append({'source': str(target), 'definitions': [
            f'sm64ds_load_overlay{ov}=legacy_sm64ds_load_overlay{ov}',
            f'sm64ds_unload_overlay{ov}=legacy_sm64ds_unload_overlay{ov}']})
    fixture += ['}', 'struct TestCallbacks {unsigned bank;bool(*legacy_load)();bool(*legacy_unload)();bool(*load)();bool(*unload)();};',
                'const TestCallbacks test_callbacks[]={']
    for module in entries:
        ov = module['overlay']
        fixture.append(f'{{{ov},legacy_sm64ds_load_overlay{ov},legacy_sm64ds_unload_overlay{ov},sm64ds_load_overlay{ov},sm64ds_unload_overlay{ov}}},')
    (output/'native_actor_lifecycle_fixture.h').write_text('\n'.join(fixture+['};'])+'\n')
    for relative in ('unmatched/native_actor_lifecycle.h', 'unmatched/native_actor_lifecycle.cpp',
                     'unmatched/actor_registry.h', 'tests/test_native_actor_lifecycle.cpp'):
        source = Path(__file__).resolve().parents[1] / relative
        destination = output/'port'/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        hashes[str(source)] = sha(source)
    shutil.copy2(manifest_path, output/manifest_path.name)
    result = {'status': 'PREPARED_NOT_EXECUTED', 'legacy_objects': commands,
              'source_sha256': hashes, 'manifest_sha256': sha(manifest_path),
              'limits': ['Constructor/resource callbacks are observers, not actor runtime.',
                         'Native wrapper/ELF and gameplay runtime gates remain mandatory.']}
    (output/'preparation.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.root.resolve(), args.output.resolve()), indent=2))
