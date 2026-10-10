#!/usr/bin/env python3
"""Read retail state at first Player render entry without a new Behavior.

The VBlank reference remains authoritative and unchanged. This supplementary
comparison admits only frame0/Behavior0 with the original Player observation,
game counter and update delta unchanged. It never delays the native game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from player_telemetry import inspect_ram

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def read32(data, address): return struct.unpack_from('<I',data,address-0x02000000)[0]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference',type=Path,required=True)
parser.add_argument('--states',type=Path,required=True)
parser.add_argument('--native-log',type=Path,required=True)
parser.add_argument('--runner',type=Path,required=True)
parser.add_argument('--rom',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args = parser.parse_args()
out = args.output.resolve(); out.mkdir(exist_ok=False,parents=True)
samples = json.loads(args.reference.read_text())['samples']
actual = {}
for m in re.finditer(r'PLAYER_TELEMETRY (\{[^\n]+\})',args.native_log.read_text()):
    item=json.loads(m[1]); label=item['checkpoint']
    if label in actual: raise ValueError('Duplicate native checkpoint '+label)
    actual[label]=item
commands=[]
for sample in samples:
    label=sample['checkpoint']
    if not re.fullmatch(r'[A-Za-z0-9_-]+',label): raise ValueError('Unsafe checkpoint')
    state=(args.states/(label+'.state')).resolve()
    assert state.is_file()
    # Retain the reference's buttons while completing its current CPU pass.
    # Any new Player Behavior or frame boundary rejects the observation.
    commands += ['load '+str(state),'poseobserve '+label,
                 'run 1 '+format(0xfff^sample['keys'],'03x'),'poseobserve off']
commands.append('quit')
command_text='\n'.join(commands)+'\n'
(out/'commands.txt').write_text(command_text)
command=['rtk','proxy',str(args.runner.resolve()),str(args.rom.resolve())]
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:
    result=subprocess.run(command,input=command_text,text=True,cwd=out,
                          stdout=stdout,stderr=stderr,timeout=180)
assert result.returncode==0
rows=[]
for expected in samples:
    label=expected['checkpoint']; native=actual.get(label)
    base_data=(args.states/(label+'.ram')).read_bytes()
    matches=[]
    for event_file in out.glob(label+'-event*-render-entry.event.json'):
        if '-body-model-' in event_file.name: continue
        event=json.loads(event_file.read_text())
        ram=event_file.with_name(event_file.name.replace('.event.json','.ram'))
        observed=inspect_ram(str(ram),label,expected['updates'],expected['keys'])
        data=ram.read_bytes()
        guards={'frame0':event['ds_frame_since_trace']==0,
                'Behavior0':event['behavior_calls']==0,
                'original_Player_unchanged':observed['player']==expected['player'],
                'original_counter_unchanged':read32(data,0x020a0db0)==read32(base_data,0x020a0db0),
                'original_delta_unchanged':read32(data,0x0208ee44)==read32(base_data,0x0208ee44),
                'original_Player_pointer_unchanged':read32(data,0x0209f394)==read32(base_data,0x0209f394)}
        matches.append({'event':event,'event_file':event_file.name,'ram_file':ram.name,
                        'ram_sha256':sha(ram),'guards':guards,'admitted':all(guards.values()),
                        'observation':observed})
    admitted=[m for m in matches if m['admitted']]
    if len(admitted)>1: raise ValueError('Ambiguous render entry '+label)
    selected=admitted[0] if admitted else None
    rows.append({'checkpoint':label,'status':('NOT_ADMITTED' if selected is None else
                 ('NATIVE_NOT_REACHED' if native is None else
                  ('IDENTICAL_RENDER_PHASE_CAMERA' if selected['observation']['camera']==native['camera'] else 'DIFFERENT_RENDER_PHASE_CAMERA'))),
                 'reference_vblank_camera':expected['camera'],
                 'native_camera':native['camera'] if native else None,
                 'retail_state_sha256':sha(args.states/(label+'.state')),
                 'original_counter':read32(base_data,0x020a0db0),'candidates':matches})
report={'status':'SUPPLEMENTARY_RENDER_PHASE_COMPARISON','counts':
        {s:sum(r['status']==s for r in rows) for s in
         ('IDENTICAL_RENDER_PHASE_CAMERA','DIFFERENT_RENDER_PHASE_CAMERA','NOT_ADMITTED','NATIVE_NOT_REACHED')},
        'rows':rows,'input_sha256':{str(p.resolve()):sha(p) for p in
        (args.reference,args.native_log,args.runner,args.rom,Path(__file__))},
        'runner_exit':result.returncode,'ram_writes':False,
        'limits':['VBlank mismatches are preserved; this does not replace the earlier reference.',
                  'Only the first render entry before any new Player Behavior/frame boundary is admitted.',
                  'Original Player fields/counter/delta must remain exactly unchanged.',
                  'No LCD pixel parity or unobserved-state/callback-order claim.']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['counts']))
