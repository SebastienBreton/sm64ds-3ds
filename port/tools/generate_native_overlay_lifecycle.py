#!/usr/bin/env python3
"""Prepare a scoped native lifecycle copy from an audited overlay inventory.

Original actors must be destroyed before unload. This adapter does not implement
dispatcher admission or establish resource, ABI or runtime correctness.
"""
import os
import argparse,json,re,struct
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inventory',type=Path,required=True);p.add_argument('--overlay',type=int,required=True)
    p.add_argument('--reset-bytes',type=int,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--root',type=Path,default=Path(os.environ.get('SM64DS_DECOMP_DIR') or Path(__file__).resolve().parents[2] / 'sm64ds-decomp'))
    a=p.parse_args();assert not a.output.exists()
    audit=json.loads(a.inventory.read_text())
    row=next(r for r in audit['overlay_inventory']['overlays'] if r['overlay']==a.overlay)
    assert not row['original_source_missing']
    profiles=row['profiles'];assert profiles and all(r['registry_matches'] for r in profiles)
    ids=[r['actor_id'] for r in profiles];assert len(ids)==len(set(ids)) and all(0<=n<512 for n in ids)
    constructors=row['constructors'];assert constructors
    cfg=a.root/f'config/arm9/overlays/ov{a.overlay:03}'
    delinks=(cfg/'delinks.txt').read_text()
    base=int(re.search(r'\.text\s+start:0x([0-9a-f]+)',delinks)[1],16)
    ctor=re.search(r'\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)',delinks)
    assert ctor
    ctor_start,ctor_end=(int(v,16) for v in ctor.groups());assert (ctor_end-ctor_start)%4==0
    blob=(a.root/f'extracted/overlays/overlay_{a.overlay:04}.bin').read_bytes()
    targets=struct.unpack_from('<'+str((ctor_end-ctor_start)//4)+'I',blob,ctor_start-base)
    names={int(addr,16):name for name,addr in re.findall(
        r'^(__sinit_\w+) kind:function\S* addr:0x([0-9a-f]+)',(cfg/'symbols.txt').read_text(),re.M)}
    assert len(targets)==len(constructors) and all(t in names for t in targets)
    constructors=[names[t] for t in targets]
    assert set(constructors)==set(row['constructors'])
    start,end=map(lambda s:int(s,0),row['bss_range']);size=end-start;assert size>0
    ov=a.overlay
    text='''// Prepared native lifecycle copy; original actors must die before unload.
// Resource/data/ABI/runtime gates remain mandatory before dispatcher admission.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
'''
    text+=f'extern unsigned char sm64ds_overlay{ov}_bss_begin[],sm64ds_overlay{ov}_bss_end[];\n'
    text+=''.join(f'extern unsigned char {r["symbol"]}[];\n' for r in profiles)
    text+=''.join(f'void {n}();\n' for n in constructors)+'}\nnamespace {\nbool loaded=false;\n'
    text+='const unsigned ids[]={'+','.join(map(str,ids))+'};\n'
    text+='void*const profiles[]={'+','.join(r['symbol'] for r in profiles)+'};\n'
    text+=f'void* previous_profiles[{len(profiles)}];\n}}\n'
    text+=f'extern "C" bool sm64ds_load_overlay{ov}(){{\n'
    text+='    if(loaded)return false;\n'
    text+=f'    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay{ov}_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay{ov}_bss_begin);\n'
    text+=f'    if(bytes!={size})return false;\n'
    text+=f'    if(sm64ds_reset_level_data({ov})!={a.reset_bytes})return false;\n'
    text+=f'    std::memset(sm64ds_overlay{ov}_bss_begin,0,bytes);\n'
    text+=f'    const int previous=sm64ds_set_overlay_scope({ov});\n'
    text+=''.join(f'    {n}();\n' for n in constructors)
    text+='    sm64ds_set_overlay_scope(previous);\n'
    text+=f'    for(unsigned i=0;i<{len(profiles)};++i){{previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}}\n'
    text+='    loaded=true;return true;\n}\n'
    text+=f'extern "C" bool sm64ds_unload_overlay{ov}(){{\n'
    text+='    if(!loaded)return false;\n'
    text+=f'    sm64ds_destroy_overlay_resources({ov});\n'
    text+=f'    for(unsigned i=0;i<{len(profiles)};++i)data_020a4bb8[ids[i]]=previous_profiles[i];\n'
    text+='    loaded=false;return true;\n}\n'
    a.output.write_text(text)
    print('PREPARED lifecycle only',ov,'profiles',len(profiles),'constructors',len(constructors),'BSS',size)


if __name__=='__main__':main()
