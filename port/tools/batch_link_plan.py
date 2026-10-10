#!/usr/bin/env python3
"""Private exact-input link planner. Never builds or edits common objects.

Whole-object replacement requires proof that all old strong definitions remain
available. A successful inventory is not linked ABI or runtime admission.
"""
import argparse, hashlib, json, re, shlex, subprocess
from pathlib import Path
NM='/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
CACHE={}
def preload(paths):
    paths=[p.resolve() for p in paths]
    for begin in range(0,len(paths),200):
        batch=paths[begin:begin+200]
        for path in batch:CACHE[str(path)]=(set(),set())
        r=subprocess.run(['rtk','proxy',NM,'-g','--defined-only',*map(str,batch)],
                         capture_output=True,text=True,check=True)
        current=str(batch[0]) if len(batch)==1 else None
        for line in r.stdout.splitlines():
            if line.endswith(':') and line[:-1] in CACHE:current=line[:-1];continue
            m=re.match(r'^[0-9a-f]+ ([A-TV-Z]) (\S+)$',line)
            if m:
                if current is None:raise ValueError('Object symbol output lacks an owner')
                strong,defined=CACHE[current];defined.add(m[2])
                if m[1] in 'ABCDGRST':strong.add(m[2])
def symbols(paths):
    if not paths:return set(),set()
    if len(paths)==1 and str(paths[0].resolve()) in CACHE:return CACHE[str(paths[0].resolve())]
    r=subprocess.run(['rtk','proxy',NM,'-g','--defined-only',*map(str,paths)],
                     capture_output=True,text=True,check=True)
    # GNU unique/weak definitions may co-exist; strong ownership must be exact.
    strong=set(re.findall(r'^[0-9a-f]+ [ABCDGRST] (\S+)$',r.stdout,re.M))
    all_defined=set(re.findall(r'^[0-9a-f]+ [A-TV-Z] (\S+)$',r.stdout,re.M))
    return strong,all_defined
def plan(batch,common_build,objects,output):
    freeze=json.loads((batch/'common-freeze.json').read_text());command=shlex.split((batch/'frozen-link.txt').read_text())
    for original,row in freeze['inputs'].items():
        path=Path(row['copy'])
        if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('Frozen input hash changed: '+original)
    private_strong,private_all=symbols(objects);rewritten=[];omitted=[];blockers=[]
    preload(objects);owners={}
    for obj in objects:
        for name in symbols([obj])[0]:owners.setdefault(name,[]).append(str(obj))
    for name,paths in owners.items():
        if len(paths)>1:blockers.append({'kind':'duplicate_private_strong_definition','symbol':name,'objects':paths})
    preload([Path(row['copy']) for row in freeze['inputs'].values() if Path(row['copy']).suffix=='.o'])
    for token in command:
        if token.startswith('-') or not token.endswith(('.o','.a','.ld')):
            rewritten.append(token);continue
        original=str((common_build/token).resolve()) if not Path(token).is_absolute() else token
        if original not in freeze['inputs']:raise ValueError('Unfrozen file link input: '+original)
        frozen=Path(freeze['inputs'][original]['copy'])
        if frozen.suffix=='.o':
            old_strong,_=symbols([frozen]);overlap=old_strong&private_strong
            if overlap:
                uncovered=sorted(old_strong-private_all)
                omitted.append({'original':original,'frozen':str(frozen),'overlap':sorted(overlap),'uncovered':uncovered})
                if uncovered:blockers.append({'kind':'whole_object_unique_definitions_would_be_lost','input':original,'symbols':uncovered})
                continue
        rewritten.append(str(frozen))
    # An output main/entry is intentionally a later explicit replacement. The
    # inventory does not pretend the old smoke main is a complete actor test.
    rewritten[1:1]=[str(p.resolve()) for p in objects]
    aliases=(batch/'native-vtable-alias-linker-options.txt').read_text().splitlines()
    rewritten[1:1]=aliases
    report={'status':'READY_FOR_EXPLICIT_MAIN_ENTRY_AND_LINK' if not blockers else 'BLOCKED_OBJECT_OWNERSHIP',
            'removed_objects':omitted,'blockers':blockers,'candidate_command':rewritten,
            'limits':['No link executed; replace smoke main with a unique diagnostic main/entry.',
                      'No automatic actor admission; linked RTTI/aliases/ctors and runtime/save gates required.']}
    output.write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('batch',type=Path);p.add_argument('common_build',type=Path)
    p.add_argument('candidate_objects',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    objects=[Path(x) for x in json.loads(a.candidate_objects.read_text())['objects']]
    print(plan(a.batch,a.common_build,objects,a.output)['status'])
