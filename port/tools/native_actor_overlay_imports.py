#!/usr/bin/env python3
"""ROM-checked numeric islands, native pointers and BSS for actor modules.

Every literal, relocation module/target, exported data label, profile registry
index and constructor sequence is checked. Shared CLPS pointers retain their
reviewed level owner and bytes. GCC supplies executable bodies and vtables.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from native_overlay_data import emit_bss


def symbols(root, overlay):
    config = (root / ('config/arm9/symbols.txt' if overlay is None else
              f'config/arm9/overlays/ov{overlay:03}/symbols.txt')).read_text()
    return {name: (int(address, 16), kind) for name, kind, address in
            re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)', config, re.M)}


def generate(root, overlay, manifest):
    module = next(m for m in manifest['modules'] if m['overlay'] == overlay)
    blob = (root / f'extracted/overlays/overlay_{overlay:04}.bin').read_bytes()
    assert hashlib.sha256(blob).hexdigest() == module['rom_sha256'], 'ROM changed'
    base = module['base']
    cfg = root / f'config/arm9/overlays/ov{overlay:03}'
    own = symbols(root, overlay)
    labels = {}
    for name, (address, kind) in own.items():
        labels.setdefault(address, []).append(name)
    edges = {int(a, 16): (int(t, 16), mod) for a, t, mod in re.findall(
        r'^from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:(\S+)',
        (cfg / 'relocs.txt').read_text(), re.M)}
    contexts = manifest['clps_contexts']
    for name, context in contexts.items():
        data = (root / f'extracted/overlays/overlay_{context["owner"]:04}.bin').read_bytes()
        offset = context['address'] - context['base']
        block = data[offset:offset+context['bytes']]
        assert hashlib.sha256(block).hexdigest() == context['sha256'], name
    lines = ['// Generated ROM-checked actor data; no executable bytes or DS vtables.',
             'asm(R"ACTOR_IMPORT(']
    for section in module['sections']:
        start = section['start']
        lines += [f'.pushsection .data.overlay{overlay}_{start:x},"aw",%progbits', '.balign 4']
        for index, word in enumerate(section['words']):
            address = start + 4 * index
            assert struct.unpack_from('<I', blob, address-base)[0] == word['value']
            for name in labels.get(address, []):
                assert own[name][1].startswith('data'), 'executable/vtable label in numeric island'
                lines += [f'.global {name}', name + ':']
            if 'resident_slot' in word:
                assert edges[address] == (word['value'], word['module'])
                label = word['resident_slot']
                assert label == f'sm64ds_resident_slot_{overlay}_{address:08x}'
                lines += [f'.global {label}', label+':', '.word 0']
            elif 'symbol' in word:
                target, dependency = word['value'], word['symbol']
                edge_target, edge_module = edges[address]
                assert edge_target == target
                if edge_module != word['module']:
                    # Multi-module relocation narrowed to the co-loaded bank.
                    single = re.fullmatch(r'overlay\((\d+)\)', word['module'])
                    assert single and single[1] in re.fullmatch(r'overlays\(([^)]+)\)', edge_module)[1].split(',')
                if dependency in contexts:
                    assert contexts[dependency]['address'] == target
                elif dependency in manifest['level_aliases']:
                    assert manifest['level_aliases'][dependency][1] == target
                else:
                    single = re.fullmatch(r'overlay\((\d+)\)', word['module'])
                    if word['module'] == 'main':
                        owner = None
                    elif single:
                        owner = int(single[1])
                    else:
                        # Multi-module relocation resolved by an explicit original identifier.
                        owner = int(re.fullmatch(r'(?:data|func)_ov(\d{3})_[0-9a-f]{8}', dependency)[1])
                        assert str(owner) in re.fullmatch(r'overlays\(([^)]+)\)', word['module'])[1].split(',')
                    assert symbols(root, owner)[dependency][0] == target
                lines.append('.word ' + dependency)
            else:
                assert address not in edges, 'unrelocated ROM pointer'
                lines.append(f'.word 0x{word["value"]:08x}')
            # Interior (halfword/byte) labels within this word.
            for off in (1, 2, 3):
                for name in labels.get(address+off, []):
                    assert own[name][1].startswith('data'), 'executable/vtable label in numeric island'
                    lines += [f'.global {name}', f'.set {name}, .-4+{off}']
        lines.append('.popsection')
    for name, address in module.get('bss_labels', []):
        assert base+len(blob) <= address < base+len(blob)+module['bss_bytes']
        labels.setdefault(address, []).append(name)
    if module['bss_bytes']:
        lines += emit_bss(overlay, labels, base+len(blob), base+len(blob)+module['bss_bytes'], True)
    lines.append(')ACTOR_IMPORT");')
    if 'profiles' in module:
        arm = (root / 'extracted/arm9_dec.bin').read_bytes()
        for actor, name in module['profiles']:
            address = own[name][0]
            assert struct.unpack_from('<I', arm, 0x2090864-0x2004000+4*actor)[0] == address
            assert struct.unpack_from('<I', blob, address-base+4)[0] & 65535 == actor
        constructors = [name for name in module['constructors']]
        def ctor_address(name):
            if name in own: return own[name][0]
            return int(re.fullmatch(r'__sinit_ov\d{3}_([0-9a-f]{8})', name)[1], 16)
        expected = [ctor_address(name) for name in constructors] + [0]
        vector = struct.pack('<' + str(len(expected)) + 'I', *expected)
        ctor = re.search(r'\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', (cfg / 'delinks.txt').read_text())
        if ctor:
            assert blob[int(ctor[1], 16)-base:int(ctor[1], 16)-base+len(vector)] == vector, 'constructor sequence changed'
        else:
            assert blob.count(vector) == 1, 'constructor sequence changed'
        lines += ['extern "C" {']
        lines += [f'extern unsigned char {name}[];' for _, name in module['profiles']]
        lines += [f'unsigned sm64ds_overlay{overlay}_ids[]={{' + ','.join(str(i) for i, _ in module['profiles']) + '};',
                  f'void* sm64ds_overlay{overlay}_profiles[]={{' + ','.join(n for _, n in module['profiles']) + '};', '}']
    if 'native_scene_profile' in module:
        profile = module['native_scene_profile']
        address = profile['address']
        assert own[profile['name']][0] == address
        assert own[profile['factory']][0] == profile['factory_address']
        assert struct.unpack_from('<IHH', blob, address-base) == (
            profile['factory_address'], profile['behavior'], profile['render'])
        assert edges[address] == (profile['factory_address'], f'overlay({overlay})')
        arm = (root / 'extracted/arm9_dec.bin').read_bytes()
        assert struct.unpack_from('<I', arm, 0x2090864-0x2004000+4*profile['actor'])[0] == address
        assert sum(4*len(s['words']) for s in module['sections']) == module['numeric_bytes']
        lines += ['extern "C" {', f"void* {profile['factory']}();",
                  'struct NativeSceneProfile {void*(*create)();unsigned short behavior,render;};',
                  'static_assert(sizeof(NativeSceneProfile)==8,"original scene profile ABI");',
                  f"NativeSceneProfile {profile['name']}={{{profile['factory']},{profile['behavior']},{profile['render']}}};"]
        # This reviewed SDK scratch is an ARM9 BSS word, not ROM executable
        # data or an overlay BSS region. Its original initial value is zero.
        for scratch in module.get('arm9_scratch_words', []):
            assert scratch['address'] >= 0x2004000+len(arm)
            assert symbols(root, None)[scratch['name']][0] == scratch['address']
            lines.append(f"unsigned {scratch['name']}=0;")
        lines.append('}')
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('overlay', type=int)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('native_actor_overlay_manifest.json').read_text())
    args.output.write_text(generate(args.root, args.overlay, manifest))
