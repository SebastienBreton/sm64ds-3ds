#!/usr/bin/env python3
"""Compare four run.py archives: uncached CPU, cached CPU, uncached GPU, cached GPU."""
import argparse
import hashlib
import json
from pathlib import Path
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('runs',nargs=4,type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
labels=['uncached_cpu','cached_cpu','uncached_gpu','cached_gpu']
images=['player-native-geometry.bmp','player-native-upper.bmp','player-native-bottom.bmp',
        'player-gpu-left.bmp','player-gpu-right.bmp']
records=[]
for label,directory in zip(labels,a.runs):
    run=json.loads((directory/'run.json').read_text())
    if run['result']!='PASS':raise SystemExit(f'{label}: run verdict is not PASS')
    if not run.get('emulator_log_fresh') or run.get('unmapped_access_count')!=0:
        raise SystemExit(f'{label}: fresh emulator log with zero unmapped accesses required')
    log=(directory/'log.txt').read_text()
    expected_cache=int(label.startswith('cached'))
    expected_gpu=int(label.endswith('gpu'))
    if f'Menu FPS configuration: BGcache={expected_cache} GPUblit={expected_gpu};' not in log:
        raise SystemExit(f'{label}: unexpected cache/blit configuration')
    marker='Menu FPS measured window begins at frame840; verified state0 substate0 animated menu'
    if log.count(marker)!=1 or 'Menu FPS phase frame=959 state=0 substate=0' not in log:
        raise SystemExit(f'{label}: animated-menu measured window not verified')
    matches=re.findall(r'Menu FPS measured120 ms/frame: game=([\d.]+) audio=([\d.]+) presentation=([\d.]+) total=([\d.]+) fps=([\d.]+)',log)
    if len(matches)!=1:raise SystemExit(f'{label}: expected one complete measured window')
    timing=dict(zip(['game_ms','audio_ms','presentation_ms','total_ms','fps'],map(float,matches[0])))
    digest={name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in images}
    measured_log=log.split('Menu FPS measured window begins',1)[-1]
    parts=[line for line in measured_log.splitlines() if line.startswith(('Prepared PPU parts','Native GPU parts','Native blit parts','Native composite ms','PPU background cache'))]
    records.append({'variant':label,'directory':str(directory.resolve()),'binary_sha256':run['sha256'],
                    'timing':timing,'image_sha256':digest,'profile':parts})
if len({record['binary_sha256'] for record in records})!=1:
    raise SystemExit('A/B archives use different binaries')
equal=all(record['image_sha256']==records[0]['image_sha256'] for record in records)
result={'scope':'One original menu Behavior/Render update, one VBlank commit and one presentation per sample; not full rt_run display-frame rate.',
        'measured_frames':120,'warmup_frames':120,'menu_state':0,'menu_substate':0,
        'images_identical':equal,'runs':records}
if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
for record in records:
    t=record['timing']
    print(f"{record['variant']}: total={t['total_ms']:.3f}ms presentation={t['presentation_ms']:.3f}ms fps={t['fps']:.3f}")
print('PASS all five captured images are byte-identical' if equal else 'FAIL captured images differ')
if not equal:raise SystemExit(1)
