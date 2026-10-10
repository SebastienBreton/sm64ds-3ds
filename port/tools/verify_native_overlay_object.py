#!/usr/bin/env python3
"""Check private ARM data imports against independent ROM/config evidence.

Verifies every emitted data word, exact relocation target/addend and BSS
labels. This is an object gate, not a native runtime or actor ABI gate.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--overlay', type=int, required=True)
    p.add_argument('--object', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--contexts', type=Path)
    p.add_argument('--canonical-object', type=Path)
    a = p.parse_args()
    cfg = a.root/f'config/arm9/overlays/ov{a.overlay:03}'
    layout = (cfg/'delinks.txt').read_text()
    base = int(re.search(r'\.text\s+start:0x([0-9a-f]+)', layout)[1], 16)
    bss = re.search(r'\.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', layout)
    bstart, bend = (int(x, 16) for x in bss.groups())
    blob = (a.root/f'extracted/overlays/overlay_{a.overlay:04}.bin').read_bytes()
    assert base+len(blob) == bstart
    labels = {}
    for name, at in re.findall(r'^(\w+) kind:\S+ addr:0x([0-9a-f]+)',
                               (cfg/'symbols.txt').read_text(), re.M):
        labels.setdefault(int(at, 16), []).append(name)
    edges = {int(at, 16): (int(to, 16), module)
             for at, to, module in re.findall(
                 r'^from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:(\S+)',
                 (cfg/'relocs.txt').read_text(), re.M)}
    rows = []
    external = {int(r['from'], 16): r for r in json.loads(a.contexts.read_text())['edges']} if a.contexts else {}
    canonical = None
    if a.canonical_object:
        canonical_file = a.canonical_object.open('rb')
        canonical = ELFFile(canonical_file)
        canonical_symbols = {s.name: s for s in canonical.get_section_by_name('.symtab').iter_symbols()}
    with a.object.open('rb') as f:
        elf = ELFFile(f)
        assert elf['e_machine'] == 'EM_ARM' and elf.elfclass == 32
        symtab = elf.get_section_by_name('.symtab')
        symbols = {s.name: s for s in symtab.iter_symbols()}
        for idx, sec in enumerate(elf.iter_sections()):
            match = re.fullmatch(rf'\.data\.overlay{a.overlay}_([0-9a-f]+)', sec.name)
            if not match:
                continue
            start = int(match[1], 16)
            data = sec.data()
            assert len(data) % 4 == 0 and base <= start < start+len(data) <= bstart
            relsec = elf.get_section_by_name('.rel'+sec.name)
            relocs = {r['r_offset']: r for r in relsec.iter_relocations()} if relsec else {}
            expected = {at-start: target for at, target in edges.items()
                        if start <= at < start+len(data)}
            assert relocs.keys() == expected.keys(), (sec.name, 'edge set')
            for off in range(0, len(data), 4):
                actual = struct.unpack_from('<I', data, off)[0]
                original = struct.unpack_from('<I', blob, start+off-base)[0]
                if off in expected:
                    target, module = expected[off]
                    rel = relocs[off]
                    name = symtab.get_symbol(rel['r_info_sym']).name
                    if start+off in external:
                        context = external[start+off]
                        assert context['target'] == hex(target) and context['original_module'] == module
                        assert context['owner'] in map(int, re.findall(r'\d+', module))
                        assert name == context['symbol']
                        owner_cfg = a.root/f'config/arm9/overlays/ov{context["owner"]:03}'
                        owner_base = int(re.search(r'start:0x([0-9a-f]+)', (owner_cfg/'delinks.txt').read_text())[1], 16)
                        owner_blob = (a.root/f'extracted/overlays/overlay_{context["owner"]:04}.bin').read_bytes()
                        origin = owner_blob[target-owner_base:target-owner_base+context['bytes']]
                        assert origin[:4] == b'CLPS'
                        stride, count = struct.unpack_from('<HH', origin, 4)
                        assert stride == 8 and len(origin) == 8+8*count
                        assert hashlib.sha256(origin).hexdigest() == context['sha256']
                        if context['island']:
                            assert canonical is not None
                            symbol = canonical_symbols[name]
                            section = canonical.get_section(symbol['st_shndx'])
                            island = int(context['island'], 16)
                            assert section.name == f'.data.level_{context["owner"]}_{island:08x}'
                            assert symbol['st_value'] == target-island
                            assert section.data()[target-island:target-island+len(origin)] == origin
                    else:
                        assert module == f'overlay({a.overlay})'
                        assert name in labels[target]
                    assert original == target
                    assert rel['r_info_type'] == 2 and actual == 0, (sec.name, off, name)
                else:
                    assert actual == original, (sec.name, off, 'scalar word')
                for name in labels.get(start+off, []):
                    assert symbols[name]['st_shndx'] == idx
                    assert symbols[name]['st_value'] == off
            rows.append({'section': sec.name, 'rom_start': hex(start),
                         'bytes': len(data), 'relocations': len(relocs)})
        assert rows, 'no imported islands'
        begin = symbols[f'sm64ds_overlay{a.overlay}_bss_begin']
        end = symbols[f'sm64ds_overlay{a.overlay}_bss_end']
        assert begin['st_shndx'] == end['st_shndx'] and begin['st_value'] == 0
        assert end['st_value'] == bend-bstart
        assert elf.get_section(begin['st_shndx'])['sh_type'] == 'SHT_NOBITS'
        count = 0
        for at, names in labels.items():
            if bstart <= at < bend:
                for name in names:
                    assert symbols[name]['st_shndx'] == begin['st_shndx']
                    assert symbols[name]['st_value'] == at-bstart
                    count += 1
    report = {'status': 'PASS ROM/config/object data agreement only',
              'overlay': a.overlay, 'object': str(a.object),
              'object_sha256': hashlib.sha256(a.object.read_bytes()).hexdigest(),
              'rom_sha256': hashlib.sha256(blob).hexdigest(), 'islands': rows,
              'bss_bytes': bend-bstart, 'bss_labels': count,
              'limits': ['Unresolved targets checked by symbol identity; full link pending.',
                         'No native PMF invocation, resources or lifecycle runtime gate.']}
    if a.contexts:
        report['contexts_sha256'] = hashlib.sha256(a.contexts.read_bytes()).hexdigest()
        report['canonical_object_sha256'] = hashlib.sha256(a.canonical_object.read_bytes()).hexdigest()
        report['canonical_clps_aliases'] = sum(bool(r['island']) for r in external.values())
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(report['status'], a.overlay, sum(r['bytes'] for r in rows),
          'bytes', sum(r['relocations'] for r in rows), 'relocations')


if __name__ == '__main__':
    main()
