#!/usr/bin/env python3
"""Inventory native-overlay import batches without generating fallback code.

Uses the production enrollment resolver once. Missing original functions,
ambiguous overlay addresses and table candidates remain explicit review items;
source coverage alone does not establish runtime correctness.
"""
import argparse,json,re,struct,sys,subprocess,shutil
from pathlib import Path

def audit(root,ids=None):
    sys.path.insert(0,str(root/'tools'))
    import srcpath
    srcpath.set_root(root);index=srcpath.build_index()
    arm=(root/'extracted/arm9_dec.bin').read_bytes();result=[]
    for cfg in sorted((root/'config/arm9/overlays').glob('ov*')):
        ov=int(cfg.name[2:])
        if ids is not None and ov not in ids:continue
        text=(cfg/'symbols.txt').read_text();de=(cfg/'delinks.txt').read_text()
        base=int(re.search(r'\.text\s+start:0x([0-9a-f]+)',de)[1],16) if '.text' in de else None
        if base is None:continue
        symbols=[(n,k,int(a,16)) for n,k,a in re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',text,re.M)]
        funcs=[n for n,k,a in symbols if k.startswith('function')]
        paths={};missing=[]
        for n in funcs:
            if n not in index:missing.append(n)
            else:paths.setdefault(index[n],[]).append(n)
        bss=re.search(r'\.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)',de)
        blob=(root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes();profiles=[]
        function_names={}
        for n,k,a in symbols:
            if k.startswith('function'):function_names.setdefault(a,[]).append(n)
        for n,k,a in symbols:
            # BaseProfile has only two words; do not require ActorProfile's
            # larger tail to identify a scene profile or read across its end.
            if n.startswith('g_profile_') and base<=a and a-base+8<=len(blob):
                words=struct.unpack_from('<2I',blob,a-base);actor=words[1]&65535
                registry=struct.unpack_from('<I',arm,0x2090864-0x2004000+actor*4)[0] if actor<400 else None
                profiles.append({'symbol':n,'address':hex(a),'actor_id':actor,'registry_matches':registry==a,
                    'factory_address':hex(words[0]),
                    'factory_symbol_candidates':function_names.get(words[0]&~1,[]),
                    'render_priority':words[1]>>16})
        ambiguous=[]
        for line in (cfg/'relocs.txt').read_text().splitlines():
            if 'module:overlays(' in line:ambiguous.append(line)
        result.append({'overlay':ov,'original_function_count':len(funcs),'source_translation_units':paths,
            'original_source_missing':missing,'profiles':profiles,
            'constructors':[n for n in funcs if n.startswith('__sinit')],
            'bss_range':[hex(int(v,16)) for v in bss.groups()] if bss else None,
            'ambiguous_relocations':ambiguous,
            'review_required':['classify numeric islands versus RTTI/vtable prefixes','validate PMF ABI and address-point stores','close native linker dependencies','run constructors, actors and teardown against DS']})
    return {'scope':'original-source inventory, not native runtime support','overlays':result}
def link_closure(root,log):
    sys.path.insert(0,str(root/'tools'))
    import srcpath
    srcpath.set_root(root);index=srcpath.build_index()
    wanted=sorted(set(re.findall(r"undefined reference to `([^']+)'",log.read_text())))
    names=list(index)
    tool=shutil.which('arm-none-eabi-c++filt') or '/opt/devkitpro/devkitARM/bin/arm-none-eabi-c++filt'
    decoded=subprocess.run([tool],input='\n'.join(names)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
    if len(decoded)!=len(names):raise ValueError('demangler output count changed')
    matches={n:[] for n in wanted}
    for mangled,plain in zip(names,decoded):
        for n in {mangled,plain}&matches.keys():matches[n].append({'symbol':mangled,'source':index[mangled]})
    return [{'undefined':n,'original_source_candidates':matches[n],
             'next_action':'import reviewed original TU once' if matches[n] else 'inspect data/ABI or recover missing original body; no fallback generated'} for n in wanted]

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--ids');p.add_argument('--output',type=Path,required=True);p.add_argument('--link-log',type=Path);a=p.parse_args()
    rows=audit(a.root.resolve(),set(map(int,a.ids.split(','))) if a.ids else None)
    if a.link_log:rows['link_closure']=link_closure(a.root.resolve(),a.link_log)
    a.output.write_text(json.dumps(rows,indent=2)+'\n')
    print(f'{len(rows["overlays"])} overlay batches inventoried; no source or runtime modified')
