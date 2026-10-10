#!/usr/bin/env python3
"""Emit GNU assembler storage for an overlay BSS, preserving offset aliases.

This allocates storage only. The overlay's native static initializers must
still run before use. No ROM pointers or initialized data are copied.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import re


def generate(config):
    sections = (config / "delinks.txt").read_text()
    m = re.search(r"^[ \t]*\.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:bss align:(\d+)", sections, re.M)
    if not m:
        raise ValueError("missing overlay BSS bounds")
    start, end = int(m[1], 16), int(m[2], 16)
    align = int(m[3])
    assert end > start and align > 0 and align & (align - 1) == 0
    symbols = defaultdict(list)
    seen = set()
    for line in (config / "symbols.txt").read_text().splitlines():
        m = re.fullmatch(r"(\w+) kind:bss addr:0x([0-9a-f]+)", line)
        if not m:
            continue
        name, address = m[1], int(m[2], 16)
        if not start <= address < end or name in seen:
            raise ValueError(f"invalid BSS symbol: {line}")
        seen.add(name)
        symbols[address].append(name)
    if not symbols:
        raise ValueError("empty BSS symbol table")
    lines = ["// Generated overlay BSS storage; native static initialization required.",
             'asm(R"BSS(', f'.pushsection .bss.sm64ds_{config.name},"aw",%nobits',
             f'.balign {align}']
    cursor = start
    for address, names in sorted(symbols.items()):
        if address > cursor:
            lines.append(f'.zero {address - cursor}')
        for name in names:
            lines += [f'.global {name}', f'.type {name}, %object', f'{name}:']
        cursor = address
    lines += [f'.zero {end - cursor}', '.popsection', ')BSS");', '']
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = generate(args.config)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source)


if __name__ == "__main__":
    main()
