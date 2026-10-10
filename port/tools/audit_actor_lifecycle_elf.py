#!/usr/bin/env python3
"""Check actual ARM32 lifecycle descriptors, constructors, BSS and profiles.

An ELF audit is not native runtime proof. Constructor order and factory/profile
words are checked against the local ROM and the validated manifest.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from native_actor_lifecycle import modules, validate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(root, elf_path, metadata, manifest_path=None):
    manifest_path = manifest_path or Path(__file__).with_name('native_actor_overlay_manifest.json')
    manifest = json.loads(manifest_path.read_text())
    entries = modules(manifest)
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM'
        syms = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()
                if s['st_shndx'] != 'SHN_UNDEF'}

        def read(address, size):
            for section in elf.iter_sections():
                start, length = section['sh_addr'], section['sh_size']
                if section['sh_flags'] & 2 and start <= address and address+size <= start+length:
                    if section['sh_type'] == 'SHT_NOBITS':
                        return bytes(size)
                    return section.data()[address-start:address-start+size]
            raise ValueError(f'ELF range not found: {address:#x}+{size}')

        def words(address, count):
            return list(struct.unpack('<'+str(count)+'I', read(address, 4*count)))

        def address(name, function=False):
            symbol = syms[name]
            assert symbol['st_info']['bind'] == 'STB_GLOBAL', name
            if function:
                assert symbol['st_info']['type'] == 'STT_FUNC', name
            return symbol['st_value']

        metadata_rows = [list(map(lambda s: int(s, 16) if s.startswith('0x') else int(s), row))
                         for row in re.findall(r'\{(\d+),(0x[0-9a-f]+),(\d+),(\d+),(\d+)\}', metadata.read_text())]
        count = words(address('sm64ds_native_overlay_count'), 1)[0]
        actual = words(address('sm64ds_native_overlays'), count*5)
        assert actual == [v for row in metadata_rows for v in row], 'Generated metadata differs from linked metadata'
        infos = {row[0]: row for row in metadata_rows}
        count = words(address('sm64ds_actor_lifecycle_count'), 1)[0]
        assert count == len(entries)
        table = syms['sm64ds_actor_lifecycles']
        assert table['st_size'] == count*52, 'ARM32 descriptor layout changed'
        proofs = []
        for index, module in enumerate(entries):
            validate(root, module)
            fields = struct.unpack('<12IB3x', read(table['st_value']+52*index, 52))
            ov, kind, reset, bss, begin, end, ctors, ctor_count, ids, profiles, previous, profile_count, loaded = fields
            assert [ov, kind, reset, bss] == [module[k] for k in ('overlay', 'native_kind', 'reset_bytes', 'bss_bytes')]
            assert infos[ov][3:] == [reset, kind]
            assert end-begin == bss and loaded == 0
            if bss:
                assert begin == address(f'sm64ds_overlay{ov}_bss_begin')
                assert end == address(f'sm64ds_overlay{ov}_bss_end')
            else:
                assert begin == end == 0
            assert ctor_count == len(module['constructors'])
            assert words(ctors, ctor_count) == [address(n, True) for n in module['constructors']]
            assert profile_count == len(module['profiles'])
            assert words(ids, profile_count) == [a for a, _ in module['profiles']]
            assert words(profiles, profile_count) == [address(n) for _, n in module['profiles']]
            assert words(previous, profile_count) == [0]*profile_count
            cfg = root/f'config/arm9/overlays/ov{ov:03}/symbols.txt'
            original = {n: (int(a, 16), k) for n, k, a in re.findall(
                r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)', cfg.read_text(), re.M)}
            blob = (root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
            profile_proofs = []
            for actor, name in module['profiles']:
                retail = list(struct.unpack_from('<7I', blob, original[name][0]-module['base']))
                native = words(address(name), 7)
                candidates = [n for n, (at, k) in original.items()
                              if at == retail[0] and k.startswith('function')]
                assert any(native[0] == address(n, True) for n in candidates if n in syms)
                assert native[1:] == retail[1:] and native[1] & 65535 == actor
                profile_proofs.append({'actor': actor, 'profile': name,
                                       'factory_symbols': candidates, 'retail_words': retail,
                                       'native_words': native})
            proofs.append({'overlay': ov, 'kind': kind, 'reset_bytes': reset,
                           'bss_bytes': bss, 'constructors': module['constructors'],
                           'profiles': profile_proofs})
        for name in ('sm64ds_has_actor_lifecycle', 'sm64ds_load_actor_lifecycle',
                     'sm64ds_unload_actor_lifecycle'):
            address(name, True)
        pending = [ov for ov in (12, 13, 15, 79) if ov not in {m['overlay'] for m in entries}]
        assert all(infos[ov][4] == 0 for ov in pending), 'Bank absent from the audited manifest was admitted'
        roots = syms['data_02092208']
        assert roots['st_size'] == 52*4 and all(words(roots['st_value'], 52))
    return {'status': 'PASS_LINKED_ARM32_DESCRIPTORS_NOT_RUNTIME',
            'banks': proofs, 'linked_metadata_exact': True, 'numeric_roots_linked': 52,
            'unproved_banks_still_refused': pending,
            'elf_sha256': sha(elf_path), 'manifest_sha256': sha(manifest_path),
            'metadata_sha256': sha(metadata),
            'limits': ['Existing bank actor bodies are unchanged; this audit checks lifecycle/data bindings.',
                       'Host differential/SAN and ARM compilation are separate evidence.',
                       'No new actor or level runtime support is inferred; native lifecycle regression pending.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, help='Optional private manifest; production default is unchanged')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Keep previous linked audits immutable')
    result = audit(args.root.resolve(), args.elf.resolve(), args.metadata.resolve(),
                   args.manifest.resolve() if args.manifest else None)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(result['status'], len(result['banks']), 'banks')
