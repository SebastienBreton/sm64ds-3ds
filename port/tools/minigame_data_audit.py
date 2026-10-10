#!/usr/bin/env python3
"""Audit the retail data closure before importing a native minigame.

This emits a review manifest, not executable storage: symbol extents inferred
from the next label still need review for indexed accesses spanning labels.
All relocation words are checked against the supplied extracted ROM.
"""
import argparse
import json
import re
import struct
from pathlib import Path


def audit(root, missing):
    modules = {}
    by_name = {}
    section_ends = {}
    for module, folder, base, binary in [
        ('main', root/'config/arm9', 0x02004000, root/'extracted/arm9_dec.bin'),
        *[(str(i), root/f'config/arm9/overlays/ov{i:03}', base,
           root/f'extracted/overlays/overlay_{i:04}.bin')
          for i, base in [(1, 0x020aa420), (4, 0x020ad660), (6, 0x020bfec0)]]]:
        symbols = {}
        for m in re.finditer(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',
                             (folder/'symbols.txt').read_text(), re.M):
            symbols[int(m[3], 16)] = (m[1], m[2])
            by_name[m[1]] = (module, int(m[3], 16), m[2])
        section_ends[module] = [int(x, 16) for x in re.findall(
            r'^\s+\.\w+\s+start:0x[0-9a-f]+ end:0x([0-9a-f]+) kind:',
            (folder/'delinks.txt').read_text(), re.M)]
        relocs = {}
        for m in re.finditer(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',
                             (folder/'relocs.txt').read_text()):
            relocs[int(m[1], 16)] = (m[2], int(m[3], 16), m[4])
        modules[module] = (symbols, relocs, base, binary.read_bytes())
    # Base helpers index across labelled members of shared state/asset tables.
    # Preserve the complete base data and BSS, excluding executable/ctor bytes.
    missing = set(missing)
    for name, (module, address, kind) in by_name.items():
        if module == '4' and (0x020b944c <= address < 0x020b948c or
                              0x020b9e00 <= address < 0x020bfec0):
            if not kind.startswith('function'):
                missing.add(name)
    # Amida's four-entry OAM arrow descriptor crosses an ambiguous label at
    # +12. Preserve the entire retail 8-byte-entry stream through its FFFF
    # terminator, rather than treating that label as the end of the asset.
    if 'data_ov006_0213a35c' in missing:
        blob = modules['6'][3]
        arrow = blob[0x0213a360-0x020bfec0:0x0213a380-0x020bfec0]
        if len(arrow)!=32 or [struct.unpack_from('<H',arrow,i+6)[0] for i in range(0,32,8)]!=[0,0,0,0xffff]:
            raise ValueError('Amida arrow OAM terminator changed')
        missing.add('data_ov006_0213a36c')
    # Jump's shared sprite pool is three 0x20-byte records. The retail
    # constructor passes those dimensions to __cxa_vec_ctor at0212f660;
    # indexed reset/update/draw functions cross labels within record zero.
    if 'data_ov006_021404d8' in missing:
        blob=modules['6'][3]
        for address, word in ((0x0212f674, 0xe3a01003), (0x0212f678, 0xe3a02020),
                              (0x0212f6a4, 0x021404d8)):
            if struct.unpack_from('<I',blob,address-0x020bfec0)[0]!=word:
                raise ValueError('Jump sprite pool constructor dimensions changed')
        for name, (module, address, kind) in by_name.items():
            if module=='6' and 0x021404d8 <= address < 0x02140538:
                if kind!='bss':raise ValueError('Jump sprite pool BSS layout changed')
                missing.add(name)
    pending = list(missing)
    records = {}
    unresolved = set()
    while pending:
        name = pending.pop()
        if name in records or name in unresolved:
            continue
        if name not in by_name:
            unresolved.add(name)
            continue
        mod, address, kind = by_name[name]
        if kind.startswith('function'):
            records[name] = {'module': mod, 'address': hex(address), 'kind': kind}
            continue
        syms, relocs, base, blob = modules[mod]
        ends = [a for a in [*syms, *section_ends[mod]] if a > address]
        if not ends:
            unresolved.add(name)
            continue
        end = min(ends)
        links = []
        if kind != 'bss':
            assert base <= address < end <= base + len(blob), name
            for at, (rtype, target, dest) in relocs.items():
                if not address <= at < end:
                    continue
                assert rtype == 'load' and at + 4 <= end, (name, hex(at), rtype)
                word = struct.unpack_from('<I', blob, at-base)[0]
                assert word == target, (name, hex(at), hex(word), hex(target))
                candidates = ['main'] if dest == 'main' else re.findall(r'\d+', dest)
                resolved = [(m, modules[m][0][target][0]) for m in candidates
                            if m in modules and target in modules[m][0]]
                if not resolved:
                    raise ValueError(f'{name}: unresolved relocation {target:08x} {dest}')
                # Retail ambiguous overlay annotations refer to reused addresses.
                # Prefer the active minigame base, then game overlay.
                resolved.sort(key=lambda p: (p[0] not in ('4', '6'), p[0]))
                target_name = resolved[0][1]
                links.append({'at': hex(at), 'target': target_name, 'module_hint': dest})
                pending.append(target_name)
        records[name] = {'module': mod, 'address': hex(address), 'end': hex(end),
                         'kind': kind, 'bytes': end-address, 'relocations': links}
    return {'status': 'REVIEW_REQUIRED', 'roots': sorted(missing), 'symbols': dict(sorted(records.items())),
            'unresolved': sorted(unresolved)}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('missing', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    result = audit(a.root, a.missing.read_text().splitlines())
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"{len(result['symbols'])} symbols, {len(result['unresolved'])} unresolved; review required")
