#!/usr/bin/env python3
"""Import HUD sprite descriptors, retaining native pointers between tables.

Only the reviewed descriptor ranges are imported, never overlay executable
code or RTTI. Reject foreign/function references instead of copying DS pointers.
"""
import argparse
from pathlib import Path
import re
import struct

TABLES = [
    (1, 0x020aa420, [(0x020ab800, 0x020ad450)]),
    # Decimal divisors, three health-pointer arrays and palette offsets,
    # 128-byte health palette, then sprite descriptors. The gaps contain
    # profiles/RTTI/vtables or tables already owned by Player/level loader.
    (2, 0x020ad660, [(0x0210c208, 0x0210c210), (0x0210c230, 0x0210c2c0),
                         (0x0210c310, 0x0210c390), (0x0210c3e8, 0x0210cb5c), (0x0210ce80, 0x0210d5f8)]),
]


def generate(root):
    lines = ['// Generated from the local ROM: HUD sprite descriptor tables.', 'asm(R"OAM(']
    for module, base, spans in TABLES:
        cfg = root / f'config/arm9/overlays/ov{module:03}'
        blob = (root / f'extracted/overlays/overlay_{module:04}.bin').read_bytes()
        inside = lambda addr: any(a <= addr < b for a, b in spans)
        names, relocs = {}, {}
        for line in (cfg / 'symbols.txt').read_text().splitlines():
            m = re.fullmatch(r'([\w.]+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?', line)
            if not m or not inside(int(m[3], 16)):
                continue
            if not m[2].startswith('data'):
                raise ValueError(f'non-data symbol: {line}')
            names.setdefault(int(m[3], 16), []).append(m[1])
        for line in (cfg / 'relocs.txt').read_text().splitlines():
            m = re.match(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)', line)
            if not m or not inside(int(m[1], 16)):
                continue
            addr, target = int(m[1], 16), int(m[3], 16)
            if m[2] != 'load' or m[4] != f'overlay({module})' or target not in names:
                raise ValueError(f'unreviewed HUD relocation: {line}')
            if addr % 4 or not inside(addr + 3) or struct.unpack_from('<I', blob, addr-base)[0] != target:
                raise ValueError(f'invalid pointer: {line}')
            relocs[addr] = names[target][0]
        for start, end in spans:
            lines += [f'.pushsection .data.oam_{start:08x},"aw",%progbits', '.balign 4']
            cursor = start
            while cursor < end:
                for name in names.get(cursor, []):
                    lines += [f'.global {name}', f'.type {name}, %object', f'{name}:']
                if cursor in relocs:
                    if any(cursor+i in names for i in range(1, 4)):
                        raise ValueError('symbol inside pointer')
                    lines.append(f'.word {relocs[cursor]}')
                    cursor += 4
                else:
                    stop = min(cursor+16, end)
                    for boundary in range(cursor+1, stop):
                        if boundary in names or boundary in relocs:
                            stop = boundary
                            break
                    raw = blob[cursor-base:stop-base]
                    if len(raw) != stop-cursor:
                        raise ValueError('short ROM read')
                    lines.append('.byte '+','.join(str(v) for v in raw))
                    cursor = stop
            lines.append('.popsection')
    # Player-icon pointers refer to the shared descriptors resident from ov1.
    cfg = root / 'config/arm9/overlays/ov001/symbols.txt'
    names = {}
    for line in cfg.read_text().splitlines():
        m = re.fullmatch(r'([\w.]+) kind:data\(\w+\) addr:0x([0-9a-f]+)(?: ambiguous)?', line)
        if m and 0x020ab800 <= int(m[2],16) < 0x020ad450:
            names.setdefault(int(m[2],16),m[1])
    blob = (root/'extracted/overlays/overlay_0002.bin').read_bytes()
    relocs = (root/'config/arm9/overlays/ov002/relocs.txt').read_text().splitlines()
    lines += ['.pushsection .data.map_icons,"aw",%progbits', '.balign 4',
              '.global _ZN3OAM15MM_PLAYER_ICONSE', '_ZN3OAM15MM_PLAYER_ICONSE:']
    for address in range(0x0210c174,0x0210c1b8,4):
        target = struct.unpack_from('<I',blob,address-0x020ad660)[0]
        expected = f'from:0x{address:08x} kind:load to:0x{target:08x} module:overlays(0,1)'
        if target not in names or expected not in relocs:
            raise ValueError('unreviewed map icon pointer '+hex(address))
        lines.append('.word '+names[target])
    lines.append('.popsection')
    return '\n'.join(lines + [')OAM");', ''])


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.write_text(generate(args.root))
