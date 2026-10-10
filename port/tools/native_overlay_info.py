#!/usr/bin/env python3
"""Classify native overlay support using the actual ROM header and module symbols."""
from pathlib import Path
import argparse,re,struct,json
from all_level_data import collect,reset_records
from minigame_imports import audit as audit_minigame_imports
from native_actor_lifecycle import modules as actor_lifecycle_modules

def generate(root,rom):
    with rom.open('rb') as f:
        header=f.read(0x60)
        offset,size=struct.unpack_from('<II',header,0x50)
        if size%32:raise ValueError('invalid ARM9 overlay table size')
        f.seek(offset);table=f.read(size)
    if len(table)!=size:raise ValueError('short overlay table')
    sizes={}
    for ov,start,end,*_ in reset_records(root,collect(root)):
        sizes[ov]=sizes.get(ov,0)+end-start
    actor_modules={m['overlay']:m for m in actor_lifecycle_modules(json.loads(Path(__file__).with_name('native_actor_overlay_manifest.json').read_text()))}
    rows=[];supported=[]
    for offset in range(0,size,32):
        ov,base,ram,bss,begin,end,file_id,flags=struct.unpack_from('<8I',table,offset)
        cfg=root/f'config/arm9/overlays/ov{ov:03}/symbols.txt'
        blob=(root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
        if len(blob)!=ram or not base<=begin<=end<=base+ram:raise ValueError(f'overlay{ov}: unexpected layout')
        functions=[]
        for line in cfg.read_text().splitlines():
            m=re.match(r'(\S+) kind:function\S+ addr:0x([0-9a-f]+)',line)
            if m:functions.append(int(m[2],16))
        values=list(struct.unpack_from('<'+str((end-begin)//4)+'I',blob,begin-base))
        if not values or values[-1]!=0 or any(v==0 for v in values[:-1]):raise ValueError('unexpected constructor terminator')
        constructors=values[:-1]
        kind=0
        if 8<=ov<60 and not bss and not functions and not constructors:kind=1
        if ov==9:
            if bss!=704 or constructors!=[0x02112458,0x02112524,0x02112a5c,0x02112ac8]:raise ValueError('castle layout changed')
            kind=2
        if ov==10:
            if base!=0x021111a0 or bss!=128 or constructors!=[0x0211203c,0x0211211c,0x0211215c]:raise ValueError('castle interior layout changed')
            kind=15
        if ov==63:
            if base!=0x02115ee0 or bss!=544 or constructors!=[0x0211e29c,0x0211e3cc,0x0211e590,0x0211e5fc]:raise ValueError('overlay63 layout changed')
            kind=16
        if ov==80:
            if base!=0x02123740 or bss!=1184 or constructors!=[0x021278c0,0x02127a60,0x02127b2c]:raise ValueError('overlay80 layout changed')
            kind=17
        if ov==3:
            module=next(m for m in json.loads(Path(__file__).with_name('native_actor_overlay_manifest.json').read_text())['modules'] if m['overlay']==3)
            if base!=module['base'] or ram!=16960 or bss or constructors or module['constructors']:
                raise ValueError('StarSel native lifecycle changed')
            if sum(4*len(s['words']) for s in module['sections'])!=module.get('numeric_bytes',88) or sizes.get(ov,0):
                raise ValueError('StarSel numeric islands/reset changed')
            # Private validated lifecycle retains these 88 numeric bytes;
            # they are imported storage, not a level reset span.
            sizes[ov]=module.get('numeric_bytes',88)
            kind=18
        if ov==85:
            if bss!=1024 or base!=0x02129020 or constructors!=[0x0212f2a8,0x0212f3a0,0x0212f5ec,0x0212f9bc,0x0212fa40,0x0212fdb0]:raise ValueError('overlay85 layout changed')
            kind=3
        if ov==98:
            if bss!=352 or base!=0x02137be0 or constructors!=[0x0213bf9c,0x0213c058,0x0213c214,0x0213c2b4]:raise ValueError('overlay98 layout changed')
            kind=4
        if ov==62:
            if bss!=960 or base!=0x02115ee0 or constructors!=[0x0211cf30,0x0211d2d8,0x0211d4a0,0x0211d690,0x0211d6fc]:raise ValueError('overlay62 layout changed')
            kind=5
        # Retail placeholder overlays carry only a zero constructor terminator
        # and alignment padding. Preserve residency/overlap checks for them.
        if not bss and not constructors and not functions and blob==bytes(32):
            if cfg.read_text().strip():raise ValueError('empty overlay has symbols')
            if (cfg.parent/'relocs.txt').read_text().strip():raise ValueError('empty overlay has relocations')
            kind=6
        if ov==89:
            if bss!=256 or base!=0x02130f00 or constructors!=[0x021328d4]:raise ValueError('overlay89 layout changed')
            kind=7
        if ov==94:
            if bss!=160 or base!=0x02135700 or constructors!=[0x021367e8]:raise ValueError('overlay94 layout changed')
            kind=8
        if ov==100:
            if bss!=1152 or base!=0x02140d80 or constructors!=[0x021473bc,0x021474e8,0x021475a4,0x02147698,0x02147a70,0x02147bc0,0x02147d7c]:raise ValueError('overlay100 layout changed')
            kind=9
        if ov==102:
            if bss!=960 or base!=0x02148a80 or constructors!=[0x0214d714,0x0214d908,0x0214de70,0x0214dfac]:raise ValueError('overlay102 layout changed')
            kind=10
        if ov==7:
            if base!=0x020ad660 or ram!=0x55ce0 or bss!=0x1900 or constructors:
                raise ValueError('retail menu overlay layout changed')
            kind=11
        if ov==5:
            if base!=0x020bfec0 or ram!=0x3160 or bss or constructors:
                raise ValueError('minigame menu overlay layout changed')
            kind=12
        if ov==4:
            if base!=0x020ad660 or ram!=70912 or bss!=4960 or constructors!=[0x020b948c,0x020b955c,0x020b9ad0,0x020b9b24]:
                raise ValueError('mini-game base overlay layout changed')
            kind=13
        if ov==6:
            if base!=0x020bfec0 or ram!=525344 or bss!=11712 or len(constructors)!=31 or constructors[16]!=0x02130f64:
                raise ValueError('Wanted overlay layout changed')
            audit_minigame_imports(root)
            # Partial native closure: the shared manifest lists reviewed slices.
            kind=14
        if ov in actor_modules:
            module=actor_modules[ov]
            symbols={n:int(a,16) for n,a in re.findall(r'^(\w+) kind:\S+ addr:0x([0-9a-f]+)',cfg.read_text(),re.M)}
            if base!=module['base'] or bss!=module['bss_bytes'] or constructors!=[symbols[n] if n in symbols else int(re.fullmatch(r'__sinit_ov\d{3}_([0-9a-f]{8})',n)[1],16) for n in module['constructors']]:
                raise ValueError(f'overlay{ov} native actor lifecycle changed')
            if sizes.get(ov,0)!=module['reset_bytes']:raise ValueError('actor level reset size changed')
            kind=module['native_kind']
        if kind:supported.append(ov)
        rows.append(f'    {{{ov},0x{base:x},{ram+bss},{sizes.get(ov,0)},{kind}}},')
    print('Native lifecycle support:',supported)
    return ('// Generated from local ROM; kind0 remains explicitly unsupported.\n'
            '#include "unmatched/native_overlay.h"\nextern "C" {\n'
            'extern const NativeOverlayInfo sm64ds_native_overlays[]={\n'+'\n'.join(rows)+
            '\n};\nextern const unsigned sm64ds_native_overlay_count=sizeof(sm64ds_native_overlays)/sizeof(sm64ds_native_overlays[0]);\n}\n')
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',type=Path);ap.add_argument('rom',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();a.output.write_text(generate(a.root,a.rom))
