#!/usr/bin/env python3
"""Prepare a private actor closure with explicitly proven resident CLPS slots.

The production manifest and CMake remain untouched. CLPS ownership must be
unique by ROM header/extent, not selected from an overlapping DS address.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import re
import shutil
from pathlib import Path

from all_level_data import collect
from native_actor_lifecycle import generate as lifecycle_generate, validate
from native_actor_overlay_imports import symbols
from overlay_clps_context import resolve
import common_scenery_profiles


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(root, plan_path, transverse_path, output):
    if output.exists():
        raise ValueError('Preserve previous private preparations')
    plan = json.loads(plan_path.read_text())
    production = Path(__file__).with_name('native_actor_overlay_manifest.json')
    manifest = json.loads(production.read_text())
    imports = []
    resident = []
    for candidate in plan['module_proposals']:
        module = copy.deepcopy(candidate)
        module.pop('status', None)
        module.pop('numeric_aliases_required', None)
        spans = [(s['start'], s['start']+4*len(s['words'])) for s in module['sections']]
        mappings, proofs = resolve(root, module['overlay'], spans)
        for section in module['sections']:
            for index, word in enumerate(section['words']):
                if 'UNRESOLVED_RELOCATION' not in word:
                    continue
                address = section['start'] + index*4
                if address not in mappings:
                    raise ValueError(f'Unproven resident slot {address:x}')
                target, original_module, _ = mappings[address]
                proof = next(p for p in proofs if int(p['from'], 16) == address)
                if not proof['island']:
                    raise ValueError('Resident provider needs canonical numeric storage')
                if (target, original_module) != (word['value'], word['UNRESOLVED_RELOCATION']):
                    raise ValueError('Original resident edge changed')
                label = f'sm64ds_resident_slot_{module["overlay"]}_{address:08x}'
                word.pop('UNRESOLVED_RELOCATION')
                word['resident_slot'] = label
                word['module'] = original_module
                proof |= {'bank': module['overlay'], 'slot_label': label,
                          'allowed_providers': [proof['owner']],
                          'read_extent': proof['bytes']}
                resident.append(proof)
        validate(root, module)
        imports.append(module)
    if len(resident) != len({(r['bank'], r['from']) for r in resident}):
        raise ValueError('Repeated resident slot')
    manifest['modules'] += imports
    manifest['level_aliases'].update(plan['level_aliases_proposed'])
    # BC_SWITCH's CLPS is concrete resident ov2 data, not a level alias.
    target = 0x0210d6f4
    original2 = root/'extracted/overlays/overlay_0002.bin'
    blob = original2.read_bytes(); base2 = 0x020ad660
    data = blob[target-base2:target-base2+16]
    if data[:8] != b'CLPS\x08\x00\x01\x00':
        raise ValueError('BC_SWITCH CLPS type/extent changed')
    import struct
    module2 = next(m for m in manifest['modules'] if m['overlay'] == 2)
    if any(s['start'] <= target < s['start']+4*len(s['words']) for s in module2['sections']):
        raise ValueError('BC_SWITCH CLPS already imported; review overlap')
    module2['sections'].append({'start': target, 'words': [
        {'value': w} for w in struct.unpack('<4I', data)]})
    output.mkdir(parents=True)
    tools = output/'tools';tools.mkdir()
    tool_names = ('all_level_data.py','native_actor_overlay_imports.py',
                  'native_actor_lifecycle.py','native_overlay_info.py')
    tool_hashes = {}
    for name in tool_names:
        source = Path(__file__).with_name(name)
        tool_hashes[name] = sha(source)
        shutil.copy2(source, tools/name)
    importer = tools/'native_actor_overlay_imports.py'
    text = importer.read_text()
    old = "            if 'symbol' in word:\n"
    new = """            if 'resident_slot' in word:
                assert edges[address] == (word['value'], word['module'])
                label = word['resident_slot']
                assert label == f'sm64ds_resident_slot_{overlay}_{address:08x}'
                lines += [f'.global {label}', label+':', '.word 0']
            elif 'symbol' in word:
