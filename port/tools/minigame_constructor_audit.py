#!/usr/bin/env python3
"""Find retail initializers that refer to a proposed minigame data closure.

Linking a factory does not retain overlay constructor entries. This audit uses
the retail constructor table and relocation map to find candidate initializers;
callers must review which candidates execute on the imported scene's paths.
"""
import argparse
import json
import re
import struct
from pathlib import Path


def audit(root, data, retained=None):
    cfg = root/'config/arm9/overlays/ov006'
    blob = (root/'extracted/overlays/overlay_0006.bin').read_bytes()
    base = 0x020bfec0
    symbols = {}
    for name, kind, address in re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',
                                         (cfg/'symbols.txt').read_text(), re.M):
        symbols[int(address, 16)] = (name, kind)
    section = re.search(r'^\s+\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:',
                        (cfg/'delinks.txt').read_text(), re.M)
    if not section:raise ValueError('missing retail constructor section')
    begin, end = (int(v, 16) for v in section.groups())
    constructors = struct.unpack_from('<'+str((end-begin)//4)+'I', blob, begin-base)
    if not constructors or 0 in constructors or struct.unpack_from('<I', blob, end-base)[0]:
        raise ValueError('retail constructor table terminator changed')
    spans = [(int(v['address'], 16), int(v['end'], 16), n)
             for n, v in data['symbols'].items()
             if v['module']=='6' and v['kind']=='bss' and (retained is None or n in retained)]
    relocs = [(int(m[1], 16), int(m[2], 16)) for m in re.finditer(
        r'from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:overlay\(6\)',
        (cfg/'relocs.txt').read_text())]
    dependencies = set((Path(__file__).resolve().parents[1] / 'menu/wanted_dependencies.txt').read_text().splitlines())
    scheduled = set(re.findall(r'^SM64DS_MINIGAME_INIT\((\w+),',
                              (Path(__file__).resolve().parents[1] / 'menu/minigame_imports.inc').read_text(), re.M))
    candidates = []
    for index, address in enumerate(constructors):
        name, kind = symbols.get(address, ('', ''))
        size = re.fullmatch(r'function\(arm,size=0x([0-9a-f]+)\)', kind)
        if not size or not name.startswith('__sinit_'):
            raise ValueError(f'unknown retail initializer at {address:08x}')
        finish = address+int(size[1], 16)
        targets = set()
        for at, target in relocs:
            if not address <= at < finish:continue
            if struct.unpack_from('<I', blob, at-base)[0]!=target:
                raise ValueError(f'{name}: constructor relocation differs from ROM')
            targets.update(n for start, end, n in spans if start <= target < end)
        if targets:
            candidates.append({'function': name, 'address': hex(address), 'retail_index': index,
                               'bss_targets': sorted(targets), 'source_enrolled': name in dependencies,
                               'scheduled': name in scheduled})
    return {'status': 'PATH_REVIEW_REQUIRED', 'retained_filter': retained is not None,
            'candidates': candidates,
            'scope': 'Retail BSS references identify candidates, not scene-path reachability or runtime completeness'}


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('data_audit', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--retained', type=Path, help='one retained ELF symbol name per line')
    args = parser.parse_args()
    result = audit(args.root, json.loads(args.data_audit.read_text()),
                   set(args.retained.read_text().splitlines()) if args.retained else None)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"{len(result['candidates'])} retail initializer candidates; path review required")
