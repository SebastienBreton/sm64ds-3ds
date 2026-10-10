#!/usr/bin/env python3
"""Native ov007 data: checked Nitro blobs, relocated tables and unified BSS.

Never copy the retail RTTI/vtables: the native compiler owns those. Apparent
pointer relocations inside validated Nitro resources are pixel/tile bytes.
"""
import argparse,json,re,struct
from pathlib import Path
BASE=0x020ad660
RANGES=[(0x020ccb54,0x02103264),(0x02103290,0x021032a8)]
BSS=(0x02103340,0x02104c40)

def symbols(path):
    return [(int(m[3],16),m[1],m[2]) for m in re.finditer(
        r'^(\S+) kind:(\S+) addr:0x([0-9a-f]+)',path.read_text(),re.M)]

def generate(root, output):
    blobpath=(root/'extracted/overlays/overlay_0007.bin').resolve()
    blob=blobpath.read_bytes()
    if BASE+len(blob)!=BSS[0]:raise ValueError('ov007 image changed')
    cfg=root/'config/arm9/overlays/ov007'
    syms=symbols(cfg/'symbols.txt')
    own={a:n for a,n,k in syms}
    main={a:n for a,n,k in symbols(root/'config/arm9/symbols.txt')}
    # Twenty contiguous typed Nitro resources. Validate each header size.
    assets=[];a=0x020d87b8
    while a<0x02102dc4:
        magic,bom,version,size,header,blocks=struct.unpack_from('<4sHHIHH',blob,a-BASE)
        if magic not in (b'NCOB',b'NCCL',b'NCSC',b'NCCG') or bom!=0xfeff or header!=16 or not 16<=size<=0x20000:
            raise ValueError(f'Invalid Nitro resource at {a:x}')
        assets.append((a,a+size,magic.decode()));a=(a+size+3)&~3
    if a!=0x02102dc4 or len(assets)!=20:raise ValueError('Nitro resources extent changed')
    def imported(a):return any(lo<=a<hi for lo,hi in RANGES) or BSS[0]<=a<BSS[1]
    aliases={a:[] for a,n,k in syms if imported(a)}
    for a,n,k in syms:
        if imported(a):aliases[a].append(n)
    relocs={};ignored=[]
    for m in re.finditer(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(.*)',(cfg/'relocs.txt').read_text()):
        source,target=int(m[1],16),int(m[3],16)
        if not any(lo<=source<hi for lo,hi in RANGES):continue
        if any(lo<=source<hi for lo,hi,_ in assets):
            ignored.append(source);continue
        if m[2]!='load' or source%4 or struct.unpack_from('<I',blob,source-BASE)[0]!=target:
            raise ValueError(f'Unexpected relocation {m[0]}')
        if m[4]=='overlay(7)':
            if imported(target):
                name=f'sm64ds_menu_address_{target:08x}'
                if name not in aliases.setdefault(target,[]):aliases[target].append(name)
            elif target in own and target<RANGES[0][0]:name=own[target]
            else:raise ValueError(f'Unresolved local target {target:x}')
        elif m[4]=='main' and target in main:name=main[target]
        else:raise ValueError(f'Unverified external relocation {m[0]}')
        relocs[source]=name
    lines=['// Generated checked retail-menu storage.','asm(R"MENU(','.pushsection .data.sm64ds_menu,"aw",%progbits','.balign 4']
    for i,(lo,hi) in enumerate(RANGES):
        lines += [f'.global sm64ds_menu_data_{i}_begin',f'sm64ds_menu_data_{i}_begin:']
        points=sorted({lo,hi}|{a for a in aliases if lo<=a<hi}|{a for a in relocs if lo<=a<hi})
        cursor=lo
        for a in points:
            if a<cursor:raise ValueError('Interior alias cuts a pointer word')
            if a>cursor:lines.append(f'.incbin "{blobpath}", {cursor-BASE}, {a-cursor}')
            for n in aliases.get(a,[]):lines += [f'.global {n}',n+':']
            if a in relocs:
                lines.append('.word '+relocs[a]);cursor=a+4
            else:cursor=a
        lines += [f'.global sm64ds_menu_data_{i}_end',f'sm64ds_menu_data_{i}_end:']
    lines += ['.popsection','.pushsection .bss.sm64ds_menu,"aw",%nobits','.balign 4','.global sm64ds_menu_bss_begin','sm64ds_menu_bss_begin:']
    cursor=BSS[0]
    for a in sorted(a for a in aliases if BSS[0]<=a<BSS[1]):
        if a>cursor:lines.append(f'.zero {a-cursor}')
        for n in aliases[a]:lines += [f'.global {n}',n+':']
        cursor=a
    lines += [f'.zero {BSS[1]-cursor}', '.global sm64ds_menu_bss_end','sm64ds_menu_bss_end:','.popsection',')MENU");']
    # ARM9 menu session state is permanent; preserve its overlapping byte views.
    common_lo,common_hi=0x0209b33c,0x0209b3ec
    lines += ['asm(R"COMMON(','.pushsection .bss.sm64ds_menu_common,"aw",%nobits','.balign 4']
    cursor=common_lo
    for a,n,k in symbols(root/'config/arm9/symbols.txt'):
        if common_lo<=a<common_hi:
            if a>cursor:lines.append(f'.zero {a-cursor}')
            lines += [f'.global {n}',n+':'];cursor=a
    lines += [f'.zero {common_hi-cursor}', '.popsection', ')COMMON");']
    arm9=(root/'extracted/arm9_dec.bin').read_bytes()
    if struct.unpack_from('<2I',blob,0x02103264-BASE)!=(0x020ccad0,0x00030001):
        raise ValueError('Menu profile changed')
    if struct.unpack_from('<I',arm9,0x02090864-0x02004000+4)[0]!=0x02103264:
        raise ValueError('Menu registry id changed')
    if struct.unpack_from('<I',blob,0x020d7db8-BASE)[0]!=0:
        raise ValueError('Menu constructor table changed')
    lines += ['extern "C" {','void* dScDSMT_c_classInit();',
              'struct MenuProfile {void*(*create)();unsigned short behavior,render;};',
              'static_assert(sizeof(MenuProfile)==8,"menu profile ABI");',
              'MenuProfile g_profile_DSMT={dScDSMT_c_classInit,1,3};','}']
    # Checked numeric ARM9 lookup table and RNG seed. No pointers in these spans.
    arm9path=(root/'extracted/arm9_dec.bin').resolve()
    for name,lo,hi in [('data_02086214',0x02086214,0x02086314),('data_0209a624',0x0209a624,0x0209a628)]:
        lines += ['asm(R"NUMERIC(','.pushsection .data.sm64ds_menu_numeric,"aw",%progbits','.balign 4',
                  f'.global {name}',name+':',f'.incbin "{arm9path}", {lo-0x02004000}, {hi-lo}',
                  '.popsection',')NUMERIC");']
    lines += ['asm(R"POOLS(','.pushsection .bss.sm64ds_menu_sdk,"aw",%nobits','.balign 4',
              '.global data_020a637c','data_020a637c:','.zero 36','.popsection',')POOLS");']
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(lines)+'\n')
    report={'data_ranges':RANGES,'bss':BSS,'nitro_resources':assets,'native_pointer_relocations':len(relocs),'ignored_pixel_relocations':[hex(a) for a in ignored]}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();print(json.dumps(generate(a.root,a.output),indent=2))
