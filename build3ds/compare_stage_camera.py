#!/usr/bin/env python3
"""Compare native camera snapshots with DS frames at the same script/timer."""
import argparse
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run',type=Path)
parser.add_argument('--elf',type=Path,default=ROOT/'build3ds/cmake/smoke_player.elf')
parser.add_argument('--reference',type=Path,default=ROOT/'build3ds/reference-ds/cutscene/references.json')
args=parser.parse_args()
with args.elf.open('rb') as stream:
    elf=ELFFile(stream)
    symbols={s['st_value']:s.name for s in elf.get_section_by_name('.symtab').iter_symbols()
             if s.name.startswith('data_0208')}
reference=json.loads(args.reference.read_text())
comparisons=[]
for path in sorted(args.run.glob('stage-frame-*-camera.json')):
    native=json.loads(path.read_text())
    script=native.get('script_source',0)
    if not script:
        symbol=symbols.get(native['script_native'],'')
        if not symbol:continue
        script=int(symbol.removeprefix('data_'),16)
    candidates=[r for r in reference if int(r['script'],16)==script and r['timer']==native['timer']]
    # Initial scheduling deliberately has old/new view states at timer0/1.
    # Only settled reference captures are an exact equivalence gate here.
    if not candidates or native['timer']<10:continue
    for ds in candidates:
        differences={field:[a-b for a,b in zip(native[field],ds.get(field,ds.get(field+'_fx12',[])))]
                     for field in ('look_at','eye','view')}
        if any(len(differences[field])!=len(native[field]) for field in differences):
            raise ValueError('reference camera fields missing or truncated')
        exact=all(not any(v) for v in differences.values())
        comparisons.append(dict(native_frame=native['frame'],ds_step=ds['step'],
                                script=hex(script),timer=native['timer'],exact=exact,
                                differences=differences))
if not comparisons:raise SystemExit('No settled snapshots with matching script and timer')
result=dict(passed=all(c['exact'] for c in comparisons),comparisons=comparisons)
(args.run/'camera-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
