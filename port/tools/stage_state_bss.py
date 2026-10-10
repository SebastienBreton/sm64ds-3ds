#!/usr/bin/env python3
"""Allocate Stage state as one native BSS block with the original field aliases.

This replaces separate Player/HAL allocations: per-player arrays, counters,
star pointers and the clipper must share the original offsets. Initialization
still belongs to the original game routines.
"""
import argparse
from pathlib import Path
import re

SPANS = [
    # Wireless SDK initialized flag and work pointer; zero means uninitialized.
    (0x020a89a8, 0x020a89b0),
    # OAM control fields and both 0x400-byte buffers with interior copy aliases.
    (0x0209e660, 0x0209ee74),
    # Three independent 32-bit audio allocation masks, reset by0204f1e8.
    (0x020a4d48, 0x020a4d54),
    (0x020a552c, 0x020a5634), (0x020a5bbc, 0x020a5bd4),
    # Stream state, four 0x12c-byte sound players, eight 12-byte channels.
    (0x020a5634, 0x020a5684), (0x020a5bd4, 0x020a6084),
    # Audio queue, shared640-byte status and256x24-byte command nodes.
    (0x020a6484, 0x020a7f60),
    (0x020a7f60, 0x020a7fc4),
    (0x0209f5f8, 0x0209f610), (0x0209f648, 0x0209fc48),
    (0x0209b2fc, 0x0209b304), (0x0209b47c, 0x0209b480),
    (0x0209b4a8, 0x0209b4ac), (0x0209cef0, 0x0209cef4),
    (0x0209ee78, 0x0209ee90),
    (0x0209f1d8, 0x0209f498), (0x0209fc48, 0x020a0c5c),
    (0x020a0f04, 0x020a0f08), (0x020a0f30, 0x020a0f34),
    # Two four-entry name tables, each cleared as 0x58 bytes by0203db64.
    # Halfword aliases at +2 are fields, not separate allocations.
    (0x020a10a4, 0x020a1154),
    # Sixteen audio heap entries of size0x44, initialized by0204fc40.
    (0x020a50ec, 0x020a552c), (0x020a6084, 0x020a6088),
    (0x020ad620, 0x020ad640),
]


def generate(config):
    names = {}
    symbol_text = (config / 'symbols.txt').read_text()
    symbol_text += '\n' + (config / 'overlays/ov001/symbols.txt').read_text()
    for line in symbol_text.splitlines():
        m = re.fullmatch(r'(\w+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?', line)
        if not m or not any(a <= int(m[3], 16) < b for a, b in SPANS):
            continue
        if m[2] != 'bss':
            raise ValueError(f'non-BSS symbol: {line}')
        names.setdefault(int(m[3], 16), []).append(m[1])
    if names.get(0x0209f40c) != ['data_0209f40c', 'STAR_MARKERS'] or any(a not in names for a, b in SPANS):
        raise ValueError('unexpected Stage BSS layout')
    lines = ['// Generated Stage and text BSS; interior aliases retain ROM offsets.', 'asm(R"STATE(']
    for start, end in SPANS:
        lines += [f'.pushsection .bss.stage_state_{start:08x},"aw",%nobits', '.balign 4']
        cursor = start
        for addr, labels in sorted(names.items()):
            if not start <= addr < end:
                continue
            if cursor < addr:
                lines.append(f'.zero {addr-cursor}')
            for label in labels:
                lines += [f'.global {label}', f'.type {label}, %object', f'{label}:']
            cursor = addr
        lines += [f'.zero {end-cursor}', '.popsection']
    lines += [')STATE");', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('config', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.write_text(generate(args.config))
