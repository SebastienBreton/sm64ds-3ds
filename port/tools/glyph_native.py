#!/usr/bin/env python3
"""Adapt glyph VRAM access and ARM register-shift semantics for native C++."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
anchor = '    if ((x & 0xf) == 0) {'
assert source.count(anchor) == 1
source = source.replace(anchor, '    chr = ntr::xlate<u32>(chr);\n\n' + anchor)
# At x%16 == 8, rsh == 32. ARM LSR by register yields zero; C shifting
# a 32-bit word by 32 is undefined (and differs on native host CPUs).
assert source.count('*src >> rsh') == 1
assert source.count('*src2 >> rsh') == 1
source = source.replace('*src >> rsh', 'glyph_lsr(*src, rsh)')
source = source.replace('*src2 >> rsh', 'glyph_lsr(*src2, rsh)')
prefix = '''// Generated from original glyph blitter; native address/shift boundary.
#include "ntr/mmio.h"
static unsigned glyph_lsr(unsigned value, unsigned shift) {
    return shift < 32 ? value >> shift : 0;
}
extern "C" {
'''
Path(sys.argv[2]).write_text(prefix + source + '\n}\n')
