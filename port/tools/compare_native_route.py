#!/usr/bin/env python3
"""Compare observed native route checkpoints with DS telemetry, keeping gaps.

Unlike wall-clock/update counters, checkpoint labels express the shared input
route phase. This compares observed fields only; it never asserts LCD parity.
"""
import argparse,hashlib,json,re
from pathlib import Path

PLAYER_FIELDS=('level','message','message_phase','script_active',
               'persistent_flags','transient_flags','player','camera')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def differences(expected,actual):
    if isinstance(expected,dict) and isinstance(actual,dict):
        return {k:differences(v,actual.get(k)) for k,v in expected.items()
                if v!=actual.get(k)}
    return {'DS':expected,'native':actual}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference',type=Path,action='append',required=True)
    p.add_argument('--native-log',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();text=args.native_log.read_text();actual={}
    for match in re.finditer(r'(PLAYER|STAR)_TELEMETRY (\{[^\n]+\})',text):
        item=json.loads(match[2]);key=(match[1],item['checkpoint'])
        if key in actual:raise ValueError('duplicate native checkpoint '+str(key))
        actual[key]=item
    rows=[]
    for path in args.reference:
        source=json.loads(path.read_text())
        for expected in source['samples']:
            kind='STAR' if 'fields' in expected else 'PLAYER'
            keys=('fields',) if kind=='STAR' else PLAYER_FIELDS
            label=expected['checkpoint'];item=actual.get((kind,label))
            if kind=='PLAYER' and 'player' not in expected:
                status='SCENE_ONLY_REFERENCE';diff={}
            elif item is None:status='NOT_REACHED';diff={}
            else:
                diff={k:differences(expected[k],item.get(k)) for k in keys
                      if k in expected and expected[k]!=item.get(k)}
                status='DIFFERENT_FIELDS' if diff else 'IDENTICAL_FIELDS'
            rows.append({'reference':str(path),'kind':kind,'checkpoint':label,
                         'status':status,'differences':diff})
    report={'status':'OBSERVED_TELEMETRY_COMPARISON','counts':
            {s:sum(r['status']==s for r in rows) for s in
             ['IDENTICAL_FIELDS','DIFFERENT_FIELDS','NOT_REACHED','SCENE_ONLY_REFERENCE']},
            'rows':rows,'native_log_sha256':sha(args.native_log),
            'reference_sha256':{str(f):sha(f) for f in args.reference},
            'limits':['Native runner result and scene/level admission are separate gates.',
                      'No framebuffer or unobserved actor-state parity claim.',
                      'Labels require the same input route and completed VBlank cadence.']}
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['counts']))
if __name__=='__main__':main()
