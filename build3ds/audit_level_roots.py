#!/usr/bin/env python3
"""Resolve each Stage root using its level's overlay, never an ambiguous address."""
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT / 'sm64ds-decomp'
BASE = 0x02004000
OV_BASE = 0x021111a0
arm9 = (REPO / 'extracted/arm9_dec.bin').read_bytes()
levels = struct.unpack_from('<52I', arm9, 0x020758c8-BASE)
roots = struct.unpack_from('<52I', arm9, 0x02092208-BASE)
assert levels == tuple(range(8, 60)), 'unexpected level overlay table'
rows = []
for level, (overlay, root) in enumerate(zip(levels, roots)):
    cfg = REPO / f'config/arm9/overlays/ov{overlay:03}'
    blob = (REPO / f'extracted/overlays/overlay_{overlay:04}.bin').read_bytes()
    symbols = {}
    for line in (cfg / 'symbols.txt').read_text().splitlines():
        match = re.fullmatch(r'(\w+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?', line)
        if match:
            symbols.setdefault(int(match[3],16), []).append((match[1], match[2]))
    assert root in symbols, (level, hex(root))
    names = [name for name, kind in symbols[root] if kind.startswith('data')]
    assert len(names) == 1, (level, names)
    offset = root-OV_BASE
    assert 0 <= offset <= len(blob)-24, (level, offset)
    clps, objects, model, kcl, other1, other2, areas, count, tail = struct.unpack_from('<II4HIB3s', blob, offset)
    assert count <= 8, (level, count)
    pointers = {root:clps, root+4:objects, root+16:areas}
    for at, target in pointers.items():
        if target == 0:
            continue
        assert target in symbols, (level, hex(at), hex(target))
        assert any(kind.startswith('data') for _,kind in symbols[target]), (level, hex(target))
        assert re.search(rf'^from:0x{at:08x} kind:load to:0x{target:08x} module:overlay\({overlay}\)$',
                         (cfg / 'relocs.txt').read_text(), re.M), (level, hex(at))
    rows.append(dict(level=level, overlay=overlay, root=f'0x{root:08x}', symbol=names[0],
                     model_file_id=model, collision_file_id=kcl, area_count=count,
                     clps=f'0x{clps:08x}', objects=f'0x{objects:08x}', areas=f'0x{areas:08x}'))
out = ROOT / 'build3ds/validation/level-roots.json'
out.write_text(json.dumps(rows, indent=2)+'\n')
print(f'PASS: {len(rows)} level roots resolve to their own overlay and data symbols; '
      'root pointer fields match overlay-specific relocation metadata. '
      'This is a catalog, not native level-data integration or gameplay validation.')
