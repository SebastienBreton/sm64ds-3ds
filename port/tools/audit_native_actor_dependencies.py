#!/usr/bin/env python3
"""Classify unresolved candidate object symbols without admitting a bank.

Frozen ELF absence can be linker garbage collection. Link inputs are consulted
only when their current bytes still match the archived hash. Names are useful
for closure planning, never sufficient evidence of ABI or original behavior.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbols(path):
    defined, undefined = set(), set()
    with Path(path).open('rb') as stream:
        table = ELFFile(stream).get_section_by_name('.symtab')
        if table is None:
            raise ValueError(f'Missing symbol table: {path}')
        for symbol in table.iter_symbols():
            if symbol.name and symbol['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK'):
                target = undefined if symbol['st_shndx'] == 'SHN_UNDEF' else defined
                target.add(symbol.name)
    return defined, undefined


def audit(batch_report, baseline, frozen_inputs, nm):
    report = json.loads(batch_report.read_text())
    common, _ = symbols(baseline)
    providers, consumers = {}, {}
    for row in report['rows']:
        if sha(row['object']) != row['object_sha256']:
            raise ValueError(f'Candidate object changed: {row["object"]}')
        defined, undefined = symbols(row['object'])
        for name in defined:
            providers.setdefault(name, []).append(row['source'])
        for name in undefined:
            consumers.setdefault(name, []).append(row['source'])
    absent = set(consumers) - common - set(providers)
    eligible, excluded = [], []
    for row in json.loads(frozen_inputs.read_text()):
        path = Path(row['path'])
        if path.suffix not in ('.o', '.a'):
            continue
        if path.exists() and sha(path) == row['sha256']:
            eligible.append(str(path))
        else:
            excluded.append(str(path))
    found = {}
    # Batch bounded command lengths for platforms with small ARG_MAX.
    for start in range(0, len(eligible), 250):
        command = ['rtk', 'proxy', str(nm), '-A', '-g', '--defined-only',
                   *eligible[start:start + 250]]
        text = subprocess.run(command, check=True, capture_output=True, text=True).stdout
        for line in text.splitlines():
            match = re.fullmatch(r'(.+):[0-9a-fA-F]+\s+([A-Za-z?])\s+(\S+)', line)
            if match and match[3] in absent:
                found.setdefault(match[3], []).append({
                    'input_and_address': match[1], 'nm_type': match[2]})
    rows = []
    for name, uses in sorted(consumers.items()):
        status = ('BATCH_OBJECT_DEFINITION' if name in providers else
                  'COMMON_FROZEN_ELF_DEFINITION' if name in common else
                  'UNCHANGED_COMMON_LINK_INPUT_DEFINITION' if name in found else
                  'REQUIRES_IMPORT_OR_LINK_REVIEW')
        rows.append({'symbol': name, 'consumers': uses, 'status': status,
                     'candidate_providers': providers.get(name, []),
                     'common_input_providers': found.get(name, [])})
    return {
        'status': 'OFFLINE_SYMBOL_CLOSURE_PLAN_NOT_LINKED',
        'rows': rows,
        'counts': {s: sum(r['status'] == s for r in rows)
                   for s in sorted({r['status'] for r in rows})},
        'candidate_report_sha256': sha(batch_report),
        'baseline_elf_sha256': sha(baseline),
        'frozen_inputs_sha256': sha(frozen_inputs),
        'unchanged_link_inputs_consulted': len(eligible),
        'changed_or_missing_inputs_excluded': excluded,
        'limits': [
            'Presence by symbol name is not proof of ABI, original body or runtime.',
            'Unchanged link-input presence can explain ELF garbage collection; '
            'it does not establish that a future link selects that implementation.',
            'Generated data/aliases and dynamic actor children require separate review.',
            'No new profile, bank or level is admitted.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batch-report', required=True, type=Path)
    parser.add_argument('--baseline-elf', required=True, type=Path)
    parser.add_argument('--frozen-inputs', required=True, type=Path)
    parser.add_argument('--nm', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Keep previous dependency reports immutable')
    result = audit(args.batch_report, args.baseline_elf, args.frozen_inputs, args.nm)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['counts'], sort_keys=True))


if __name__ == '__main__':
    main()
