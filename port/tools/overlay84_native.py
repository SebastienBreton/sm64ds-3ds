#!/usr/bin/env python3
"""ROM-checked ov84 data islands; original C++ owns Goomba profiles/RTTI.

Material animation descriptors, scalar tables, resource pointer arrays and
nonvirtual PMFs are retained. DS RTTI/vtables are deliberately not emitted.
This prepares a private object, without dispatcher admission.
"""
import argparse
import struct
from pathlib import Path
from native_overlay_data import generate as generate_native

BASE = 0x02129020
SPANS = [
    (0x02130174, 0x0213035c),  # Material channels and actor scalar/resource tables.
    (0x02130880, 0x021308d4),  # Material name/descriptors; five Goomba PMFs.
    (0x021309c4, 0x021309f4),  # Six Red Bob-omb PMFs.
    (0x02130a14, 0x02130a30),  # Red Bob-omb profile.
    (0x02130acc, 0x02130b20),  # Three Fire Piranha profiles.
    (0x02130ba4, 0x02130bec),  # Nine Piranha PMFs.
    (0x02130c04, 0x02130c20),  # Piranha profile.
]
PMFS = [(0x021308ac, 0x021308d4), (0x021309c4, 0x021309f4),
        (0x02130ba4, 0x02130bec)]


def generate(root):
    blob = (root/'extracted/overlays/overlay_0084.bin').read_bytes()
    # Three profiles are defined by the linked original daKrb_c.cpp.
    expected = {
        0x021308ec: (0x0212c0b0, 0x001800c8, 0x10000006, 0x32000,
                     0x46000, 0x1000000, 0x1000000),
        0x02130908: (0x0212c054, 0x001900c9, 0x10000006, 0x32000,
                     0x46000, 0x6a4000, 0x800000),
        0x02130924: (0x0212bff8, 0x001a00ca, 0x10000006, 0x64000,
                     0xc8000, 0x1000000, 0x1000000),
    }
    for address, words in expected.items():
        assert struct.unpack_from('<7I', blob, address-BASE) == words
    return generate_native(root, 84, BASE, SPANS, 0x02130cc0,
                           0x02130ee0, 8, PMFS)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    assert not a.output.exists()
    a.output.write_text(generate(a.root))
