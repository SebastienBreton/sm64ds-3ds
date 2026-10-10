#!/usr/bin/env python3
"""Compare isolated game-phase archives, with optional inclusive GX profiling."""
import argparse
import hashlib
import json
from pathlib import Path
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('before',type=Path)
p.add_argument('after',type=Path)
p.add_argument('--fine',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
images=['player-native-geometry.bmp','player-native-upper.bmp','player-native-bottom.bmp',
        'player-gpu-left.bmp','player-gpu-right.bmp']

def read(directory):
    run=json.loads((directory/'run.json').read_text())
    if run['result']!='PASS' or not run.get('emulator_log_fresh') or run.get('unmapped_access_count')!=0:
        raise SystemExit(f'{directory}: fresh PASS with zero unmapped accesses required')
    log=(directory/'log.txt').read_text()
    marker='Menu FPS measured window begins at frame840; verified state0 substate0 animated menu'
    if log.count(marker)!=1 or 'Menu FPS phase frame=959 state=0 substate=0' not in log:
        raise SystemExit(f'{directory}: measured animated-menu state missing')
    phases={name:float(value) for name,value in re.findall(
        r'Menu game phase measured120 ms/frame: (.+)=([\d.]+)',log)}
    if len(phases)!=12:raise SystemExit(f'{directory}: expected twelve game phases')
    measured=log.split(marker,1)[1]
    batching=[float(value) for value in re.findall(r'profile frames=60 game_ms=[\d.]+ batch_ms=([\d.]+)',measured)]
    fine={name:{'inclusive_ms_per_update':float(ms),'calls_per_update':float(calls)}
          for name,ms,calls in re.findall(r'GX fine measured120: (.+) ms/update=([\d.]+) calls/update=([\d.]+)',log)}
    return {'directory':str(directory.resolve()),'binary_sha256':run['sha256'],
            'phases_ms_per_update':phases,'batching_ms_per_update':batching,
            'fine_inclusive':fine,'image_sha256':{name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in images}}

records={label:read(directory) for label,directory in [('before',a.before),('after',a.after)]}
if a.fine:records['fine']=read(a.fine)
equal=all(record['image_sha256']==records['before']['image_sha256'] for record in records.values())
before=records['before']['phases_ms_per_update']['Render/GX decode']
after=records['after']['phases_ms_per_update']['Render/GX decode']
report={'scope':'One update and presentation per sample in Azahar; full game display-frame rate and physical hardware are not measured.',
        'measured_updates':120,'images_identical':equal,'render_ms_saved_per_update':before-after,
        'render_reduction_percent':100*(before-after)/before,'records':records,
        'fine_warning':'Fine phases are inclusive and include clock overhead; matrix multiply is nested inside matrix commands. Do not sum them or infer FPS from instrumented runs.'}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(f'Render/GX: {before:.6f} -> {after:.6f} ms/update; saved {before-after:.6f} ms ({report["render_reduction_percent"]:.1f}%)')
print('PASS five captured images identical' if equal else 'FAIL captured images differ')
if not equal:raise SystemExit(1)
