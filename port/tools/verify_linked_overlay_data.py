#!/usr/bin/env python3
"""Verify retained imported data after the real native linker selected targets.

Unreferenced islands may be garbage-collected. Retained scalar words and every
native pointer must agree with the independently checked relocatable object.
This gate does not execute the actor or a PMF.
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
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--objects', type=Path, nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    rows = []
    with a.elf.open('rb') as f:
        linked = ELFFile(f)
        assert linked['e_machine'] == 'EM_ARM' and linked['e_type'] == 'ET_EXEC'
        live = {s.name: s for s in linked.get_section_by_name('.symtab').iter_symbols()}
        for path in a.objects:
            with path.open('rb') as o:
                elf = ELFFile(o)
                symtab = elf.get_section_by_name('.symtab')
                for index, section in enumerate(elf.iter_sections()):
                    if not re.fullmatch(r'\.data\.overlay\d+_[0-9a-f]+', section.name):
                        continue
                    labels = [s for s in symtab.iter_symbols() if s['st_shndx'] == index and s.name and s['st_info']['bind'] != 'STB_LOCAL']
                    present = [s for s in labels if s.name in live and isinstance(live[s.name]['st_shndx'], int)]
                    if not present:
                        rows.append({'object': str(path), 'section': section.name, 'status': 'GC_DROPPED'})
                        continue
                    assert len(present) == len(labels)
                    bases = {live[s.name]['st_value']-s['st_value'] for s in present}
                    assert len(bases) == 1
                    address = bases.pop()
                    owner = linked.get_section(live[present[0].name]['st_shndx'])
                    offset = address-owner['sh_addr']
                    data = owner.data()[offset:offset+section['sh_size']]
                    assert len(data) == section['sh_size']
                    relsec = elf.get_section_by_name('.rel'+section.name)
                    relocs = {r['r_offset']: r for r in relsec.iter_relocations()} if relsec else {}
                    for off in range(0, len(data), 4):
                        word = struct.unpack_from('<I', data, off)[0]
                        old = struct.unpack_from('<I', section.data(), off)[0]
                        if off in relocs:
                            rel = relocs[off]
                            target = symtab.get_symbol(rel['r_info_sym']).name
                            assert rel['r_info_type'] == 2 and target in live
                            assert word == (live[target]['st_value']+old) & 0xffffffff, (section.name, off, target)
                        else:
                            assert word == old, (section.name, off, 'scalar')
                    rows.append({'object': str(path), 'section': section.name, 'status': 'PASS',
                                 'bytes': len(data), 'native_pointer_words': len(relocs)})
    result = {'status': 'PASS retained imported data and selected native pointer targets only',
              'elf_sha256': hashlib.sha256(a.elf.read_bytes()).hexdigest(), 'islands': rows,
              'objects_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in a.objects},
              'limits': ['Object ROM/config proof is a separate prerequisite.',
                         'GC-dropped islands have only their earlier object gate.',
                         'No invocation, resources, dispatcher admission or gameplay proof.']}
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(result['status'], sum(r.get('bytes', 0) for r in rows), 'bytes',
          sum(r.get('native_pointer_words', 0) for r in rows), 'pointer words')


if __name__ == '__main__':
    main()
