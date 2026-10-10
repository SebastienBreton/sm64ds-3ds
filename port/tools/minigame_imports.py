#!/usr/bin/env python3
"""Check the shared native minigame manifest against retail profiles and factories.

This checks identity and initializer order, not completeness of a game's imported
code/data or its gameplay. Unsupported retail profiles are emitted as an audit
catalog so the next import starts with the actual actor/factory correspondence.
"""
import argparse
import json
import re
import struct
from pathlib import Path

BASE = 0x020bfec0
ARM9_BASE = 0x02004000
PROFILE_TABLE = 0x02090864


def audit(root):
    manifest = (Path(__file__).resolve().parents[1] / 'menu/minigame_imports.inc').read_text()
    rows = re.findall(r'^SM64DS_MINIGAME\((\d+),\s*(\w+),\s*(0x[0-9a-f]+),\s*'
                      r'(0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*"([^"]+)"\)$', manifest, re.M)
    inits = re.findall(r'^SM64DS_MINIGAME_INIT\((\w+),\s*(0x[0-9a-f]+)\)$', manifest, re.M)
    if not rows or len(rows) != manifest.count('\nSM64DS_MINIGAME('):
        raise ValueError('malformed or empty minigame import manifest')
    if len(inits) != manifest.count('\nSM64DS_MINIGAME_INIT('):
        raise ValueError('malformed initializer manifest')
    if len({r[0] for r in rows}) != len(rows) or len({r[1] for r in rows}) != len(rows):
        raise ValueError('duplicate imported actor or factory')
    cfg = root/'config/arm9/overlays/ov006'
    symbols = {}
    for name, kind, address in re.findall(r'^(\w+) kind:(\S+) addr:(0x[0-9a-f]+)',
                                         (cfg/'symbols.txt').read_text(), re.M):
        symbols[name] = (kind, int(address, 16))
    blob = (root/'extracted/overlays/overlay_0006.bin').read_bytes()
    arm9 = (root/'extracted/arm9_dec.bin').read_bytes()
    dependencies = set((Path(__file__).resolve().parents[1] / 'menu/wanted_dependencies.txt').read_text().splitlines())
    bindings = set((Path(__file__).resolve().parents[1] / 'menu/wanted_native_bindings.txt').read_text().splitlines())
    imports = []
    for actor_s, factory, profile_s, factory_s, vtable_s, label in rows:
        actor, profile, address, vtable = int(actor_s), int(profile_s, 16), int(factory_s, 16), int(vtable_s, 16)
        actual_profile = struct.unpack_from('<I', arm9, PROFILE_TABLE+actor*4-ARM9_BASE)[0]
        if actual_profile != profile or not BASE <= profile <= BASE+len(blob)-8:
            raise ValueError(f'{label}: retail actor {actor} profile changed')
        if struct.unpack_from('<IHH', blob, profile-BASE) != (address, actor, actor):
            raise ValueError(f'{label}: retail factory/behavior/render changed')
        kind, symbol_address = symbols.get(factory, ('', 0))
        size = re.fullmatch(r'function\(arm,size=(0x[0-9a-f]+)\)', kind)
        if symbol_address != address or not size:
            raise ValueError(f'{label}: factory symbol changed')
        factory_size = int(size[1], 16)
        words = struct.unpack_from('<'+str(factory_size//4)+'I', blob, address-BASE)
        factory_vtable_evidence = {'kind': 'direct_literal', 'function': hex(address)}
        if vtable not in words:
            # A retail factory may allocate and call a separately enrolled C2.
            # Inspect only direct ARM BL callees with known symbol extents.
            # No source rewrite or inferred native slot replaces ROM evidence.
            callees = []
            function_sizes = {a: int(m[1],16) for k,a in symbols.values()
                              if (m := re.fullmatch(r'function\(arm,size=(0x[0-9a-f]+)\)',k))}
            for index, word in enumerate(words):
                if word & 0x0f000000 != 0x0b000000:
                    continue
                displacement = word & 0xffffff
                if displacement & 0x800000: displacement -= 0x1000000
                target = address + index*4 + 8 + displacement*4
                size = function_sizes.get(target)
                if not size or not BASE <= target < target+size <= BASE+len(blob):
                    continue
                callee_words = struct.unpack_from('<'+str(size//4)+'I',blob,target-BASE)
                if vtable in callee_words:
                    callees.append(target)
            if len(set(callees)) != 1:
                raise ValueError(f'{label}: unique retail factory/C2 vtable evidence missing')
            factory_vtable_evidence = {'kind':'direct_BL_callee_literal', 'factory':hex(address),
                                      'callee':hex(callees[0]), 'vtable':hex(vtable)}
        class_name = factory.removesuffix('_classInit')
        native_vtable = '_ZTV'+str(len(class_name))+class_name
        if symbols.get(native_vtable, ('', 0))[1] != vtable:
            raise ValueError(f'{label}: retail vtable symbol identity changed')
        if factory not in dependencies:
            raise ValueError(f'{label}: source generation does not include factory root')
        imports.append({'actor': actor, 'name': label, 'factory': factory,
                        'profile': hex(profile), 'factory_address': hex(address),
                        'factory_bytes': factory_size, 'vtable': hex(vtable),
                        'explicit_storage_binding': native_vtable in bindings, 'vtable_evidence': factory_vtable_evidence})
    ctor_match = re.search(r'^\s+\.ctor\s+start:(0x[0-9a-f]+) end:(0x[0-9a-f]+) kind:',
                           (cfg/'delinks.txt').read_text(), re.M)
    if not ctor_match:
        raise ValueError('missing retail constructor section')
    begin, end = (int(v, 16) for v in ctor_match.groups())
    # delinks excludes the trailing NULL word; the ROM overlay descriptor
    # includes it. Validate that terminator separately from callable entries.
    constructors = struct.unpack_from('<'+str((end-begin)//4)+'I', blob, begin-BASE)
    if not constructors or 0 in constructors or struct.unpack_from('<I', blob, end-BASE)[0] != 0:
        raise ValueError('retail constructor table terminator changed')
    initializer_records = []
    previous = -1
    for name, address_s in inits:
        address = int(address_s, 16)
        if symbols.get(name, ('', 0))[1] != address or address not in constructors:
            raise ValueError(f'{name}: initializer identity changed')
        index = constructors.index(address)
        if index <= previous or name not in dependencies:
            raise ValueError(f'{name}: initializer order/source dependency missing')
        previous = index
        initializer_records.append({'function': name, 'address': hex(address), 'retail_index': index})
    factories = {a: n for n, (kind, a) in symbols.items()
                 if n.endswith('_classInit') and kind.startswith('function')}
    imported_ids = {r['actor'] for r in imports}
    catalog = []
    for actor in range(0x187):
        profile = struct.unpack_from('<I', arm9, PROFILE_TABLE+actor*4-ARM9_BASE)[0]
        if not BASE <= profile <= BASE+len(blob)-8:
            continue
        factory, behavior, render = struct.unpack_from('<IHH', blob, profile-BASE)
        if factory not in factories:
            continue
        catalog.append({'actor': actor, 'factory': factories[factory], 'profile': hex(profile),
                        'behavior': behavior, 'render': render, 'imported': actor in imported_ids})
    return {'status': 'IDENTITY_CHECKED_ONLY', 'imports': imports,
            'initializers': initializer_records, 'retail_catalog': catalog}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.root)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"ROM-verified {len(result['imports'])} imported profiles, "
          f"{len(result['initializers'])} initializers; catalog {len(result['retail_catalog'])} retail profiles")
