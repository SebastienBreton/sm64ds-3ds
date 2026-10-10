#!/usr/bin/env python3
"""Prepare native platform-bank data, with canonical CLPS context evidence."""
import argparse
import json
from pathlib import Path
from native_overlay_data import generate as generate_native
from overlay_clps_context import resolve, alias_level_copy

LAYOUTS = {
    91: (0x02130f00, 0x02135440, 0x021356e0, 14,
         [(0x021344e8, 0x02134524), (0x02134ba0, 0x02134bc4),
          (0x02134bd0, 0x02134bdc),  # Mutable clip-cap vector, between RTTI/name.
          (0x02134bf8, 0x02134c54), (0x02134cdc, 0x02134e64),
          (0x02134e80, 0x02134eb8), (0x02134f60, 0x02135078),
          (0x02135100, 0x02135120), (0x02135138, 0x0213516c),
          (0x02135298, 0x021352b4), (0x0213533c, 0x0213536c),
          (0x02135388, 0x021353a4)],
         [(0x02134e44, 0x02134e5c), (0x0213533c, 0x0213536c)]),
    95: (0x02135700, 0x02137780, 0x02137940, 11,
         [(0x02136f58, 0x02136fe0), (0x021373dc, 0x021374f4),
          (0x0213757c, 0x021375b0), (0x021375cc, 0x02137620),
          (0x021376cc, 0x021376e8)], [(0x0213757c, 0x021375a4)]),
}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('--overlay', type=int, choices=LAYOUTS, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--contexts', type=Path, required=True)
    p.add_argument('--level-input', type=Path, required=True)
    p.add_argument('--level-output', type=Path, required=True)
    a = p.parse_args()
    assert not any(x.exists() for x in (a.output, a.contexts, a.level_output))
    base, bss, end, profiles, spans, pmfs = LAYOUTS[a.overlay]
    edges, rows = resolve(a.root, a.overlay, spans)
    a.output.write_text(generate_native(a.root, a.overlay, base, spans, bss,
                                       end, profiles, pmfs, reviewed_external_edges=edges))
    a.level_output.write_text(alias_level_copy(a.level_input.read_text(), rows))
    a.contexts.write_text(json.dumps({'overlay': a.overlay, 'edges': rows,
                                    'limits': ['Object/link proof pending; no lifecycle execution.']}, indent=2)+'\n')
    print('PREPARED', a.overlay, len(rows), 'reviewed CLPS edges')
