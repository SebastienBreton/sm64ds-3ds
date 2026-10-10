#!/usr/bin/env python3
"""Separate ROM import, source, ELF, registry admission and runtime evidence.

This is an offline inventory, never a native gameplay PASS. Object records use
the original loaders' mappings. Dynamic children and indirect calls are outside
this inventory. One source index and one numeric-data graph serve all 52 levels.
"""
import argparse
import collections
import hashlib
import json
import re
import shutil
from pathlib import Path

from elftools.elf.elffile import ELFFile
from all_level_data import collect
from audit_level_native_closure import analyze
from common_scenery_profiles import ROWS as COMMON_ROWS
from overlay_import_audit import audit


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def elf_symbols(path):
    result = collections.defaultdict(list)
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf['e_machine'] != 'EM_ARM':
            raise ValueError('Expected the actual ARM native ELF')
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            if not symbol.name or symbol['st_shndx'] == 'SHN_UNDEF':
                continue
            result[symbol.name].append({
                'binding': symbol['st_info']['bind'],
                'type': symbol['st_info']['type'],
                'address': hex(symbol['st_value']), 'size': symbol['st_size']})
    return result


def function_status(names, linked):
    entries = [{'symbol': name, **entry} for name in names
               for entry in linked.get(name, [])]
    strong = any(e['binding'] == 'STB_GLOBAL' and e['type'] == 'STT_FUNC'
                 for e in entries)
    weak = any(e['binding'] == 'STB_WEAK' and e['type'] == 'STT_FUNC'
               for e in entries)
    return {'status': 'STRONG_FUNCTION' if strong else 'WEAK_FUNCTION' if weak
            else 'NOT_IN_ELF', 'symbols': entries,
            'limit': 'Binding is not proof of original body, ABI or semantics; '
                     'absence can also result from linker garbage collection'}


