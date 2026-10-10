#!/usr/bin/env python3
"""Review native _ZTV references without assuming every table has a GCC prefix.

The linked ELF proves offset-to-top/RTTI words. Relocations and masked function
bodies distinguish selected code from discarded alternatives. This is an audit,
not a proof that arbitrary stores or their virtual-call exposure are correct.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def symbol_bytes(elf, symbol):
    section = elf.get_section(symbol['st_shndx'])
    offset = symbol['st_value'] - section['sh_addr']
    return section.data()[offset:offset + symbol['st_size']]


def inputs(command, ar):
    for path in dict.fromkeys(v for v in command if v.endswith(('.o', '.a'))):
        if path.endswith('.o'):
            yield path, Path(path).read_bytes()
            continue
        names = subprocess.run(['rtk', 'proxy', ar, 't', path], check=True,
                               capture_output=True, text=True).stdout.splitlines()
        for member in names:
            data = subprocess.run(['rtk', 'proxy', ar, 'p', path, member],
                                  check=True, capture_output=True).stdout
            if data.startswith(b'\x7fELF'):
                yield path + '(' + member + ')', data


def audit(elf_path, command, ar):
    with elf_path.open('rb') as stream:
        final = ELFFile(stream)
        final_symbols = {s.name: s for s in final.get_section_by_name('.symtab').iter_symbols()}
        tables = {}
        for name, symbol in final_symbols.items():
            if not name.startswith('_ZTV') or not isinstance(symbol['st_shndx'], int):
                continue
            data = symbol_bytes(final, symbol)
            words = list(struct.unpack_from('<' + str(min(4, len(data) // 4)) + 'I', data))
            rtti = final_symbols.get(name.replace('_ZTV', '_ZTI', 1))
            gcc = len(words) >= 2 and rtti is not None and words[:2] == [0, rtti['st_value']]
            tables[name] = {'address': symbol['st_value'], 'bytes': len(data),
                            'words': words, 'gcc_offset_top_rtti_prefix': gcc}
        rows = []
        count = 0
        for identity, data in inputs(command, ar):
            count += 1
            obj = ELFFile(io.BytesIO(data))
            symbols = obj.get_section_by_name('.symtab')
            if symbols is None:
                continue
            for relocations in obj.iter_sections():
                if not isinstance(relocations, RelocationSection):
                    continue
                section = obj.get_section(relocations['sh_info'])
                if not section['sh_flags'] & 2:  # SHF_ALLOC; debug references aren't stores.
                    continue
                refs = []
                for reloc in relocations.iter_relocations():
                    name = symbols.get_symbol(reloc['r_info_sym']).name
                    if not name.startswith('_ZTV'):
                        continue
                    offset = reloc['r_offset']
                    addend = struct.unpack_from('<I', section.data(), offset)[0] if reloc['r_info_type'] == 2 else None
                    refs.append({'table': name, 'type': reloc['r_info_type'],
                                 'offset': offset, 'addend': addend,
                                 'linked_table': tables.get(name)})
                if not refs:
                    continue
                selected = []
                for symbol in symbols.iter_symbols():
                    if symbol['st_info']['type'] != 'STT_FUNC' or not isinstance(symbol['st_shndx'], int):
                        continue
                    if obj.get_section(symbol['st_shndx']).name != section.name or symbol['st_value'] != 0:
                        continue
                    target = final_symbols.get(symbol.name)
                    if target is None or target['st_size'] != section['sh_size']:
                        continue
                    actual = bytearray(symbol_bytes(final, target))
                    expected = bytearray(section.data())
                    for reloc in relocations.iter_relocations():
                        offset = reloc['r_offset']
                        actual[offset:offset + 4] = expected[offset:offset + 4] = bytes(4)
                    if actual == expected:
                        selected.append(symbol.name)
                rows.append({'input': identity, 'input_sha256': hashlib.sha256(data).hexdigest(),
                             'section': section.name, 'selected_functions': selected, 'refs': refs})
    candidates = [dict(input=r['input'], section=r['section'], functions=r['selected_functions'], ref=v)
                  for r in rows if r['selected_functions'] for v in r['refs']
                  if v['type'] == 2 and v['addend'] == 0 and v['linked_table']
                  and v['linked_table']['gcc_offset_top_rtti_prefix']]
    return {'status': 'AUDIT_REFERENCES_ONLY', 'elf_sha256': hashlib.sha256(elf_path.read_bytes()).hexdigest(),
            'input_objects_and_archive_members': count, 'tables': tables, 'rows': rows,
            'selected_zero_prefix_review_candidates': candidates,
            'limits': ['A zero addend is a review candidate; subsequent instructions may add the address-point offset.',
                       'Data references and non-GCC-prefix tables are not classified as incorrect stores.',
                       'Implicit SDK archives and computed stores without symbolic _ZTV references are outside this audit.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--link-command', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ar', default='/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar')
    args = parser.parse_args()
    result = audit(args.elf, json.loads(args.link_command.read_text()), args.ar)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'], result['input_objects_and_archive_members'], 'inputs,',
          len(result['selected_zero_prefix_review_candidates']), 'selected zero-prefix candidates')


if __name__ == '__main__':
    main()
