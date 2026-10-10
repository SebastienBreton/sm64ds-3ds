#!/usr/bin/env python3
"""Inventory actual private actor object edges against a frozen native link.

No link, data generation or runtime support is inferred from symbol presence.
Archive definitions are listed as candidates, not necessarily selected members.
"""
import os
import argparse, hashlib, json, re, subprocess, sys
from pathlib import Path

NM='/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbols(paths):
    p=subprocess.run([NM,'-A','-g','-P',*map(str,paths)],capture_output=True,text=True,check=True)
    rows=[]
    for line in p.stdout.splitlines():
        match=re.match(r'^(.*): (\S+) ([A-Za-z?])(?:\s|$)',line)
        if match:
            origin,name,kind=match.groups()
            rows.append((origin,name,kind))
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--actors',type=Path,required=True)
    parser.add_argument('--native-build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    assert not args.output.exists()
    actors=json.loads(args.actors.read_text());native=json.loads(args.native_build.read_text())
    base=args.actors.parent
    assert all(r['exit_code']==0 for r in actors['rows'])
    objects=[base/'objects'/Path(r['source']).with_suffix('.o') for r in actors['rows']]
    assert all(sha(p)==r['object_sha256'] for p,r in zip(objects,actors['rows']))
    paths=[Path(p) for p,h in native['link_inputs_sha256'].items() if Path(p).suffix in ('.o','.a')]
    assert all(sha(p)==native['link_inputs_sha256'][str(p)] for p in paths)
    private=symbols(objects);production=symbols(paths)
    defs={};prod={};refs={}
    for target, rows in ((defs,private),(prod,production)):
        for origin,name,kind in rows:
            if kind not in ('U','w','v'):
                target.setdefault(name,[]).append({'object':origin,'kind':kind})
    for origin,name,kind in private:
        if kind=='U':refs.setdefault(name,[]).append(origin)
    root=Path(os.environ.get('SM64DS_DECOMP_DIR') or Path(__file__).resolve().parents[2] / 'sm64ds-decomp')
    sys.path.insert(0,str(root/'tools'))
    import srcpath
    srcpath.set_root(root);index=srcpath.build_index()
    edges=[]
    for name,origins in sorted(refs.items()):
        resolution='private_definition' if name in defs else 'native_candidate' if name in prod else 'unresolved'
        edges.append({'symbol':name,'referenced_by':origins,'resolution':resolution,
            'definitions':defs.get(name,[]) or prod.get(name,[]),
            'original_source':index.get(name)})
    duplicates=[{'symbol':n,'private':d,'native':prod[n]} for n,d in sorted(defs.items())
        if n in prod and any(r['kind'] not in ('W','V') for r in d)
        and any(r['kind'] not in ('W','V') for r in prod[n])]
    counts={k:sum(e['resolution']==k for e in edges) for k in ('private_definition','native_candidate','unresolved')}
    result={'status':'PREPARATION actual ARM11 symbol inventory; no link/runtime PASS',
        'counts':counts,'private_strong_native_duplicates':duplicates,'edges':edges,
        'actor_report_sha256':sha(args.actors),'native_build_sha256':sha(args.native_build),
        'inputs_sha256':{str(p):sha(p) for p in objects+paths},
        'limits':['Native archive definitions are candidates; member extraction/link precedence not proven',
            'Existing weak HAL definitions require semantic review, not merely symbol resolution',
            'Data/PMF/vtable relocation, lifecycle, resource ownership and actor ABI not proven']}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'counts':counts,'strong_duplicates':len(duplicates),
        'unresolved':[e['symbol'] for e in edges if e['resolution']=='unresolved']},indent=2))


if __name__=='__main__':main()