"""
    if text.count(old) != 1:
        raise ValueError('Private importer adapter site changed')
    importer.write_text(text.replace(old,new))
    (tools/'native_actor_overlay_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    sources = dict(plan['original_tu_sha256'])
    sources.update(json.loads(transverse_path.read_text())['original_tu_sha256'])
    for source, expected in sources.items():
        if sha(root/source) != expected:
            raise ValueError('Original TU changed: '+source)
        destination = output/'original-tu'/source
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(root/source,destination)
    original_common = copy.deepcopy(common_scenery_profiles.ROWS)
    original_level_names = set(common_scenery_profiles.LEVEL_NAMES)
    original_external = set(common_scenery_profiles.EXTERNAL)
    original_initializers = list(common_scenery_profiles.INITIALIZERS)
    try:
        for initializer in plan.get('required_common_initializers', []):
            if initializer not in common_scenery_profiles.INITIALIZERS:
                common_scenery_profiles.INITIALIZERS.append(initializer)
        for row in plan['required_common_profiles']:
            name = row['symbol'].removeprefix('g_profile_')
            common_scenery_profiles.ROWS.append((name,row['factory_symbol_candidates'][0],
                int(row['address'],16),tuple(row['original_profile_words'])))
            common_scenery_profiles.LEVEL_NAMES.add(name)
            if name in ('BAR','OBJ_RED_FIRE','OBJ_BLUE_FIRE'):
                common_scenery_profiles.EXTERNAL.add(name)
        (output/'common_scenery_profiles.cpp').write_text(common_scenery_profiles.generate(root))
    finally:
        common_scenery_profiles.ROWS[:] = original_common
        common_scenery_profiles.LEVEL_NAMES.clear()
        common_scenery_profiles.LEVEL_NAMES.update(original_level_names)
        common_scenery_profiles.EXTERNAL.clear()
        common_scenery_profiles.EXTERNAL.update(original_external)
        common_scenery_profiles.INITIALIZERS[:] = original_initializers
    (output/'native_actor_lifecycles.cpp').write_text(lifecycle_generate(root,manifest))
    owned = output/'port/unmatched';owned.mkdir(parents=True)
    for name in ('native_actor_lifecycle.h','native_actor_lifecycle.cpp'):
        shutil.copy2(Path(__file__).resolve().parents[1] / 'unmatched'/name,owned/name)
    lifecycle = owned/'native_actor_lifecycle.cpp';text=lifecycle.read_text()
    text=text.replace('int sm64ds_set_overlay_scope(int);','int sm64ds_set_overlay_scope(int);\nbool sm64ds_prepare_actor_relocations(unsigned);\nvoid sm64ds_commit_actor_relocations(unsigned);\nvoid sm64ds_clear_actor_relocations(unsigned);')
    text=text.replace('    if (sm64ds_reset_level_data(id)', '    if (!sm64ds_prepare_actor_relocations(id)) return false;\n    if (sm64ds_reset_level_data(id)')
    text=text.replace('    const int previous =', '    sm64ds_commit_actor_relocations(id);\n    const int previous =')
    text=text.replace('    entry->loaded = false;', '    entry->loaded = false;\n    sm64ds_clear_actor_relocations(id);')
    lifecycle.write_text(text)
    result = {'status':'PREPARED_PRIVATE_NOT_LINKED','original_tu_sha256':sources,
              'manifest':str(tools/'native_actor_overlay_manifest.json'),
              'resident_slots':resident,'original_tools_sha256':tool_hashes,
              'production_manifest_sha256':sha(production),
              'plan_sha256':sha(plan_path),'transverse_plan_sha256':sha(transverse_path),
              'constraints':['Resident bank79 slots require provider15 in the initial admitted scope.',
                             'Bank79 in levels18/39 is not admitted by this preparation; their level overlays26/47 remain unsupported.',
                             'No production manifest/CMake, matched source, GPU, cadence or user-build changed.',
                             'ELF/data/lifecycle/runtime proof still required.']}
    (output/'preparation.json').write_text(json.dumps(result,indent=2)+'\n')
    shutil.copy2(Path(__file__),output/Path(__file__).name)
    return result


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--transverse',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=prepare(args.root.resolve(),args.plan.resolve(),args.transverse.resolve(),args.output.resolve())
    print(result['status'],len(result['original_tu_sha256']),'TUs',len(result['resident_slots']),'resident slots')
