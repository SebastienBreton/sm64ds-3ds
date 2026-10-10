#!/usr/bin/env python3
"""Generate actor lifecycle descriptors from a ROM-validated import manifest.

This does not admit unknown banks or manufacture factories. Code, data, ABI,
resources and native runtime require their separate gates before enrollment.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def modules(manifest):
    entries = [m for m in manifest['modules'] if 'profiles' in m]
    ids = [m['overlay'] for m in entries]
    kinds = [m['native_kind'] for m in entries]
    if len(ids) != len(set(ids)) or len(kinds) != len(set(kinds)):
        raise ValueError('Duplicate actor overlay ID or native kind')
    if any(not 8 <= ov < 104 for ov in ids):
        raise ValueError('Actor bank ID collides with static/menu overlays or exceeds ROM table')
    if any(k < 19 for k in kinds):
        raise ValueError('Actor kind collides with existing static/menu lifecycles')
    if any(not m['profiles'] for m in entries):
        raise ValueError('Actor descriptor requires at least one reviewed profile')
    return entries


def validate(root, module):
    ov = module['overlay']
    cfg = root/f'config/arm9/overlays/ov{ov:03}'
    blob = (root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
    if hashlib.sha256(blob).hexdigest() != module['rom_sha256']:
        raise ValueError(f'overlay{ov}: ROM hash changed')
    symbol_text = (cfg/'symbols.txt').read_text()
    symbols = {n: (int(a, 16), k) for n, k, a in re.findall(
        r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)', symbol_text, re.M)}
    delinks = (cfg/'delinks.txt').read_text()
    base = int(re.search(r'\.text\s+start:0x([0-9a-f]+)', delinks)[1], 16)
    if base != module['base']:
        raise ValueError(f'overlay{ov}: base changed')
    bss = re.search(r'\.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', delinks)
    start, end = [int(v, 16) for v in bss.groups()] if bss else (base+len(blob),)*2
    if start != base+len(blob) or end-start != module['bss_bytes']:
        raise ValueError(f'overlay{ov}: BSS changed')
    ctor = re.search(r'\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', delinks)
    if ctor:
        begin, end = [int(v, 16) for v in ctor.groups()]
        addresses = [symbols[n][0] if n in symbols else
                     int(re.fullmatch(r'__sinit_ov\d{3}_([0-9a-f]{8})', n)[1], 16)
                     for n in module['constructors']]
        if end-begin != 4*len(addresses):
            raise ValueError(f'overlay{ov}: constructor count changed')
        original = list(struct.unpack_from('<'+str(len(addresses)+1)+'I', blob, begin-base))
        if original != addresses+[0]:
            raise ValueError(f'overlay{ov}: constructor order/terminator changed')
    elif module['constructors']:
        raise ValueError(f'overlay{ov}: no original constructor section')
    arm = (root/'extracted/arm9_dec.bin').read_bytes()
    seen = set()
    for actor, name in module['profiles']:
        if actor in seen or not 0 <= actor < 512:
            raise ValueError(f'overlay{ov}: invalid/duplicate profile ID')
        seen.add(actor)
        address = symbols[name][0]
        factory, packed = struct.unpack_from('<2I', blob, address-base)
        registry = struct.unpack_from('<I', arm, 0x02090864-0x02004000+4*actor)[0]
        if registry != address or packed & 65535 != actor:
            raise ValueError(f'overlay{ov}: profile identity changed')
        if not any(at == factory and kind.startswith('function')
                   for at, kind in symbols.values()):
            raise ValueError(f'overlay{ov}: unknown original factory')


def generate(root, manifest):
    entries = modules(manifest)
    lines = ['// ROM-validated descriptors; executable bodies remain original TUs.',
             '#include "unmatched/native_actor_lifecycle.h"', 'extern "C" {']
    for module in entries:
        validate(root, module)
        ov = module['overlay']
        if module['bss_bytes']:
            lines.append(f'extern unsigned char sm64ds_overlay{ov}_bss_begin[],sm64ds_overlay{ov}_bss_end[];')
        lines += [f'extern unsigned char {name}[];' for _, name in module['profiles']]
        lines += [f'void {name}();' for name in module['constructors']]
    lines += ['}', 'namespace {']
    for module in entries:
        ov = module['overlay']
        profiles = module['profiles']
        lines += [f'const unsigned ids{ov}[]={{'+','.join(str(i) for i, _ in profiles)+'};',
                  f'void* const profiles{ov}[]={{'+','.join(n for _, n in profiles)+'};',
                  f'void* previous{ov}[{len(profiles)}];',
                  f'void (*const constructors{ov}[])()={{'+(','.join(module['constructors']) or 'nullptr')+'};']
    lines += ['}', 'extern "C" {', 'NativeActorLifecycle sm64ds_actor_lifecycles[]={']
    for module in entries:
        ov = module['overlay']
        values = [str(ov), str(module['native_kind']), str(module['reset_bytes']),
                  str(module['bss_bytes']), f'sm64ds_overlay{ov}_bss_begin' if module['bss_bytes'] else 'nullptr',
                  f'sm64ds_overlay{ov}_bss_end' if module['bss_bytes'] else 'nullptr', f'constructors{ov}',
                  str(len(module['constructors'])), f'ids{ov}', f'profiles{ov}',
                  f'previous{ov}', str(len(module['profiles'])), 'false']
        lines.append('    {'+','.join(values)+'},')
    lines += ['};', 'extern const unsigned sm64ds_actor_lifecycle_count='
              'sizeof(sm64ds_actor_lifecycles)/sizeof(sm64ds_actor_lifecycles[0]);']
    # Preserve the existing public callback symbols used by archived probes.
    for module in entries:
        ov, kind = module['overlay'], module['native_kind']
        for operation in ('load', 'unload'):
            lines.append(f'bool sm64ds_{operation}_overlay{ov}(){{return '
                         f'sm64ds_{operation}_actor_lifecycle({ov},{kind});}}')
    return '\n'.join(lines+['}'])+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('native_actor_overlay_manifest.json'))
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if args.plan:
        modules(manifest)
        print(json.dumps({'imports': [m['overlay'] for m in manifest['modules']],
                          'actor_lifecycles': [m['overlay'] for m in modules(manifest)]}))
    else:
        if not args.output:
            parser.error('--output is required without --plan')
        args.output.write_text(generate(args.root.resolve(), manifest))


if __name__ == '__main__':
    main()
