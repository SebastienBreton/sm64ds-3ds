#!/usr/bin/env python3
"""Private section-reference graph, distinct from GNU ARM unwind retention.

This identifies code/data referenced by explicit entry/profile roots. It is not
a final-link proof: exceptions, COMDAT selection and calls back from native
objects outside this graph still require the actual complete native link.
"""
import argparse
import hashlib
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--link-report', type=Path, required=True)
    p.add_argument('--roots', nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    report = json.loads(a.link_report.read_text())
    nodes, definitions = {}, {}
    paths = [Path(r['path']) for r in report['inputs']]
    for path in paths:
        with path.open('rb') as f:
            elf = ELFFile(f)
            symtab = elf.get_section_by_name('.symtab')
            for index, sec in enumerate(elf.iter_sections()):
                if sec['sh_flags'] & 2 and not sec.name.startswith(('.ARM.exidx', '.ARM.extab')):
                    nodes[(str(path), index)] = {'section': sec.name, 'references': [], 'symbols': []}
            for symbol in symtab.iter_symbols():
                node = (str(path), symbol['st_shndx'])
                if node in nodes and symbol.name:
                    nodes[node]['symbols'].append(symbol.name)
                    if symbol['st_info']['bind'] != 'STB_LOCAL':
                        definitions.setdefault(symbol.name, []).append(node)
            for sec in elf.iter_sections():
                if sec['sh_type'] != 'SHT_REL':
                    continue
                node = (str(path), sec['sh_info'])
                if node not in nodes:
                    continue
                for rel in sec.iter_relocations():
                    symbol = symtab.get_symbol(rel['r_info_sym'])
                    local = (str(path), symbol['st_shndx'])
                    nodes[node]['references'].append((symbol.name, local if local in nodes else None))
    pending = []
    for root in a.roots:
        assert root in definitions, root
        pending.extend(definitions[root])
    visited, unresolved = set(), {}
    while pending:
        node = pending.pop()
        if node in visited:
            continue
        visited.add(node)
        for name, local in nodes[node]['references']:
            targets = [local] if local else definitions.get(name, [])
            if targets:
                pending.extend(targets)
            else:
                unresolved.setdefault(name, set()).add(node)
    result = {'status': 'DIAGNOSTIC private code/data section reachability only',
              'roots': a.roots, 'visited_sections': len(visited),
              'reachable': [{'object': path, 'section': nodes[(path, idx)]['section'],
                             'symbols': nodes[(path, idx)]['symbols']} for path, idx in sorted(visited)],
              'externals': [{'symbol': name, 'referenced_by': [
                  {'object': path, 'section': nodes[(path, idx)]['section']} for path, idx in sorted(origins)]}
                  for name, origins in sorted(unresolved.items())],
              'inputs_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
              'limits': ['ARM unwind metadata intentionally not root; final-link selection not inferred.',
                         'External native object callbacks and COMDAT selection remain outside this graph.',
                         'No execution, bank admission or actor ABI proof.']}
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(result['status'], len(visited), 'sections', len(unresolved), 'externals')


if __name__ == '__main__':
    main()