def run(root, native_info, elf, runtime_reports=()):
    inventory = audit(root)
    numeric = collect(root)
    linked = elf_symbols(elf)
    infos = {int(ov): {'base': '0x'+base, 'bytes': int(size),
                      'numeric_bytes': int(data), 'kind': int(kind)}
             for ov, base, size, data, kind in re.findall(
                 r'\{(\d+),0x([0-9a-f]+),(\d+),(\d+),(\d+)\}',
                 native_info.read_text())}
    registered = {words[1] & 65535 for _, _, _, words in COMMON_ROWS}
    for row in inventory['overlays']:
        row['metadata'] = infos[row['overlay']]
        functions = [n for names in row['source_translation_units'].values()
                     for n in names] + row['original_source_missing']
        row['linked_function_counts'] = dict(collections.Counter(
            function_status([name], linked)['status'] for name in functions))
        for profile in row['profiles']:
            profile['factory_elf'] = function_status(
                profile['factory_symbol_candidates'], linked)
            profile['registry_admitted'] = (
                profile['actor_id'] in registered if row['overlay'] == 2
                else infos[row['overlay']]['kind'] != 0)
            profile['registration_scope'] = (
                'Common bootstrap or deferred Stage helper' if row['overlay'] == 2
                else 'Only while the original bank is loaded')
    levels = []
    all_actors = collections.defaultdict(set)
    for level in range(52):
        data = analyze(root, level, native_info, overlay_inventory=inventory,
                       numeric_levels=numeric)
        actors = {a['actor_id']: a['profiles']
                  for rows in data['actor_records_by_star_filter'].values()
                  for a in rows}
        statuses = {}
        for actor, profiles in actors.items():
            all_actors[actor].add(level)
            admitted = [p for p in profiles if p['registry_admitted']]
            bound = [p for p in admitted
                     if p['factory_elf']['status'] == 'STRONG_FUNCTION']
            statuses[str(actor)] = {
                'profile_candidates': profiles,
                'status': 'ADMITTED_STRONG_FACTORY' if bound else
                          'ADMITTED_NO_STRONG_FACTORY' if admitted else
                          'UNADMITTED_PROFILE' if profiles else
                          'NO_PROFILE_IN_REQUESTED_BANKS',
                'limit': 'Not proof of reachability, initialization or runtime'}
        levels.append({
            k: data[k] for k in ('level', 'level_overlay', 'level_root',
                                 'area_count', 'requested_overlays_original_order',
                                 'unsupported_overlays', 'numeric_import_spans',
                                 'provenance', 'limits')
        } | {'numeric_root_imported': True,
             'all_requested_lifecycles_admitted': not data['unsupported_overlays'],
             'actor_status_counts': dict(collections.Counter(
                 r['status'] for r in statuses.values())),
             'actor_profiles': statuses,
             'stage_controllers': data['stage_controller_ids'],
             'object_tables': data['tables'],
             'actor_records_by_star_filter': data['actor_records_by_star_filter']})
    module_map = {r['overlay']: r for r in inventory['overlays']}
    candidates = []
    for group in ([12, 13, 15, 79], [70, 77, 92]):
        modules = [module_map[n] for n in group]
        affected = [r['level'] for r in levels
                    if set(group) & set(r['unsupported_overlays'])]
        newly_admitted = [r['level'] for r in levels if r['unsupported_overlays']
                          and set(r['unsupported_overlays']) <= set(group)]
        sources = {p for r in modules for p in r['source_translation_units']}
        candidates.append({'overlays': group, 'source_translation_units': len(sources),
                           'functions': sum(r['original_function_count'] for r in modules),
                           'profiles': sum(len(r['profiles']) for r in modules),
                           'constructors': sum(len(r['constructors']) for r in modules),
                           'affected_levels': affected,
                           'newly_lifecycle_admitted_levels': newly_admitted,
                           'original_missing': [n for r in modules
                                                for n in r['original_source_missing']],
                           'limit': 'Projected lifecycle coverage only; source/link/ABI, '
                                    'dynamic children and runtime remain to be proved'})
    evidence = []
    for path in runtime_reports:
        report = json.loads(path.read_text())
        evidence.append({'path': str(path.resolve()), 'sha256': sha(path),
                         'report': report,
                         'limit': 'Evidence applies only to this report\'s checkpoints '
                                  'and route, not every mission or actor in a level'})
    return {'scope': 'OFFLINE_COVERAGE_NOT_RUNTIME_PASS',
            'summary': {'numeric_roots_imported': len(numeric),
                        'level_count': len(levels),
                        'all_requested_lifecycle_admitted_levels': [r['level'] for r in levels
                            if r['all_requested_lifecycles_admitted']],
                        'level_overlays_without_lifecycle': [r['level_overlay'] for r in levels
                            if infos[r['level_overlay']]['kind'] == 0],
                        'unique_actor_ids_in_enumerated_records': len(all_actors),
                        'common_manifest_profiles': len(registered)},
            'levels': levels, 'overlay_inventory': inventory,
            'candidate_batches': candidates, 'runtime_evidence': evidence,
            'provenance': {'arm9': sha(root/'extracted/arm9_dec.bin'),
                           'native_info': {'path': str(native_info), 'sha256': sha(native_info)},
                           'elf': {'path': str(elf), 'sha256': sha(elf)},
                           'tools': {p.name: sha(p) for p in [Path(__file__),
                               Path(__file__).with_name('audit_level_native_closure.py'),
                               Path(__file__).with_name('overlay_import_audit.py')]}},
            'limits': ['Static resident overlays0/1/2 do not require dynamic dispatcher admission.',
                       'Overlay6 is a reviewed partial minigame closure, owned by menu work.',
                       'Numeric imports exclude code and vtables. Strong ELF binding does not prove behavior.',
                       'Factories in unsupported banks may be absent after linker garbage collection.',
                       'All entrances and seven star filters are inventoried; original runtime selects subsets.',
                       'Actor-created children, arbitrary indirect calls and multiplayer are not closed.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--native-info', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--runtime-report', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Keep prior coverage archives immutable')
    result = run(args.root.resolve(), args.native_info.resolve(), args.elf.resolve(),
                 args.runtime_report)
    output.mkdir(parents=True)
    for name in ('audit_gameplay_coverage.py', 'audit_level_native_closure.py',
                 'overlay_import_audit.py'):
        shutil.copy2(Path(__file__).with_name(name), output/name)
    (output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['summary'], indent=2))
    print(json.dumps(result['candidate_batches'], indent=2))


if __name__ == '__main__':
    main()
