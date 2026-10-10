#!/usr/bin/env python3
"""Prepare original actor TUs and bounded data candidates for closure review.

Unknown relocations, compiler-owned RTTI/vtables and ambiguous resource owners
fail closed. The proposal does not modify the production manifest or admit a
bank. Link/ABI/lifecycle and original actor runtime gates remain required.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path
from all_level_data import collect, reset_records


def sha(data):
    return hashlib.sha256(data).hexdigest()


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def symbols(root, owner):
    path = root/('config/arm9/symbols.txt' if owner is None else
                 f'config/arm9/overlays/ov{owner:03}/symbols.txt')
    return {n: (int(a, 16), k) for n, k, a in re.findall(
        r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)', path.read_text(), re.M)}


def prepare(root, coverage, ids, output):
    if output.exists():
        raise ValueError('Keep prior batch archives immutable')
    inventory = coverage['overlay_inventory']['overlays']
    selected = [next(r for r in inventory if r['overlay'] == ov) for ov in ids]
    if any(r['original_source_missing'] for r in selected):
        raise ValueError('Missing original source; no fallback generated')
    relevant_levels = [r for r in coverage['levels'] if r['unsupported_overlays']
                       and set(r['unsupported_overlays']) <= set(ids)]
    required_common = {int(a) for level in relevant_levels
                       for a, row in level['actor_profiles'].items()
                       if any(p['overlay'] == 2 and not p['registry_admitted']
                              for p in row['profile_candidates'])}
    common = next(r for r in inventory if r['overlay'] == 2)
    common_index = {name: path for path, names in common['source_translation_units'].items()
                    for name in names}
    common_rows = [r for r in common['profiles'] if r['actor_id'] in required_common]
    common_sources = {common_index[name] for row in common_rows
                      for name in row['factory_symbol_candidates']}
    sources = {path for row in selected for path in row['source_translation_units']} | common_sources
    texts = {path: clean((root/path).read_text()) for path in sources}
    all_text = '\n'.join(texts.values())
    identifiers = set(re.findall(r'\b\w+\b', all_text))
    # Explicit definitions remain in their original C++ TUs, avoiding duplicate
    # ownership of profiles such as the clock's two complete descriptors.
    definitions = set(re.findall(r'@symbol\s+(\w+)', '\n'.join(
        (root/path).read_text() for path in sources)))
    manifest = json.loads(Path(__file__).with_name('native_actor_overlay_manifest.json').read_text())
    numeric = collect(root)
    resets = reset_records(root, numeric)
    numeric_spans = {ov: spans for ov, _, blob, spans, _ in numeric if blob is not None}
    proposals, aliases, external, unresolved = [], {}, [], []
    for ordinal, row in enumerate(selected):
        ov = row['overlay']
        own = symbols(root, ov)
        labels = {}
        for name, (at, kind) in own.items():
            labels.setdefault(at, []).append((name, kind))
        cfg = root/f'config/arm9/overlays/ov{ov:03}'
        delinks = (cfg/'delinks.txt').read_text()
        base = int(re.search(r'\.text\s+start:0x([0-9a-f]+)', delinks)[1], 16)
        blob = (root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
        end = base+len(blob)
        boundaries = sorted(set(labels) | {end})
        # Interior data_N labels are aliases, not array ends. In particular
        # KabeFiles has six pointers but labels after its first word. Retain
        # its whole run up to the next function/profile/RTTI/vtable boundary.
        # These are candidates requiring extent review, never automatic PASS.
        semantic_boundaries = {end}
        for at, values in labels.items():
            for name, kind in values:
                if kind.startswith('function') or name.startswith(('_ZTV', '_ZTI', '_ZTS', 'g_profile_')):
                    semantic_boundaries.add(at)
                    size = re.search(r'size=0x([0-9a-f]+)', kind)
                    if size:
                        semantic_boundaries.add(at+int(size[1], 16))
        ctor = re.search(r'\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', delinks)
        if ctor:
            semantic_boundaries.update(int(v, 16) for v in ctor.groups())
        edges = {int(a, 16): (int(t, 16), module) for a, t, module in re.findall(
            r'^from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:(\S+)',
            (cfg/'relocs.txt').read_text(), re.M)}
        dependency_cache = {}
        selected_spans = set()
        queue = [n for n in identifiers & own.keys()
                 if own[n][1].startswith('data') and n.startswith('data_')]
        queue += [r['symbol'] for r in row['profiles'] if r['symbol'] not in definitions]
        visited = set()

        def dependency(address, target, module):
            key = (address, target, module)
            if key in dependency_cache:
                return dependency_cache[key]
            owners = ([None] if module == 'main' else
                      [int(re.fullmatch(r'overlay\((\d+)\)', module)[1])]
                      if module.startswith('overlay(') else
                      [int(n) for n in re.fullmatch(r'overlays\(([^)]+)\)', module)[1].split(',')])
            matches = [(owner, n, k) for owner in owners
                       for n, (at, k) in symbols(root, owner).items() if at == target]
            explicit = [r for r in matches if r[1] in identifiers]
            choices = explicit if explicit else matches if len(owners) == 1 else []
            if not choices:
                unresolved.append({'overlay': ov, 'from': hex(address),
                                   'target': hex(target), 'module': module,
                                   'reason': 'No uniquely reviewed resource/symbol owner',
                                   'candidates': matches})
                dependency_cache[key] = None
                return None
            # Multiple aliases at one owned address have the same pointer
            # identity; prefer the spelling explicitly used by original TUs.
            owner, name, kind = sorted(choices, key=lambda r: r[1])[0]
            if len({r[0] for r in choices}) != 1 or name.startswith(('_ZTV', '_ZTI', '_ZTS')):
                unresolved.append({'overlay': ov, 'from': hex(address),
                                   'reason': 'Ambiguous owner or compiler-owned RTTI/vtable',
                                   'candidates': choices})
                dependency_cache[key] = None
                return None
            if owner == ov and kind.startswith('data') and name not in definitions:
                queue.append(name)
            elif owner != ov and kind.startswith('data'):
                external.append({'overlay': ov, 'from': hex(address), 'owner': owner,
                                 'symbol': name, 'address': hex(target), 'module': module,
                                 'status': 'REVIEW_PROVIDER_AND_IDENTITY'})
            dependency_cache[key] = {'value': target, 'symbol': name, 'module': module}
            return dependency_cache[key]

        while queue:
            name = queue.pop()
            if name in visited:
                continue
            visited.add(name)
            at, kind = own[name]
            if kind == 'bss' or at >= end:
                continue
            if name.startswith(('_ZTV', '_ZTI', '_ZTS')):
                unresolved.append({'overlay': ov, 'symbol': name,
                                   'reason': 'Compiler owns RTTI/vtable; cannot emit retail words'})
                continue
            if any(lo <= at < hi for lo, hi in numeric_spans.get(ov, [])):
                aliases[name] = [ov, at]
                continue
            stop = at+28 if name.startswith('g_profile_') else min(
                b for b in semantic_boundaries if b > at)
            if stop > end or at % 4 or stop % 4:
                unresolved.append({'overlay': ov, 'symbol': name,
                                   'reason': 'Unreviewed non-word data extent'})
                continue
            if any(k.startswith('function') or n.startswith(('_ZTV', '_ZTI', '_ZTS'))
                   for a, values in labels.items() if at <= a < stop for n, k in values):
                unresolved.append({'overlay': ov, 'symbol': name,
                                   'reason': 'Data extent crosses executable/RTTI/vtable label'})
                continue
            selected_spans.add((at, stop))
            for address, (target, module) in edges.items():
                if at <= address < stop:
                    dependency(address, target, module)
        merged = []
        for lo, hi in sorted(selected_spans):
            if merged and lo <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(hi, merged[-1][1]))
            else:
                merged.append((lo, hi))
        sections = []
        for lo, hi in merged:
            words = []
            for address in range(lo, hi, 4):
                value = struct.unpack_from('<I', blob, address-base)[0]
                if address in edges:
                    target, module = edges[address]
                    if value != target:
                        raise ValueError('Original relocation value mismatch')
                    word = dependency(address, target, module)
                    if word is None:
                        word = {'value': value, 'UNRESOLVED_RELOCATION': module}
                else:
                    word = {'value': value}
                words.append(word)
            sections.append({'start': lo, 'words': words})
        bss = row['bss_range']
        proposals.append({'overlay': ov, 'native_kind': 24+ordinal, 'base': base,
                          'rom_sha256': sha(blob),
                          'bss_bytes': int(bss[1], 0)-int(bss[0], 0) if bss else 0,
                          'reset_bytes': sum(hi-lo for owner, lo, hi, *_ in resets if owner == ov),
                          'constructors': row['constructors'],
                          'profiles': [[r['actor_id'], r['symbol']] for r in row['profiles']],
                          'sections': sections,
                          'numeric_aliases_required': {n: v for n, v in aliases.items() if v[0] == ov},
                          'status': 'PROPOSED_REVIEW_AND_LINK_REQUIRED'})
    output.mkdir(parents=True)
    copied = {}
    for name in sorted(sources):
        destination = output/'original-tu'/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, destination)
        copied[name] = sha((root/name).read_bytes())
    common_blob = (root/'extracted/overlays/overlay_0002.bin').read_bytes()
    for row in common_rows:
        row['original_profile_words'] = list(struct.unpack_from(
            '<7I', common_blob, int(row['address'], 0)-0x020ad660))
    result = {'status': 'PREPARED_NOT_ADMITTED_NOT_LINKED',
              'coverage_source_scope': coverage['scope'], 'overlays': ids,
              'candidate_level_ids': [r['level'] for r in relevant_levels],
              'required_common_profiles': common_rows,
              'original_tu_sha256': copied, 'module_proposals': proposals,
              'level_aliases_proposed': aliases,
              'external_dependencies': external, 'unresolved': unresolved,
              'limits': ['Original source availability and proposed data extents are not ABI/link/runtime proof.',
                         'Data extents preserve interior aliases up to semantic boundaries; each extent still requires review.',
                         'Common actor constructor/data dependencies and dynamic children still require closure.',
                         'Ambiguous resource identities require review; no address-only binding is admitted.',
                         'Proposals do not modify production manifests or dispatcher admission.']}
    (output/'batch-plan.json').write_text(json.dumps(result, indent=2)+'\n')
    shutil.copy2(Path(__file__), output/Path(__file__).name)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--coverage', type=Path, required=True)
    parser.add_argument('--overlays', default='12,13,15,79')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.root.resolve(), json.loads(args.coverage.read_text()),
                     list(map(int, args.overlays.split(','))), args.output.resolve())
    print(json.dumps({'sources': len(result['original_tu_sha256']),
                      'required_common_ids': [r['actor_id'] for r in result['required_common_profiles']],
                      'unresolved_count': len(result['unresolved']),
                      'modules': [{k: m[k] for k in ('overlay', 'native_kind', 'reset_bytes', 'bss_bytes')}
                                  | {'data_bytes': sum(4*len(s['words']) for s in m['sections'])}
                                  for m in result['module_proposals']]}, indent=2))
